[BITS 16]           ; We're in 16-bit mode
[ORG 0x7C00]        ; BIOS loads us at this address

mov [boot_drive], dl ; Save the BIOS boot drive

; Set up segments
cli                 ; Disable interrupts
mov ax, 0x0000      ; Set up segments
mov ds, ax
mov es, ax
mov ss, ax
mov sp, 0x7C00      ; Set up stack pointer
sti                 ; Enable interrupts

; Print boot message
mov si, boot_msg
call print_string

; Load kernel (64 sectors = 32KB)
mov ah, 0x02        ; BIOS read sector function
mov al, 64          ; Number of sectors to read
mov ch, 0           ; Cylinder 0
mov cl, 2           ; Start at Sector 2
mov dh, 0           ; Head 0
mov dl, [boot_drive]; Use the saved boot drive
mov bx, 0x1000      ; Load address
int 0x13            ; Call BIOS interrupt
jc disk_error       ; Jump if carry flag set (error)

; Switch to 32-bit protected mode
cli                 ; Disable interrupts
lgdt [gdt_descriptor] ; Load GDT

; Enable A20 line
in al, 0x92
or al, 2
out 0x92, al

; Set PE bit in CR0
mov eax, cr0
or eax, 1
mov cr0, eax

; Jump to 32-bit code
jmp 0x08:protected_mode

disk_error:
    mov si, disk_error_msg
    call print_string
    jmp $           ; Infinite loop

print_string:
    mov ah, 0x0E    ; BIOS teletype function
.loop:
    lodsb           ; Load byte from SI into AL
    test al, al     ; Check if end of string
    jz .done        ; If zero, we're done
    int 0x10        ; Print character
    jmp .loop
.done:
    ret

[BITS 32]
protected_mode:
    ; Set up segment registers
    mov ax, 0x10    ; Data segment selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Jump to kernel
    jmp 0x1000

; Data
boot_drive      db 0
boot_msg        db 'Booting SentinelOS...', 0x0D, 0x0A, 0
disk_error_msg  db 'Disk read error!', 0x0D, 0x0A, 0

; GDT (Global Descriptor Table)
gdt_start:
    ; Null descriptor
    dq 0
gdt_code: ; Code segment descriptor
    dw 0xFFFF  ; Limit (bits 0-15)
    dw 0       ; Base (bits 0-15)
    db 0       ; Base (bits 16-23)
    db 0x9A    ; Access byte (Read/Execute)
    db 0xCF    ; Granularity
    db 0       ; Base (bits 24-31)
gdt_data: ; Data segment descriptor
    dw 0xFFFF  ; Limit (bits 0-15)
    dw 0       ; Base (bits 0-15)
    db 0       ; Base (bits 16-23)
    db 0x92    ; Access byte (Read/Write)
    db 0xCF    ; Granularity
    db 0       ; Base (bits 24-31)
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1 ; GDT limit
    dd gdt_start               ; GDT base address

times 510-($-$$) db 0   ; Pad to 510 bytes
dw 0xAA55               ; Boot signature 