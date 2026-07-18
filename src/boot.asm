; boot.asm
; Multiboot 1 header and kernel entry point

; Declare constants for the Multiboot header.
MBALIGN  equ  1 << 0            ; align loaded modules on page boundaries
MEMINFO  equ  1 << 1            ; provide memory map
FLAGS    equ  MBALIGN | MEMINFO ; this is the Multiboot 'flag' field
MAGIC    equ  0x1BADB002        ; 'magic number' lets bootloader find the header
CHECKSUM equ -(MAGIC + FLAGS)   ; checksum of above, to prove we are multiboot

; Declare the multiboot header section. It must be aligned on a 32-bit boundary.
section .multiboot
align 4
    dd MAGIC
    dd FLAGS
    dd CHECKSUM

; Allocate the stack space for the kernel.
section .bss
align 16
stack_bottom:
    resb 16384 ; Allocate 16 KiB of stack space
stack_top:

; Entry point of our operating system.
section .text
global _start
_start:
    ; 1. Set up the stack pointer.
    mov esp, stack_top

    ; 2. Call the C kernel main function.
    extern kernel_main
    call kernel_main

    ; 3. If kernel_main returns, disable interrupts and enter a halt loop.
    cli
.hang:
    hlt
    jmp .hang
