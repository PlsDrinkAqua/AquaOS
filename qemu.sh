#!/bin/sh
set -e

cd "$(dirname "$0")"
qemu_host=${HOST:-$(./default-host.sh)}
qemu_disk=disk/zenos-dev.img

# Create the TinyCC development disk once; later boots retain user files.
if [ ! -e "$qemu_disk" ]; then
    python3 ports/tinycc/build.py
    python3 ports/tinycc/make_disk.py "$qemu_disk"
fi

sh ./iso.sh
exec "qemu-system-$(./target-triplet-to-arch.sh "$qemu_host")" \
    -m 128M -cdrom myos.iso \
    -drive "file=$qemu_disk,format=raw,if=ide" "$@"
