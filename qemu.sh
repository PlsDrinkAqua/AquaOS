#!/bin/sh
set -eu
cd "$(dirname "$0")"
if [ "${AQUAOS_ARCH:-x86_64}" != x86_64 ]; then
    printf "AquaOS supports only x86_64\n" >&2
    exit 1
fi
exec sh ./qemu64.sh "$@"
