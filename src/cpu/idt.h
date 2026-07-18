#ifndef IDT_H
#define IDT_H

#include <stdint.h>

/* Structure defining an IDT gate descriptor. */
struct idt_entry_struct {
    uint16_t base_lo;             // The lower 16 bits of the ISR address
    uint16_t sel;                 // Kernel segment selector (0x08)
    uint8_t  always0;             // Reserved, always zero
    uint8_t  flags;               // Gate flags (type, privilege levels, present bit)
    uint16_t base_hi;             // The upper 16 bits of the ISR address
} __attribute__((packed));
typedef struct idt_entry_struct idt_entry_t;

/* Structure pointing to our array of descriptors, loaded into IDTR. */
struct idt_ptr_struct {
    uint16_t limit;               // Number of descriptors minus 1
    uint32_t base;                // Base address of the array
} __attribute__((packed));
typedef struct idt_ptr_struct idt_ptr_t;

/* Register frame layout pushed onto the stack during interrupts. */
struct registers {
    uint32_t ds;                                      // Data segment selector saved manually
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;  // Registers saved by pusha
    uint32_t int_no, err_code;                        // Interrupt vector number and optional error code
    uint32_t eip, cs, eflags, useresp, ss;            // Saved automatically by the CPU
};
typedef struct registers registers_t;

/* Type for handling callbacks. */
typedef void (*isr_t)(registers_t*);

void idt_init(void);
void register_interrupt_handler(uint8_t n, isr_t handler);

#endif
