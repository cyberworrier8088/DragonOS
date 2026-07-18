#ifndef IDT_H
#define IDT_H

#include <stdint.h>

/* Structure defining a 64-bit IDT gate descriptor. */
struct idt_entry_struct {
    uint16_t base_lo;             // Offset bits 0-15
    uint16_t sel;                 // Kernel segment selector (0x08)
    uint8_t  ist;                 // Interrupt Stack Table offset, unused
    uint8_t  flags;               // Gate flags (0x8E for 64-bit Interrupt Gate)
    uint16_t base_mid;            // Offset bits 16-31
    uint32_t base_hi;             // Offset bits 32-63
    uint32_t always0;             // Reserved, always zero
} __attribute__((packed));
typedef struct idt_entry_struct idt_entry_t;

/* Structure pointing to the array of 64-bit descriptors. */
struct idt_ptr_struct {
    uint16_t limit;               // Limit (size - 1)
    uint64_t base;                // 64-bit base address
} __attribute__((packed));
typedef struct idt_ptr_struct idt_ptr_t;

/* 64-bit register frame pushed onto the stack during interrupts. */
struct registers {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags, rsp, ss;
};
typedef struct registers registers_t;

/* Type for handling callbacks. */
typedef void (*isr_t)(registers_t*);

void idt_init(void);
void register_interrupt_handler(uint8_t n, isr_t handler);

#endif
