// Copyright (c) Vitaliy Filippov, 2019+
// License: VNPL-1.1 (see README.md for details)

#include "blockstore_impl.h"

// Stabilize small write:
// 1) Copy data from the journal to the data device
// 2) Increase version on the metadata device and sync it
// 3) Advance clean_db entry's version, clear previous journal entries
//
// This makes 1 4K small write+sync look like:
// 512b+4K (journal) + sync + 512b (journal) + sync + 4K (data) [+ sync?] + 512b (metadata) + sync.
// WA = 2.375. It's not the best, SSD FTL-like redirect-write could probably be lower
// even with defragmentation. But it's fixed and it's still better than in Ceph. :)
// except for HDD-only clusters, because each write results in 3 seeks.

// Stabilize big write:
// 1) Copy metadata from the journal to the metadata device
// 2) Move dirty_db entry to clean_db and clear previous journal entries
//
// This makes 1 128K big write+sync look like:
// 128K (data) + sync + 512b (journal) + sync + 512b (journal) + sync + 512b (metadata) + sync.
// WA = 1.012. Very good :)

// Stabilize delete:
// 1) Remove metadata entry and sync it
// 2) Remove dirty_db entry and clear previous journal entries
// We have 2 problems here:
// - In the cluster environment, we must store the "tombstones" of deleted objects until
//   all replicas (not just quorum) agrees about their deletion. That is, "stabilize" is
//   not possible for deletes in degraded placement groups
// - With simple "fixed" metadata tables we can't just clear the metadata entry of the latest
//   object version. We must clear all previous entries, too.
// FIXME Fix both problems - probably, by switching from "fixed" metadata tables to "dynamic"

// AND We must do it in batches, for the sake of reduced fsync call count
// AND We must know what we stabilize. Basic workflow is like:
// 1) primary OSD receives sync request
// 2) it submits syncs to blockstore and peers
// 3) after everyone acks sync it acks sync to the client
// 4) after a while it takes his synced object list and sends stabilize requests
//    to peers and to its own blockstore, thus freeing the old version

int blockstore_impl_t::dequeue_stable(blockstore_op_t *op)
{
    if (PRIV(op)->op_state)
    {
        return continue_stable(op);
    }
    obj_ver_id* v;
    int i, todo = 0;
    for (i = 0, v = (obj_ver_id*)op->buf; i < op->len; i++, v++)
    {
        auto dirty_it = dirty_db.find(*v);
        if (dirty_it == dirty_db.end())
        {
            auto & clean_db = clean_db_shard(v->oid);
            auto clean_it = clean_db.find(v->oid);
            if (clean_it == clean_db.end() || clean_it->second.version < v->version)
            {
                // No such object version
                op->retval = -ENOENT;
                FINISH_OP(op);
                return 2;
            }
            else
            {
                // Already stable
            }
        }
        else if (IS_IN_FLIGHT(dirty_it->second.state))
        {
            // Object write is still in progress. Wait until the write request completes
            return 0;
        }
        else if (!IS_SYNCED(dirty_it->second.state))
        {
            // Object not synced yet. Caller must sync it first
            op->retval = -EBUSY;
            FINISH_OP(op);
            return 2;
        }
        else if (!IS_STABLE(dirty_it->second.state))
        {
            todo++;
        }
    }
    if (!todo)
    {
        // Already stable
        op->retval = 0;
        FINISH_OP(op);
        return 2;
    }
    // Check journal space
    blockstore_journal_check_t space_check(this);
    if (!space_check.check_available(op, todo, sizeof(journal_entry_stable), 0))
    {
        return 0;
    }
    // There is sufficient space. Check SQEs
    BS_SUBMIT_CHECK_SQES(space_check.sectors_to_write);
    // Prepare and submit journal entries
    int s = 0;
    for (i = 0, v = (obj_ver_id*)op->buf; i < op->len; i++, v++)
    {
        // FIXME: Only stabilize versions that aren't stable yet
        if (!journal.entry_fits(sizeof(journal_entry_stable)) &&
            journal.sector_info[journal.cur_sector].dirty)
        {
            prepare_journal_sector_write(journal.cur_sector, op);
            s++;
        }
        journal_entry_stable *je = (journal_entry_stable*)
            prefill_single_journal_entry(journal, JE_STABLE, sizeof(journal_entry_stable));
        je->oid = v->oid;
        je->version = v->version;
        je->crc32 = je_crc32((journal_entry*)je);
        journal.crc32_last = je->crc32;
    }
    prepare_journal_sector_write(journal.cur_sector, op);
    s++;
    assert(s == space_check.sectors_to_write);
    PRIV(op)->op_state = 1;
    return 1;
}

int blockstore_impl_t::continue_stable(blockstore_op_t *op)
{
    if (PRIV(op)->op_state == 2)
        goto resume_2;
    else if (PRIV(op)->op_state == 4)
        goto resume_4;
    else
        return 1;
resume_2:
    if (!disable_journal_fsync)
    {
        BS_SUBMIT_GET_SQE(sqe, data);
        my_uring_prep_fsync(sqe, dsk.journal_fd, IORING_FSYNC_DATASYNC);
        data->iov = { 0 };
        data->callback = [this, op](ring_data_t *data) { handle_write_event(data, op); };
        PRIV(op)->min_flushed_journal_sector = PRIV(op)->max_flushed_journal_sector = 0;
        PRIV(op)->pending_ops = 1;
        PRIV(op)->op_state = 3;
        return 1;
    }
resume_4:
    // Mark dirty_db entries as stable, acknowledge op completion
    obj_ver_id* v;
    int i;
    for (i = 0, v = (obj_ver_id*)op->buf; i < op->len; i++, v++)
    {
        // Mark all dirty_db entries up to op->version as stable
#ifdef BLOCKSTORE_DEBUG
        printf("Stabilize %lx:%lx v%lu\n", v->oid.inode, v->oid.stripe, v->version);
#endif
        mark_stable(*v);
    }
    // Acknowledge op
    op->retval = 0;
    FINISH_OP(op);
    return 2;
}

void blockstore_impl_t::mark_stable(const obj_ver_id & v, bool forget_dirty)
{
    auto dirty_it = dirty_db.find(v);
    if (dirty_it != dirty_db.end())
    {
        while (1)
        {
            bool was_stable = IS_STABLE(dirty_it->second.state);
            if ((dirty_it->second.state & BS_ST_WORKFLOW_MASK) == BS_ST_SYNCED)
            {
                dirty_it->second.state = (dirty_it->second.state & ~BS_ST_WORKFLOW_MASK) | BS_ST_STABLE;
                // Allocations and deletions are counted when they're stabilized
                if (IS_BIG_WRITE(dirty_it->second.state))
                {
                    int exists = -1;
                    if (dirty_it != dirty_db.begin())
                    {
                        auto prev_it = dirty_it;
                        prev_it--;
                        if (prev_it->first.oid == v.oid)
                        {
                            exists = IS_DELETE(prev_it->second.state) ? 0 : 1;
                        }
                    }
                    if (exists == -1)
                    {
                        auto & clean_db = clean_db_shard(v.oid);
                        auto clean_it = clean_db.find(v.oid);
                        exists = clean_it != clean_db.end() ? 1 : 0;
                    }
                    if (!exists)
                    {
                        inode_space_stats[dirty_it->first.oid.inode] += dsk.data_block_size;
                    }
                }
                else if (IS_DELETE(dirty_it->second.state))
                {
                    auto & sp = inode_space_stats[dirty_it->first.oid.inode];
                    if (sp > dsk.data_block_size)
                        sp -= dsk.data_block_size;
                    else
                        inode_space_stats.erase(dirty_it->first.oid.inode);
                }
            }
            if (forget_dirty && (IS_BIG_WRITE(dirty_it->second.state) ||
                IS_DELETE(dirty_it->second.state)))
            {
                // Big write overrides all previous dirty entries
                auto erase_end = dirty_it;
                while (dirty_it != dirty_db.begin())
                {
                    dirty_it--;
                    if (dirty_it->first.oid != v.oid)
                    {
                        dirty_it++;
                        break;
                    }
                }
                auto & clean_db = clean_db_shard(v.oid);
                auto clean_it = clean_db.find(v.oid);
                uint64_t clean_loc = clean_it != clean_db.end()
                    ? clean_it->second.location : UINT64_MAX;
                erase_dirty(dirty_it, erase_end, clean_loc);
                break;
            }
            if (was_stable || dirty_it == dirty_db.begin())
            {
                break;
            }
            dirty_it--;
            if (dirty_it->first.oid != v.oid)
            {
                break;
            }
        }
        flusher->enqueue_flush(v);
    }
    auto unstab_it = unstable_writes.find(v.oid);
    if (unstab_it != unstable_writes.end() &&
        unstab_it->second <= v.version)
    {
        unstable_writes.erase(unstab_it);
    }
}
