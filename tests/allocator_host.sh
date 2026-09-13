#!/bin/sh
set -eu
ulimit -c 0
cd "$(dirname "$0")/.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cc -std=c11 -Wall -Wextra -Werror -Ikernel/include -Ilibk/include \
    -c kernel/kernel/MEM/kmalloc.c -o "$work/kernel.o"
cc -std=c11 -Wall -Wextra -Werror -fno-builtin -Iuserlibc/include \
    -Drealloc=zenos_realloc -Derrno=zenos_errno -Dmalloc=zenos_malloc -Dfree=zenos_free -Dcalloc=zenos_calloc -Dsbrk=zenos_sbrk \
    -c userlibc/stdlib/malloc.c -o "$work/user.o"
cc -std=c11 -Wall -Wextra -Werror tests/allocator_host.c \
    "$work/kernel.o" "$work/user.o" -o "$work/check"
"$work/check"
"$work/check" user
