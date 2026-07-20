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

    ; Switch onto our own large stack. The bootloader-provided stack is not
    ; guaranteed to be large enough for deeply nested C call chains (ported
    ; game engines in particular routinely assume a >=1MB OS stack), so we
    ; can't rely on whatever Limine handed us.
    lea rsp, [rel kernel_stack_top]

    ; Enable SSE before ANY C code runs: the entire kernel is compiled with
    ; SSE so that every function uses the standard x86_64 float ABI. Mixing
    ; -mno-sse and -msse objects passed doubles through different locations
    ; (stack vs XMM0), which silently fed garbage into game timing code.
    mov rax, cr0
    and rax, ~(1 << 2)      ; clear EM (no x87 emulation)
    or  rax, (1 << 1)       ; set MP (monitor coprocessor)
    mov cr0, rax
    mov rax, cr4
    or  rax, (3 << 9)       ; set OSFXSR | OSXMMEXCPT
    mov cr4, rax

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

section .bss
align 16
kernel_stack_bottom:
    resb 0x400000   ; 4MB kernel stack
kernel_stack_top:
