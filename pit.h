#include "isr.h"
#include <stdint.h>

#ifndef PIT_H
#define PIT_H

void pit_init(uint32_t frequency);
void pit_tick();
uint64_t pit_get_ticks();
void pit_handler(registers* regs);

#endif 