section .multiboot2_header
align 8
multiboot2_header:
    dd 0xE85250D6              ; Multiboot2 magic
    dd 0                        ; Architecture (i386)
    dd multiboot2_header_end - multiboot2_header ; Header length
    dd 0x100000000 - (0xE85250D6 + 0 + (multiboot2_header_end - multiboot2_header)) ; Checksum

    ; End tag
    dw 0                        ; Type
    dw 0                        ; Flags
    dd 8                        ; Size
multiboot2_header_end: 