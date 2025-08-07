ENTRY(_start)
OUTPUT_FORMAT("elf64-x86-64")
SECTIONS
{
    . = 0x100000;

    .multiboot2_header :
    {
        *(.multiboot2_header)
    }

    .text :
    {
        *(.text)
    }

    .rodata :
    {
        *(.rodata)
    }

    .data :
    {
        *(.data)
    }

    .bss :
    {
        *(.bss)
    }

    . = ALIGN(4096);
    _kernel_end = .;
} 