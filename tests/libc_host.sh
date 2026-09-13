#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cc -std=gnu11 -O2 -Wall -Wextra -Werror -fno-builtin -Iuserlibc/include \
    -DHOST_TEST -Derrno=zenos_errno \
    -Dmemcpy=zenos_memcpy -Dmemmove=zenos_memmove -Dmemset=zenos_memset -Dmemcmp=zenos_memcmp \
    -Dstrcpy=zenos_strcpy -Dstrchr=zenos_strchr -Dstrrchr=zenos_strrchr -Dstrstr=zenos_strstr \
    -Dstrtol=zenos_strtol -Dstrtoul=zenos_strtoul -Dstrtoll=zenos_strtoll \
    -Dstrtoull=zenos_strtoull -Datoi=zenos_atoi -Dqsort=zenos_qsort \
    tests/libc_core.c userlibc/string/memory.c userlibc/string/search.c \
    userlibc/stdlib/strto.c userlibc/stdlib/qsort.c -o "$work/check"
"$work/check"
