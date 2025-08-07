#include "keyboard.h"
#include "io.h"
#include <stdint.h>
#include <stddef.h>
#include "serial.h"


void shell_input_char(char c);

static bool e0_prefix = false;

// US QWERTY scancode to ASCII table (partial, for demo)
static const char scancode_ascii[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n', 0,
    'a','s','d','f','g','h','j','k','l',';','\'','`', 0,'\\','z','x','c','v','b','n','m',',','.','/', 0,
    '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

void keyboard_handle_scancode(uint8_t scancode) {


    if (scancode == 0xE0) {
        e0_prefix = true;
        return;
    }
    
    if (e0_prefix) {
        e0_prefix = false;
        switch (scancode) {
            case 0x48: shell_input_char(KEY_UP); break;
            case 0x50: shell_input_char(KEY_DOWN); break;
            case 0x4B: shell_input_char(KEY_LEFT); break;
            case 0x4D: shell_input_char(KEY_RIGHT); break;
        }
        return;
    }
    
    if (scancode & 0x80) { // Key release
        return;
    }
    char c = scancode_ascii[scancode];
    if (c) {
        shell_input_char(c);
    }
}

void keyboard_handler(registers* regs) {
    (void)regs;
    uint8_t scancode = inb(0x60);
    keyboard_handle_scancode(scancode);
}

void keyboard_init() {
    // Clear the keyboard buffer by reading from the data port until it's empty.
    // This discards any keypresses that happened during boot.
    while (inb(0x64) & 1) {
        inb(0x60);
    }
    serial_writestring("[Serial] Keyboard buffer cleared.\n");
} 