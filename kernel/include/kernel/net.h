#pragma once
#include <stddef.h>
#include <stdint.h>

/* Single interface, static QEMU network. All calls run with IRQs disabled.
 * Receive buffers are borrowed only for the duration of net_receive(). */
void net_init(const uint8_t mac[6], uint32_t now);
void net_receive(const uint8_t *frame, size_t length);
void net_poll(uint32_t now);
/* Copy the resolved gateway MAC; returns 1 if available, otherwise 0. */
int net_gateway_mac(uint8_t out[6]);
