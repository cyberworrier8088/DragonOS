; boot.asm
; Multiboot 1 header and 64-bit long mode switch

MBALIGN  equ  1 << 0            ; align loaded modules on page boundaries
MEMINFO  equ  1 << 1            ; provide memory map
FLAGS    equ  MBALIGN | MEMINFO ; this is the Multiboot 'flag' field
MAGIC    equ  0x1BADB002        ; 'magic number'
CHECKSUM equ -(MAGIC + FLAGS)   ; checksum

section .multiboot
align 4
    dd MAGIC
    dd FLAGS
    dd CHECKSUM

section .bss
align 4096
pml4:
    resb 4096
pdpt:
    resb 4096
pd:
    resb 4096

align 16
stack_bottom:
    resb 16384 ; Allocate 16 KiB of stack space
stack_top:

section .text
[bits 32]
global _start
_start:
    ; Disable interrupts
    cli

    ; 1. Set up page tables for 2 MiB identity map
    ; PML4[0] -> PDPT
    mov eax, pdpt
    or eax, 0x3     ; Present + Read/Write
    mov [pml4], eax

    ; PDPT[0] -> PD
    mov eax, pd
    or eax, 0x3     ; Present + Read/Write
    mov [pdpt], eax

    ; PD[0] -> Huge Page (2 MiB) at physical address 0
    mov eax, 0x83   ; Present + Read/Write + Huge Page (bit 7)
    mov [pd], eax

    ; 2. Load PML4 into CR3
    mov eax, pml4
    mov cr3, eax

    ; 3. Enable PAE in CR4
    mov eax, cr4
    or eax, 1 << 5  ; Set PAE bit
    mov cr4, eax

    ; 4. Enable Long Mode in EFER MSR (0xC0000080)
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8  ; Set LME bit
    wrmsr

    ; 5. Enable Paging in CR0
    mov eax, cr0
    or eax, 1 << 31 ; Set PG bit
    mov cr0, eax

    ; 6. Load 64-bit GDT
    lgdt [gdt64_ptr]

    ; 7. Far jump to 64-bit segment
    jmp 0x08:long_mode_start

[bits 64]
long_mode_start:
    ; Set segment registers to 0x10 (64-bit Data Segment)
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Set up the 64-bit stack pointer
    mov rsp, stack_top

    ; Call the C kernel main function
    extern kernel_main
    call kernel_main

    ; Hang if returns
    cli
.hang:
    hlt
    jmp .hang

section .rodata
align 8
gdt64:
    ; Null descriptor (index 0)
    dq 0
    ; Code descriptor (index 1, offset 0x08): L=1, D=0, Access=0x9A
    dq 0x00AF9A0000000000
    ; Data descriptor (index 2, offset 0x10): L=0, Access=0x92
    dq 0x0000920000000000
gdt64_ptr:
    dw $ - gdt64 - 1
    dq gdt64
