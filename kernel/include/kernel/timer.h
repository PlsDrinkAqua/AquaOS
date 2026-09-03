#ifndef _KERNEL_TIMER_H
#define _KERNEL_TIMER_H

#include <stdint.h>
#include <kernel/interrupt_frame.h>

void timer_init(uint32_t frequency);
void timer_isr(interrupt_frame_t *regs);
uint32_t timer_ticks(void);

#endif
