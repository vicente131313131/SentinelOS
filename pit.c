#include "pit.h"
#include <stdint.h>
#include "serial.h"

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43

static volatile uint64_t pit_ticks = 0;

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

void pit_init(uint32_t frequency) {
    if (frequency == 0) {
        serial_writestring("[Serial] pit_init: frequency is zero!\n");
        frequency = 1;
    }
    uint32_t divisor = 1193180 / frequency;
    outb(PIT_COMMAND, 0x36); // Command: channel 0, lobyte/hibyte, mode 3, binary
    outb(PIT_CHANNEL0, divisor & 0xFF); // Low byte
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF); // High byte
    pit_ticks = 0;
}

void pit_tick() {
    pit_ticks++;
}

void pit_handler(registers* regs) {
    (void)regs; // Unused
    pit_tick();
}

uint64_t pit_get_ticks() {
    return pit_ticks;
} 