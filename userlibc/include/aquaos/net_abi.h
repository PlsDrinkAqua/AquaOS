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

/* Single-interface snapshot, syscall 39. IPv4 integers are 0xAABBCCDD.
 * Lease values are seconds; zero means no lease, UINT32_MAX infinite. */
enum {
    AQUAOS_DHCP_NONE, AQUAOS_DHCP_SELECTING, AQUAOS_DHCP_REQUESTING,
    AQUAOS_DHCP_BOUND, AQUAOS_DHCP_RENEWING, AQUAOS_DHCP_REBINDING,
    AQUAOS_DHCP_FALLBACK
};
#define AQUAOS_NET_PRESENT 1U
#define AQUAOS_NET_LINK_UP 2U
typedef struct {
    uint32_t ip, netmask, gateway, dns, server;
    uint32_t lease_seconds, lease_remaining, dhcp_state, flags;
    uint8_t mac[6], reserved[2];
} aquaos_net_info;
typedef char aquaos_net_info_size_check[sizeof(aquaos_net_info) == 44 ? 1 : -1];
