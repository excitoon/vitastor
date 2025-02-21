// Copyright (c) Vitaliy Filippov, 2019+
// License: VNPL-1.1 or GNU GPL-2.0+ (see README.md for details)

#pragma once

#include "object_id.h"
#include "osd_id.h"

// Magic numbers
#define SECONDARY_OSD_OP_MAGIC      0x2bd7b10325434553l
#define SECONDARY_OSD_REPLY_MAGIC   0xbaa699b87b434553l
// Operation request / reply headers have fixed size after which comes data
#define OSD_PACKET_SIZE             0x80
// Opcodes
#define OSD_OP_MIN                  1
#define OSD_OP_SEC_READ             1
#define OSD_OP_SEC_WRITE            2
#define OSD_OP_SEC_WRITE_STABLE     3
#define OSD_OP_SEC_SYNC             4
#define OSD_OP_SEC_STABILIZE        5
#define OSD_OP_SEC_ROLLBACK         6
#define OSD_OP_SEC_DELETE           7
#define OSD_OP_TEST_SYNC_STAB_ALL   8
#define OSD_OP_SEC_LIST             9
#define OSD_OP_SHOW_CONFIG          10
#define OSD_OP_READ                 11
#define OSD_OP_WRITE                12
#define OSD_OP_SYNC                 13
#define OSD_OP_DELETE               14
#define OSD_OP_PING                 15
#define OSD_OP_SEC_READ_BMP         16
#define OSD_OP_MAX                  16
#define OSD_RW_MAX                  64*1024*1024
#define OSD_PROTOCOL_VERSION        1

// Memory alignment for direct I/O (usually 512 bytes)
#ifndef DIRECT_IO_ALIGNMENT
#define DIRECT_IO_ALIGNMENT 512
#endif

// Memory allocation alignment (page size is usually optimal)
#ifndef MEM_ALIGNMENT
#define MEM_ALIGNMENT 4096
#endif

// common request and reply headers
struct __attribute__((__packed__)) osd_op_header_t
{
    // magic & protocol version
    uint64_t magic;
    // operation id
    uint64_t id;
    // operation type
    uint64_t opcode;
};

struct __attribute__((__packed__)) osd_reply_header_t
{
    // magic & protocol version
    uint64_t magic;
    // operation id
    uint64_t id;
    // operation type
    uint64_t opcode;
    // return value
    int64_t retval;
};

// read or write to the secondary OSD
struct __attribute__((__packed__)) osd_op_sec_rw_t
{
    osd_op_header_t header;
    // object
    object_id oid;
    // read/write version (automatic or specific)
    // FIXME deny values close to UINT64_MAX
    uint64_t version;
    // offset
    uint32_t offset;
    // length
    uint32_t len;
    // bitmap/attribute length - bitmap comes after header, but before data
    uint32_t attr_len;
    uint32_t pad0;
};

struct __attribute__((__packed__)) osd_reply_sec_rw_t
{
    osd_reply_header_t header;
    // for reads and writes: assigned or read version number
    uint64_t version;
    // for reads: bitmap/attribute length (just to double-check)
    uint32_t attr_len;
    uint32_t pad0;
};

// delete object on the secondary OSD
struct __attribute__((__packed__)) osd_op_sec_del_t
{
    osd_op_header_t header;
    // object
    object_id oid;
    // delete version (automatic or specific)
    uint64_t version;
};

struct __attribute__((__packed__)) osd_reply_sec_del_t
{
    osd_reply_header_t header;
    uint64_t version;
};

// sync to the secondary OSD
struct __attribute__((__packed__)) osd_op_sec_sync_t
{
    osd_op_header_t header;
};

struct __attribute__((__packed__)) osd_reply_sec_sync_t
{
    osd_reply_header_t header;
};

// stabilize or rollback objects on the secondary OSD
struct __attribute__((__packed__)) osd_op_sec_stab_t
{
    osd_op_header_t header;
    // obj_ver_id array length in bytes
    uint64_t len;
};
typedef osd_op_sec_stab_t osd_op_sec_rollback_t;

struct __attribute__((__packed__)) osd_reply_sec_stab_t
{
    osd_reply_header_t header;
};
typedef osd_reply_sec_stab_t osd_reply_sec_rollback_t;

// bulk read bitmaps from a secondary OSD
struct __attribute__((__packed__)) osd_op_sec_read_bmp_t
{
    osd_op_header_t header;
    // obj_ver_id array length in bytes
    uint64_t len;
};

struct __attribute__((__packed__)) osd_reply_sec_read_bmp_t
{
    // retval is payload length in bytes. payload is {version,bitmap}[]
    osd_reply_header_t header;
};

// show configuration
struct __attribute__((__packed__)) osd_op_show_config_t
{
    osd_op_header_t header;
    // JSON request length
    uint64_t json_len;
};

struct __attribute__((__packed__)) osd_reply_show_config_t
{
    osd_reply_header_t header;
};

// list objects on replica
struct __attribute__((__packed__)) osd_op_sec_list_t
{
    osd_op_header_t header;
    // placement group total number and total count
    pg_num_t list_pg, pg_count;
    // size of an area that maps to one PG continuously
    uint64_t pg_stripe_size;
    // inode range (used to select pools)
    uint64_t min_inode, max_inode;
};

struct __attribute__((__packed__)) osd_reply_sec_list_t
{
    osd_reply_header_t header;
    // stable object version count. header.retval = total object version count
    // FIXME: maybe change to the number of bytes in the reply...
    uint64_t stable_count;
};

// read or write to the primary OSD (must be within individual stripe)
struct __attribute__((__packed__)) osd_op_rw_t
{
    osd_op_header_t header;
    // inode
    uint64_t inode;
    // offset
    uint64_t offset;
    // length
    uint32_t len;
    // flags (for future)
    uint32_t flags;
    // inode metadata revision
    uint64_t meta_revision;
    // object version for atomic "CAS" (compare-and-set) writes
    // writes and deletes fail with -EINTR if object version differs from (version-1)
    uint64_t version;
};

struct __attribute__((__packed__)) osd_reply_rw_t
{
    osd_reply_header_t header;
    // for reads: bitmap length
    uint32_t bitmap_len;
    uint32_t pad0;
    // for reads: object version
    uint64_t version;
};

// sync to the primary OSD
struct __attribute__((__packed__)) osd_op_sync_t
{
    osd_op_header_t header;
};

struct __attribute__((__packed__)) osd_reply_sync_t
{
    osd_reply_header_t header;
};

// FIXME it would be interesting to try to unify blockstore_op and osd_op formats
union osd_any_op_t
{
    osd_op_header_t hdr;
    osd_op_sec_rw_t sec_rw;
    osd_op_sec_del_t sec_del;
    osd_op_sec_sync_t sec_sync;
    osd_op_sec_stab_t sec_stab;
    osd_op_sec_read_bmp_t sec_read_bmp;
    osd_op_sec_list_t sec_list;
    osd_op_show_config_t show_conf;
    osd_op_rw_t rw;
    osd_op_sync_t sync;
    uint8_t buf[OSD_PACKET_SIZE];
};

union osd_any_reply_t
{
    osd_reply_header_t hdr;
    osd_reply_sec_rw_t sec_rw;
    osd_reply_sec_del_t sec_del;
    osd_reply_sec_sync_t sec_sync;
    osd_reply_sec_stab_t sec_stab;
    osd_reply_sec_read_bmp_t sec_read_bmp;
    osd_reply_sec_list_t sec_list;
    osd_reply_show_config_t show_conf;
    osd_reply_rw_t rw;
    osd_reply_sync_t sync;
    uint8_t buf[OSD_PACKET_SIZE];
};

extern const char* osd_op_names[];
