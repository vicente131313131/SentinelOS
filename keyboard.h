#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>
#include "isr.h"

// Special keys
#define KEY_UP      0x80
#define KEY_DOWN    0x81
#define KEY_LEFT    0x82
#define KEY_RIGHT   0x83

// Extended editing keys
#define KEY_SEL_LEFT   0x84
#define KEY_SEL_RIGHT  0x85
#define KEY_COPY       0x86
#define KEY_PASTE      0x87

void keyboard_init(void);
void keyboard_handle_scancode(uint8_t scancode);
void keyboard_handler(registers* regs);

#endif // KEYBOARD_H 