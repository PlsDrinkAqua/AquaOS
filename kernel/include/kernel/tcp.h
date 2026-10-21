#pragma once
#include <stddef.h>
#include <stdint.h>
/* Single-CPU, IRQs disabled. Bounded active-open TCP, no listeners. */
void net_tcp_init(void);
void net_tcp_poll(uint32_t now);
void net_tcp_input(const uint8_t *ip, unsigned header, unsigned total);
int net_tcp_socket(void);
int net_tcp_connect(int handle, uint32_t ip, uint16_t port);
int net_tcp_status(int handle);
int net_tcp_send(int handle, const void *data, size_t length);
int net_tcp_read(int handle, void *data, size_t capacity);
int net_tcp_shutdown(int handle);
void net_tcp_release(int handle);
/* Shared IPv4 egress; resolves next hop, never retains the payload. */
int net_ipv4_send(uint32_t destination, unsigned protocol, const void *payload, size_t length);
