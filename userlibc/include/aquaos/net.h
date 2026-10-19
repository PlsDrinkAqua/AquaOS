#pragma once
#include <stdint.h>
#include <stddef.h>
#include <aquaos/net_abi.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Gateway-only ICMP API. Numeric IPv4 encoded as 0xAABBCCDD.
 * Result: round-trip milliseconds or -1 with errno (EAGAIN while pending). */
int aquaos_ping_start(uint32_t ip);
int aquaos_ping_result(void);

/* Nonblocking UDP descriptors; close/dup/dup2/fork use normal fd semantics.
 * Bind before send/receive; port zero chooses an ephemeral port. Numeric IPs
 * are 0xAABBCCDD and ports are host integers. Payload maximum: 1472 bytes.
 * Send/receive return bytes or -1 with errno. ERANGE preserves the datagram.
 * Empty datagrams return zero. Source output is required for receive. */
int aquaos_udp_socket(void);
int aquaos_udp_bind(int fd, uint16_t port);
int aquaos_udp_sendto(int fd, const void *data, size_t length, uint32_t ip, uint16_t port);
int aquaos_udp_recvfrom(int fd, void *data, size_t capacity, aquaos_udp_address *source);

#ifdef __cplusplus
}
#endif
