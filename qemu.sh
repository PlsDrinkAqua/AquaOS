#!/bin/sh
set -e

cd "$(dirname "$0")"
qemu_host=${HOST:-$(./default-host.sh)}
qemu_disk=disk/zenos-dev.img

# Rebuild the compiler, user programs and kernel before every boot.
python3 ports/tinycc/build.py
sh ./iso.sh

# Publish a fresh disk only after its creation and filesystem checks succeed.
# Files created inside the previous guest disk are intentionally discarded.
mkdir -p disk
qemu_work=$(mktemp -d disk/.qemu-build.XXXXXX)
trap 'rm -rf "$qemu_work"' EXIT
trap 'exit 1' HUP INT TERM
python3 ports/tinycc/make_disk.py "$qemu_work/zenos-dev.img"
mv -f "$qemu_work/zenos-dev.img" "$qemu_disk"
rmdir "$qemu_work"
trap - EXIT HUP INT TERM

exec "qemu-system-$(./target-triplet-to-arch.sh "$qemu_host")" \
    -m 128M -cdrom myos.iso \
    -drive "file=$qemu_disk,format=raw,if=ide" "$@"
