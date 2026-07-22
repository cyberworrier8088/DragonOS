[bits 64]

global idt_flush
idt_flush:
    lidt [rdi]      ; In System V AMD64 ABI, first parameter is in RDI
    ret

; Both stubs must preserve the FULL CPU state, including x87/SSE registers.
; Interrupt handlers call into SSE-compiled code (the game keyboard hooks,
; libc math), so without fxsave/fxrstor an IRQ arriving mid-float-operation
; silently corrupts the interrupted context's XMM registers. That manifested
; as random game-state corruption on every keypress.

extern isr_handler
isr_common_stub:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp    ; Pass pointer to stack frame (registers_t*) in RDI
    mov rbx, rsp    ; Remember GP-save block (rbx is restored by pops below)
    sub rsp, 528    ; 512-byte FXSAVE area + alignment slack
    and rsp, -16    ; FXSAVE requires 16-byte alignment; also aligns the call
    fxsave [rsp]

    call isr_handler

    fxrstor [rsp]
    mov rsp, rax    ; isr_handler returns the frame to resume (may be a new task)

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16     ; Clean up pushed error code and ISR vector
    iretq           ; 64-bit interrupt return

extern irq_handler
irq_common_stub:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp    ; Pass pointer to stack frame (registers_t*) in RDI
    mov rbx, rsp    ; Remember GP-save block (rbx is restored by pops below)
    sub rsp, 528    ; 512-byte FXSAVE area + alignment slack
    and rsp, -16    ; FXSAVE requires 16-byte alignment; also aligns the call
    fxsave [rsp]

    call irq_handler

    fxrstor [rsp]
    mov rsp, rax    ; Switch stack pointer to target task registers_t* frame

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16     ; Clean up pushed error code and IRQ vector
    iretq           ; 64-bit interrupt return

%macro ISR_NOERRCODE 1
  global isr%1
  isr%1:
    push qword 0
    push qword %1
    jmp isr_common_stub
%endmacro

%macro ISR_ERRCODE 1
  global isr%1
  isr%1:
    push qword %1
    jmp isr_common_stub
%endmacro

ISR_NOERRCODE 128


%macro IRQ 2
  global irq%1
  irq%1:
    push qword 0
    push qword %2
    jmp irq_common_stub
%endmacro

ISR_NOERRCODE 0
ISR_NOERRCODE 1
ISR_NOERRCODE 2
ISR_NOERRCODE 3
ISR_NOERRCODE 4
ISR_NOERRCODE 5
ISR_NOERRCODE 6
ISR_NOERRCODE 7
ISR_ERRCODE   8
ISR_NOERRCODE 9
ISR_ERRCODE   10
ISR_ERRCODE   11
ISR_ERRCODE   12
ISR_ERRCODE   13
ISR_ERRCODE   14
ISR_NOERRCODE 15
ISR_NOERRCODE 16
ISR_NOERRCODE 17
ISR_NOERRCODE 18
ISR_NOERRCODE 19
ISR_NOERRCODE 20
ISR_NOERRCODE 21
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_NOERRCODE 29
ISR_NOERRCODE 30
ISR_NOERRCODE 31

IRQ 0, 32
IRQ 1, 33
IRQ 2, 34
IRQ 3, 35
IRQ 4, 36
IRQ 5, 37
IRQ 6, 38
IRQ 7, 39
IRQ 8, 40
IRQ 9, 41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47
