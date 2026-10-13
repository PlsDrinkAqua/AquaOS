#ifndef _IO_H
#define _IO_H

#include <stdint.h>

// Since they are all static inline functions, we need to declare and define them in the same header file
static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %w1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint16_t inw(uint16_t port) {
    uint16_t value;
    __asm__ volatile (
        "inw %w1, %0"
        : "=a"(value)
        : "Nd"(port)
    );
    return value;
}

static inline void outw(uint16_t port, uint16_t value) {
    __asm__ volatile (
        "outw %0, %w1"
        :
        : "a"(value), "Nd"(port)
    );
}


static inline uint32_t inl(uint16_t port) {
    uint32_t value;
    __asm__ volatile ("inl %w1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outl(uint16_t port, uint32_t value) {
    __asm__ volatile ("outl %0, %w1" : : "a"(value), "Nd"(port));
}

static inline void io_wait(void)
{
    outb(0x80, 0);
}

#endif