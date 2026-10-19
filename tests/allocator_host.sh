#!/bin/sh
set -eu
ulimit -c 0
cd "$(dirname "$0")/.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cc -std=c11 -Wall -Wextra -Werror -Ikernel/include -Ilibk/include \
    -c kernel/kernel/MEM/kmalloc.c -o "$work/kernel.o"
cc -std=c11 -Wall -Wextra -Werror -fno-builtin -Iuserlibc/include \
    -Drealloc=aquaos_realloc -Derrno=aquaos_errno -Dmalloc=aquaos_malloc -Dfree=aquaos_free -Dcalloc=aquaos_calloc -Dsbrk=aquaos_sbrk \
    -c userlibc/stdlib/malloc.c -o "$work/user.o"
cc -std=c11 -Wall -Wextra -Werror tests/allocator_host.c \
    "$work/kernel.o" "$work/user.o" -o "$work/check"
"$work/check"
"$work/check" user
