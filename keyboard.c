#include "keyboard.h"
#include "io.h"
#include <stdint.h>
#include <stddef.h>
#include "serial.h"


void shell_input_char(char c);

static bool e0_prefix = false;
static bool shift_held = false;
static bool ctrl_held = false;
static bool gui_held = false; // macOS Command key maps to GUI in PS/2

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
            case 0x4B: shell_input_char(shift_held ? KEY_SEL_LEFT : KEY_LEFT); break;
            case 0x4D: shell_input_char(shift_held ? KEY_SEL_RIGHT : KEY_RIGHT); break;
            case 0x52: // Insert
                if (shift_held) { shell_input_char(KEY_PASTE); }
                else if (ctrl_held) { shell_input_char(KEY_COPY); }
                break;
            case 0x5B: // Left GUI (Command)
            case 0x5C: // Right GUI (Command)
                gui_held = true; return;
            case 0xDB: // Left GUI release
            case 0xDC: // Right GUI release
                gui_held = false; return;
        }
        return;
    }
    
    // Handle modifier press/release (make codes: LShift=0x2A, RShift=0x36, Ctrl=0x1D)
    if (scancode == 0x2A || scancode == 0x36) { shift_held = true; return; }
    if (scancode == (0x2A | 0x80) || scancode == (0x36 | 0x80)) { shift_held = false; return; }
    if (scancode == 0x1D) { ctrl_held = true; return; }
    if (scancode == (0x1D | 0x80)) { ctrl_held = false; return; }

    if (scancode & 0x80) { // Key release (others)
        return;
    }
    char c = scancode_ascii[scancode];
    if (c) {
        // Ctrl+C / Ctrl+V mapping
        if (ctrl_held && (c == 'c' || c == 'C')) { shell_input_char(KEY_COPY); return; }
        if (ctrl_held && (c == 'v' || c == 'V')) { shell_input_char(KEY_PASTE); return; }
        // GUI(Command)+C/V mapping for macOS keyboards
        if (gui_held && (c == 'c' || c == 'C')) { shell_input_char(KEY_COPY); return; }
        if (gui_held && (c == 'v' || c == 'V')) { shell_input_char(KEY_PASTE); return; }
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