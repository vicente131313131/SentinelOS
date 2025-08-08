/* idt.c – Set up and load the Interrupt Descriptor Table (IDT) */
#include "idt.h"
#include "serial.h"
#include <stddef.h>

struct idt_entry idt[IDT_ENTRIES];
struct idt_ptr idtp;

extern void idt_load(uint64_t);

/* Populate one IDT entry with a 64-bit handler and attributes. */
void idt_set_gate(int n, uint64_t handler, uint16_t selector, uint8_t type_attr) {
    idt[n].offset_low = handler & 0xFFFF;
    idt[n].selector = selector;
    idt[n].ist = 0;
    idt[n].type_attr = type_attr;
    idt[n].offset_mid = (handler >> 16) & 0xFFFF;
    idt[n].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt[n].zero = 0;
}

/* Load the IDT using lidt. */
void idt_install() {
    idtp.limit = (sizeof(struct idt_entry) * IDT_ENTRIES) - 1;
    idtp.base = (uint64_t)&idt;
    idt_load((uint64_t)&idtp);
    serial_writestring("[Serial] IDT loaded\n");
} 