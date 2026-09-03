
#include <stdint.h>
#include <kernel/io.h>
#include <kernel/interrupt_frame.h>
#include <kernel/task.h>
#include <kernel/timer.h>

#define PIT_COMMAND_PORT 0x43
#define PIT_CHANNEL0_PORT 0x40
#define PIT_BASE_FREQUENCY 1193182U
#define TIMER_QUANTUM_TICKS 5U

static uint32_t ticks;

void timer_init(uint32_t frequency) {
    if (frequency == 0) {
        frequency = 100;
    }

    uint32_t divisor = PIT_BASE_FREQUENCY / frequency;
    if (divisor == 0) {
        divisor = 1;
    }
    if (divisor > 0xFFFF) {
        divisor = 0xFFFF;
    }

    outb(PIT_COMMAND_PORT, 0x36);
    outb(PIT_CHANNEL0_PORT, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0_PORT, (uint8_t)((divisor >> 8) & 0xFF));
}

uint32_t timer_ticks(void) {
    return ticks;
}

void timer_isr(interrupt_frame_t *regs) {
    ticks++;
    task_wake_sleepers(ticks);

    if ((ticks % TIMER_QUANTUM_TICKS) == 0) {
        task_preempt_current(regs);
    }
}
