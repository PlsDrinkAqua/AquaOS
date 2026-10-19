#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cc -std=gnu11 -O2 -Wall -Wextra -Werror -fno-builtin -Iuserlibc/include \
    -DHOST_TEST -Derrno=aquaos_errno \
    -Dmemcpy=aquaos_memcpy -Dmemmove=aquaos_memmove -Dmemset=aquaos_memset -Dmemcmp=aquaos_memcmp \
    -Dstrcpy=aquaos_strcpy -Dstrchr=aquaos_strchr -Dstrrchr=aquaos_strrchr -Dstrstr=aquaos_strstr \
    -Dstrtol=aquaos_strtol -Dstrtoul=aquaos_strtoul -Dstrtoll=aquaos_strtoll \
    -Dstrtoull=aquaos_strtoull -Datoi=aquaos_atoi -Dqsort=aquaos_qsort \
    tests/libc_core.c userlibc/string/memory.c userlibc/string/search.c \
    userlibc/stdlib/strto.c userlibc/stdlib/qsort.c -o "$work/check"
"$work/check"
