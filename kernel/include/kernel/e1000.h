#pragma once

/* Boot-time initialization of MMIO and the receive DMA ring. */
void e1000_init(void);
/* Single consumer, called with interrupts disabled by the timer ISR. */
void e1000_poll(void);
