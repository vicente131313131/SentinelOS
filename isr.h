#ifndef ISR_H
#define ISR_H

#include <stdint.h>

#define IRQ10 42
#define IRQ11 43
#define IRQ12 44
#define IRQ13 45
#define IRQ14 46
#define IRQ15 47

typedef struct {
    // Registers pushed by isr_common_stub
    uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax;
    
    // Pushed by ISR macro
    uint64_t int_no, err_code;
    
    // Pushed by the CPU automatically on interrupt
    uint64_t rip, cs, rflags, userrsp, ss;
} registers;

void isr_install();
void isr_handler_c(registers regs);
void page_fault_handler(registers* regs);
void register_interrupt_handler(uint8_t n, void (*handler)(registers*));

#endif 