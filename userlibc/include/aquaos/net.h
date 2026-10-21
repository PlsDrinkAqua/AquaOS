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

/* Nonblocking TCP active-open descriptors. Connect starts the handshake;
 * status returns 0 when connected, -1/EAGAIN while pending, or terminal errno.
 * read/write return stream bytes, EAGAIN while waiting, read 0 at peer FIN.
 * shutdown queues FIN after pending data; last close releases ownership.
 * close/dup/fork/exec use normal descriptor semantics. */
int aquaos_tcp_socket(void);
int aquaos_tcp_connect(int fd, uint32_t ip, uint16_t port);
int aquaos_tcp_status(int fd);
int aquaos_tcp_shutdown(int fd);

#ifdef __cplusplus
}
#endif
