#pragma once
#include <stdint.h>
typedef struct { uint32_t ip; uint16_t port, reserved; } aquaos_udp_address;
/* Fixed LP64 syscall request. Numeric addresses and ports use host integers.
 * SEND: ebx=fd, ecx=request. RECEIVE: ebx=fd, ecx=request, edx=source output.
 * RECEIVE reads data/length; the byte count is returned in eax. The request
 * itself is input-only. Reserved fields must be zero. */
typedef struct {
    uint64_t data;
    uint64_t length;
    uint32_t ip;
    uint16_t port;
    uint16_t reserved;
} aquaos_udp_request_t;
/* Keep the size check compatible with the native compiler's C dialect. */
typedef char aquaos_udp_request_size_check[sizeof(aquaos_udp_request_t) == 24 ? 1 : -1];
