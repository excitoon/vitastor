#!/bin/bash -ex

. `dirname $0`/common.sh

OSD_SIZE=1024
OSD_COUNT=5
OSD_ARGS=
for i in $(seq 1 $OSD_COUNT); do
    dd if=/dev/zero of=./testdata/test_osd$i.bin bs=1024 count=1 seek=$((OSD_SIZE*1024-1))
    build/src/vitastor-osd --osd_num $i --bind_address 127.0.0.1 $OSD_ARGS --etcd_address $ETCD_URL $(build/src/vitastor-disk simple-offsets --format options ./testdata/test_osd$i.bin 2>/dev/null) &>./testdata/osd$i.log &
    eval OSD${i}_PID=$!
done

$ETCDCTL put /vitastor/config/pools '{"1":{"name":"testpool","scheme":"replicated","pg_size":2,"pg_minsize":1,"pg_count":1,"failure_domain":"osd"}}'

$ETCDCTL put /vitastor/config/pgs '{"items":{"1":{"1":{"osd_set":[1,0],"primary":1}}}}'

sleep 2

if ! ($ETCDCTL get /vitastor/pg/state/1/1 --print-value-only | jq -s -e '(. | length) != 0 and .[0].state == ["active","degraded"]'); then
    format_error "Failed to start the PG active+degraded"
fi

LD_PRELOAD="build/src/libfio_vitastor.so" \
fio -thread -name=test -ioengine=build/src/libfio_vitastor.so -bs=4M -direct=1 -iodepth=1 -fsync=1 -rw=write \
    -etcd=$ETCD_URL -pool=1 -inode=2 -size=32M -cluster_log_level=10

$ETCDCTL put /vitastor/config/pgs '{"items":{"1":{"1":{"osd_set":[1,0],"primary":0}}}}'

sleep 2

if [ "`$ETCDCTL get /vitastor/pg/state/1/1 --print-value-only`" != "" ]; then
    format_error "Failed to stop the PG"
fi

$ETCDCTL put /vitastor/pg/history/1/1 '{"all_peers":[1,2,3]}'

$ETCDCTL put /vitastor/config/pgs '{"items":{"1":{"1":{"osd_set":[4,5],"primary":4}}}}'

sleep 5

if ! ($ETCDCTL get /vitastor/pg/state/1/1 --print-value-only | jq -s -e '(. | length) != 0 and .[0].state == ["active"]'); then
    format_error "Failed to move degraded objects to the clean OSD set"
fi

$ETCDCTL put /vitastor/config/pgs '{"items":{"1":{"1":{"osd_set":[4,5],"primary":0}}}}'

$ETCDCTL put /vitastor/pg/history/1/1 '{"all_peers":[1,2,3]}'

sleep 2

if [ "`$ETCDCTL get /vitastor/pg/state/1/1 --print-value-only`" != "" ]; then
    format_error "Failed to stop the PG after degraded recovery"
fi

cp testdata/osd4.log testdata/osd4_pre.log
>testdata/osd4.log

$ETCDCTL put /vitastor/config/pgs '{"items":{"1":{"1":{"osd_set":[4,5],"primary":4}}}}'

sleep 2

if grep -q 'PG 1/1.*is.*has_' testdata/osd4.log; then
    format_error "PG has degraded or misplaced objects after a full re-peer following a degraded recovery"
fi

if ! ($ETCDCTL get /vitastor/pg/state/1/1 --print-value-only | jq -s -e '(. | length) != 0 and .[0].state == ["active"]'); then
    format_error "PG not active+clean after a full re-peer following a degraded recovery"
fi

format_green OK
