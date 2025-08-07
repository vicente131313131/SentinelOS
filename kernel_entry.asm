[BITS 32]
[EXTERN kernel_main]

; Multiboot2 info is in ebx, save it.
mov [multiboot_info_ptr], ebx

; Set up stack for 32-bit mode
mov esp, 0x90000

; --- Setup Long Mode ---
; Check for long mode support
mov eax, 0x80000000
cpuid
cmp eax, 0x80000001
jb .no_long_mode

mov eax, 0x80000001
cpuid
test edx, 1 << 29
jz .no_long_mode

; --- Setup Page Tables to map first 4GB ---
; We will identity map the first 4GB using 2MB huge pages.
; This requires one PML4, one PDPT, and four PDTs.
; PML4 -> 0x1000
; PDPT -> 0x2000
; PDTs -> 0x3000, 0x4000, 0x5000, 0x6000

; Clear page structures (6 pages total)
mov edi, 0x1000
mov ecx, 4096 * 6
xor eax, eax
rep stosb

; Set up PML4 entry to point to PDPT
mov eax, 0x2000 | 3 ; Present, R/W
mov [0x1000], eax

; Set up the PDPT to point to the 4 PDTs
mov edi, 0x2000
mov ecx, 4
mov eax, 0x3000 | 3 ; Address of first PDT
.map_pdpt:
    mov [edi], eax
    add eax, 0x1000 ; Next PDT is 4KB higher
    add edi, 8
    loop .map_pdpt

; Fill the 4 PDTs to map 4GB (4 * 512 * 2MB pages)
mov edi, 0x3000
mov ecx, 512 * 4 ; 2048 entries total
mov eax, 0x00000083 ; Present, R/W, Huge Page (PS=1)
.map_pdt:
    mov [edi], eax
    add eax, 0x200000 ; Next 2MB block
    add edi, 8
    loop .map_pdt

; Move page table base to CR3
mov eax, 0x1000
mov cr3, eax

; Enable PAE
mov eax, cr4
or eax, 1 << 5
mov cr4, eax

; Enable Long Mode
mov ecx, 0xC0000080
rdmsr
or eax, 1 << 8
wrmsr

; Enable Paging
mov eax, cr0
or eax, 1 << 31
mov cr0, eax

lgdt [gdt64.pointer]
jmp 0x08:.long_mode_start

.no_long_mode:
    mov eax, 0xb8000
    mov byte [eax], 'E'
    mov byte [eax+1], 0x4F
    hlt

[BITS 64]
.long_mode_start:
    ; Update segment registers for 64-bit mode
    mov ax, 0x10 ; Data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Initialize FPU/SSE
    mov rax, cr0
    and ax, 0xFFFB      ; Clear EM bit (no emulation)
    or ax, 0x2          ; Set MP bit (monitor coprocessor)
    mov cr0, rax
    mov rax, cr4
    or ax, 0x600        ; Set OSFXSR and OSXMMEXCPT bits
    mov cr4, rax
    fninit              ; Initialize FPU

    ; Set up stack
    mov rsp, 0x8F000

    ; Pass Multiboot info to kernel_main
    mov rdi, [multiboot_info_ptr]
    call kernel_main

    ; Halt if kernel_main returns
    cli
.hlt_loop:
    hlt
    jmp .hlt_loop

section .rodata
align 8
gdt64:
    .null: dq 0
    .code: dq 0x00209A0000000000
    .data: dq 0x0000920000000000
.pointer:
    dw .pointer - gdt64 - 1
    dq gdt64

section .bss
align 4
multiboot_info_ptr:
    resd 1 