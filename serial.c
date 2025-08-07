#include "serial.h"
#include <stdint.h>
#include "io.h"

#define COM1_PORT 0x3F8

// The outb and inb functions are now in io.h

void serial_init() {
    outb(COM1_PORT + 1, 0x00);    // Disable all interrupts
    outb(COM1_PORT + 3, 0x80);    // Enable DLAB (set baud rate divisor)
    outb(COM1_PORT + 0, 0x03);    // Set divisor to 3 (lo byte) 38400 baud
    outb(COM1_PORT + 1, 0x00);    //                  (hi byte)
    outb(COM1_PORT + 3, 0x03);    // 8 bits, no parity, one stop bit
    outb(COM1_PORT + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
    outb(COM1_PORT + 4, 0x0B);    // IRQs enabled, RTS/DSR set
}

int serial_is_transmit_empty() {
    return inb(COM1_PORT + 5) & 0x20;
}

void serial_write(char c) {
    while (!serial_is_transmit_empty());
    outb(COM1_PORT, c);
}

void serial_writestring(const char* str) {
    for (int i = 0; str[i] != '\0'; i++) {
        serial_write(str[i]);
    }
}

void serial_writehex(uint64_t n) {
    char buffer[17];
    char* hex_chars = "0123456789abcdef";
    buffer[16] = '\0';
    int i = 15;

    if (n == 0) {
        serial_writestring("0x0");
        return;
    }

    serial_writestring("0x");
    while (n > 0 && i >= 0) {
        buffer[i--] = hex_chars[n % 16];
        n /= 16;
    }
    serial_writestring(&buffer[i + 1]);
} 