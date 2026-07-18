[bits 64]

section .text
global _start

_start:
    cli

    ; Load GDT using RIP-relative address
    lgdt [rel gdt64_ptr]

    ; Reload CS using RIP-relative address far return
    push qword 0x08
    lea rax, [rel .reload_cs]
    push rax
    retfq

.reload_cs:
    ; Reload other segment registers with 0x10
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Invoke kernel_main
    extern kernel_main
    call kernel_main

    ; Hang if kernel returns
.halt_loop:
    cli
    hlt
    jmp .halt_loop

section .rodata
align 8
gdt64:
    ; Null descriptor
    dq 0
    ; Code descriptor (0x08): L=1, D=0, Access=0x9A (64-bit code)
    dq 0x00AF9A0000000000
    ; Data descriptor (0x10): L=0, Access=0x92 (64-bit data)
    dq 0x0000920000000000

gdt64_ptr:
    dw $ - gdt64 - 1
    dq gdt64
