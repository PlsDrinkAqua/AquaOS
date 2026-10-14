#pragma once
#include <stdint.h>
#define KERNEL_LINK_BASE UINT64_C(0xffffffff80000000)
#define KERNEL_DIRECT_BASE UINT64_C(0xffff800000000000)
#define KERNEL_DIRECT_SIZE UINT64_C(0x1000000000) /* 64 GiB, boot.S must match. */
#define KERNEL_HEAP_BASE UINT64_C(0xffffffffc0000000)
/* Dedicated device window, beyond the kernel heap. */
#define KERNEL_MMIO_BASE UINT64_C(0xffffffffe0000000)
#define USER_ADDRESS_LIMIT UINT64_C(0x0000800000000000)
