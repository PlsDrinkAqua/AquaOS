#pragma once

/* Boot-time initialization of MMIO and receive/transmit DMA rings. */
void e1000_init(void);
/* Reclaim TX and poll RX; called with interrupts disabled by the timer ISR. */
void e1000_poll(void);

#include <stddef.h>
#define E1000_FRAME_MAX 1514U
/* Kernel buffers only. Copies an Ethernet frame without FCS; pads to 60 bytes.
 * Nonblocking: 0 means queued, -K_EAGAIN means full, -K_ENODEV means unavailable,
 * -K_EINVAL means invalid buffer/length. Caller may reuse its buffer on return.
 * Serialized with interrupt masking on the single-CPU kernel. */
int e1000_transmit(const void *frame, size_t length);

/* Current hardware link status; safe before initialization. */
int e1000_link_up(void);
