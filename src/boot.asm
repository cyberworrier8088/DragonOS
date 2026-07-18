; boot.asm
; Multiboot 1 header and 64-bit long mode switch with 4 GiB mapping & VBE graphics

MBALIGN  equ  1 << 0            ; align loaded modules on page boundaries
MEMINFO  equ  1 << 1            ; provide memory map
GRAPHICS equ  1 << 2            ; request graphics mode
FLAGS    equ  MBALIGN | MEMINFO | GRAPHICS
MAGIC    equ  0x1BADB002        ; 'magic number'
CHECKSUM equ -(MAGIC + FLAGS)   ; checksum

section .multiboot
align 4
    dd MAGIC
    dd FLAGS
    dd CHECKSUM
    dd 0    ; mode_type: 0 = linear graphics
    dd 800  ; width: 800 pixels
    dd 600  ; height: 600 pixels
    dd 32   ; depth: 32 bits per pixel

section .bss
align 4096
pml4:
    resb 4096
pdpt:
    resb 4096
pd_tables:
    resb 4096 * 4  ; Allocate space for 4 page directories (covers 4 GiB)

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

    ; Immediately preserve Multiboot magic and info pointer in registers
    ; EDI and ESI are not modified during the long mode transition.
    ; This sets up RDI and RSI automatically for the System V ABI!
    mov edi, eax        ; EDI = magic
    mov esi, ebx        ; ESI = info pointer

    ; 1. Set up page tables for 4 GiB identity map
    ; PML4[0] -> PDPT
    mov eax, pdpt
    or eax, 0x3     ; Present + Read/Write
    mov [pml4], eax

    ; PDPT[0], [1], [2], [3] point to the 4 page directories in pd_tables
    mov eax, pd_tables
    or eax, 0x3     ; Present + Read/Write
    mov [pdpt], eax

    mov eax, pd_tables + 4096
    or eax, 0x3
    mov [pdpt + 8], eax

    mov eax, pd_tables + 8192
    or eax, 0x3
    mov [pdpt + 16], eax

    mov eax, pd_tables + 12288
    or eax, 0x3
    mov [pdpt + 24], eax

    ; Populate all 2048 entries (4 directories * 512 entries) in pd_tables
    mov ecx, 0      ; entry index from 0 to 2047
    mov edx, 0x83   ; first entry value (base address 0 | Huge Page | Present | R/W)
.loop_pd:
    mov [pd_tables + ecx*8], edx
    add edx, 0x200000   ; increment physical address by 2 MiB
    inc ecx
    cmp ecx, 2048
    jne .loop_pd

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
    ; RDI (magic) and RSI (info pointer) are already set up from _start!
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
