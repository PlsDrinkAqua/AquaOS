#!/bin/sh
set -eu
cd "$(dirname "$0")"
python3 tools/build64.py
python3 ports/tinycc/build.py --arch x86_64
python3 tools/iso64.py
# Each run gets a fresh disk; previous guest files remain in earlier images.
mkdir -p disk
zenos64_work=$(mktemp -d disk/zenos64.XXXXXX)
python3 tools/disk64.py "$zenos64_work/dev.img"
printf 'Guest disk retained at %s/dev.img\n' "$zenos64_work"
printf 'Packet capture retained at %s/network.pcap\n' "$zenos64_work"
exec qemu-system-x86_64 -m 128M -cdrom build/x86_64/myos.iso \
    -drive "file=$zenos64_work/dev.img,format=raw,if=ide" \
    -netdev user,id=net0,net=10.0.2.0/24,host=10.0.2.2,dhcpstart=10.0.2.15 \
    -device e1000,netdev=net0,mac=52:54:00:12:34:56 \
    -object "filter-dump,id=netcapture,netdev=net0,file=$zenos64_work/network.pcap" "$@"
