section .multiboot2_header
align 8
multiboot2_header:
    dd 0xE85250D6              ; Multiboot2 magic
    dd 0                        ; Architecture (i386)
    dd multiboot2_header_end - multiboot2_header ; Header length
    dd 0x100000000 - (0xE85250D6 + 0 + (multiboot2_header_end - multiboot2_header)) ; Checksum

    ; Request a linear framebuffer (any resolution), 32 bpp
    dw 5                        ; Type: Framebuffer
    dw 0                        ; Flags
    dd 24                       ; Size of this tag (must be 8-byte aligned)
    dd 0                        ; Width  (0 = any)
    dd 0                        ; Height (0 = any)
    dd 32                       ; Depth  (bits per pixel)
    dd 0                        ; Padding to 8-byte alignment

    ; Request VBE information (optional)
    dw 7                        ; Type: VBE info request
    dw 0                        ; Flags
    dd 8                        ; Size

    ; End tag
    dw 0                        ; Type
    dw 0                        ; Flags
    dd 8                        ; Size
multiboot2_header_end: 