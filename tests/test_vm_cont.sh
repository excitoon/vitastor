#!/bin/bash -ex

export KEEP_DATA=1
. `dirname $0`/common.sh

etcdctl --endpoints=http://127.0.0.1:12379/v3 del --prefix /vitastor/mon/master
etcdctl --endpoints=http://127.0.0.1:12379/v3 del --prefix /vitastor/pg/state
etcdctl --endpoints=http://127.0.0.1:12379/v3 del --prefix /vitastor/osd/state

OSD_COUNT=3
OSD_ARGS=
for i in $(seq 1 $OSD_COUNT); do
    build/src/vitastor-osd --osd_num $i --bind_address 127.0.0.1 $OSD_ARGS --etcd_address $ETCD_URL $(build/src/vitastor-disk simple-offsets --format options ./testdata/test_osd$i.bin 2>/dev/null) &>./testdata/osd$i.log &
    eval OSD${i}_PID=$!
done

node mon/mon-main.js --etcd_url $ETCD_URL --etcd_prefix "/vitastor" &>./testdata/mon.log &
MON_PID=$!

sleep 3

if ! ($ETCDCTL get /vitastor/pg/state/1/1 --print-value-only | jq -s -e '(. | length) != 0 and .[0].state == ["active"]'); then
    format_error "FAILED: 1 PG NOT UP"
fi

if ! cmp build/src/block-vitastor.so /usr/lib/x86_64-linux-gnu/qemu/block-vitastor.so; then
    sudo rm -f /usr/lib/x86_64-linux-gnu/qemu/block-vitastor.so
    sudo ln -s "$(realpath .)/build/src/block-vitastor.so" /usr/lib/x86_64-linux-gnu/qemu/block-vitastor.so
fi

qemu-system-x86_64 -enable-kvm -m 1024 \
    -drive 'file=vitastor:etcd_host=127.0.0.1\:'$ETCD_PORT'/v3:image=debian9',format=raw,if=none,id=drive-virtio-disk0,cache=none \
    -device virtio-blk-pci,scsi=off,bus=pci.0,addr=0x5,drive=drive-virtio-disk0,id=virtio-disk0,bootindex=1,write-cache=off,physical_block_size=4096,logical_block_size=512 \
    -vnc 0.0.0.0:0

format_green OK
