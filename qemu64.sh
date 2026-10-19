#!/bin/sh
set -eu
cd "$(dirname "$0")"
python3 tools/build64.py
python3 ports/tinycc/build.py --arch x86_64
python3 tools/iso64.py
# Each run gets a fresh disk; previous guest files remain in earlier images.
mkdir -p disk
aquaos64_work=$(mktemp -d disk/aquaos64.XXXXXX)
python3 tools/disk64.py "$aquaos64_work/dev.img"
printf 'Guest disk retained at %s/dev.img\n' "$aquaos64_work"
printf 'Packet capture retained at %s/network.pcap\n' "$aquaos64_work"
exec qemu-system-x86_64 -m 128M -cdrom build/x86_64/aquaos.iso \
    -drive "file=$aquaos64_work/dev.img,format=raw,if=ide" \
    -netdev user,id=net0,net=6.6.6.0/24,host=6.6.6.6,dhcpstart=6.6.6.15 \
    -device e1000,netdev=net0,mac=52:54:00:12:34:56 \
    -object "filter-dump,id=netcapture,netdev=net0,file=$aquaos64_work/network.pcap" "$@"
