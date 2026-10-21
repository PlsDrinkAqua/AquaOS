#pragma once
#include <stddef.h>
#include <stdint.h>

/* Single interface, static QEMU network. All calls run with IRQs disabled.
 * Receive buffers are borrowed only for the duration of net_receive(). */
void net_init(const uint8_t mac[6], uint32_t now);
void net_receive(const uint8_t *frame, size_t length);
void net_poll(uint32_t now);
/* Resolve a unicast next hop. Local /24 peers use ARP; remote IPs use gateway.
 * Returns 0 and MAC, EAGAIN while resolving, ETIMEDOUT during failure cooldown,
 * ENOSPC if all eight peer slots are occupied. Retry send after EAGAIN. */
int net_resolve(uint32_t destination, uint8_t out[6]);
/* Copy the resolved gateway MAC; returns 1 if available, otherwise 0. */
int net_gateway_mac(uint8_t out[6]);

/* IP encoded as 0xAABBCCDD. Start returns 0; result returns RTT milliseconds,
 * -K_EAGAIN while pending, or an error. Reading a final result consumes it. */
int net_ping_start(uint32_t pid, uint32_t ip, uint32_t now);
int net_ping_result(uint32_t pid);
void net_ping_cancel(uint32_t pid);

/* Nonblocking kernel UDP API; IRQs must be disabled. IPs and ports are host
 * integers (IP 0xAABBCCDD). Eight bound ports, four queued datagrams per port.
 * Send returns 0 when queued. Receive returns bytes, EAGAIN when empty,
 * ERANGE without consuming when capacity is too small. Unbind discards queue.
 * Local peers and off-subnet unicast; no fragmentation. ARP misses return
 * EAGAIN without queuing the datagram. */
#define NET_UDP_MAX_PAYLOAD 1472U
int net_udp_bind(uint16_t port);
void net_udp_unbind(uint16_t port);
int net_udp_send(uint32_t destination, uint16_t source_port,
                 uint16_t destination_port, const void *data, size_t length);
int net_udp_receive(uint16_t port, void *data, size_t capacity,
                    uint32_t *source_ip, uint16_t *source_port);

#ifdef AQUAOS_UDP_ECHO_TEST_PORT
/* Disposable QEMU test builds only. */
void net_udp_echo_test_reset(void);
void net_udp_echo_test_poll(uint32_t now);
#endif
