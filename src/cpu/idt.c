#include "idt.h"
#include "ports.h"
#include "scheduler.h"
#include "../libc/string.h"
#include "../libc/stdlib.h"
#include "../drivers/serial.h"
#include "../shell/gui.h"

/* Game state flags and unwind targets used for fault isolation */
extern int quake_running;
extern int doom_running;
extern jmp_buf quake_exit_jmp;
extern jmp_buf doom_exit_jmp;

extern void idt_flush(uint64_t);

idt_entry_t idt_entries[256];
idt_ptr_t   idt_ptr;
isr_t       interrupt_handlers[256];

extern void isr128(void);

// Handle an int 0x80 system call. Returns the register frame to resume on --
// normally the caller's own frame, but sys_yield/sys_exit return a *different*
// task's frame so the ISR stub switches to it via `mov rsp, rax`.
static registers_t* syscall_dispatch(registers_t* r) {
    switch (r->rax) {
        case 1: // sys_print(rbx = message)
            print_serial((const char*)r->rbx);
            return r;
        case 2: // sys_yield -- hand the CPU to the next runnable task right now
            return schedule(r);
        case 3: // sys_exit -- retire this task, then switch away for good
            task_exit();
            return schedule(r);
        case 4: { // sys_getpid
            task_t* cur = get_current_task();
            r->rax = cur ? cur->pid : 0;
            return r;
        }
        default:
            return r;
    }
}

static void idt_set_gate(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags) {
    idt_entries[num].base_lo  = base & 0xFFFF;
    idt_entries[num].base_mid = (base >> 16) & 0xFFFF;
    idt_entries[num].base_hi  = (base >> 32) & 0xFFFFFFFF;
    idt_entries[num].sel      = sel;
    idt_entries[num].ist      = 0;
    idt_entries[num].flags    = flags;
    idt_entries[num].always0  = 0;
}

void idt_init(void) {
    idt_ptr.limit = sizeof(idt_entry_t) * 256 - 1;
    idt_ptr.base  = (uint64_t)&idt_entries;

    memset(&idt_entries, 0, sizeof(idt_entry_t) * 256);
    memset(&interrupt_handlers, 0, sizeof(isr_t) * 256);

    /* Remap PIC to avoid exception overlap */
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 0x20); // Master PIC mapping offset is 32 (0x20)
    outb(0xA1, 0x28); // Slave PIC mapping offset is 40 (0x28)
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    outb(0x21, 0x0);  // Unmask all hardware interrupts
    outb(0xA1, 0x0);

    /* Software Syscall Interrupt (0x80 = Vector 128, DPL=3 for User Mode access).
     * Dispatched directly by isr_handler (not via the generic handler table) so
     * it can return a switched-to task frame for sys_yield/sys_exit. */
    idt_set_gate(128, (uint64_t)isr128, 0x08, 0xEE);

    /* CPU Exceptions */
    extern void isr0();  extern void isr1();  extern void isr2();  extern void isr3();
    extern void isr4();  extern void isr5();  extern void isr6();  extern void isr7();
    extern void isr8();  extern void isr9();  extern void isr10(); extern void isr11();
    extern void isr12(); extern void isr13(); extern void isr14(); extern void isr15();
    extern void isr16(); extern void isr17(); extern void isr18(); extern void isr19();
    extern void isr20(); extern void isr21(); extern void isr22(); extern void isr23();
    extern void isr24(); extern void isr25(); extern void isr26(); extern void isr27();
    extern void isr28(); extern void isr29(); extern void isr30(); extern void isr31();

    idt_set_gate(0, (uint64_t)isr0, 0x08, 0x8E);   idt_set_gate(1, (uint64_t)isr1, 0x08, 0x8E);
    idt_set_gate(2, (uint64_t)isr2, 0x08, 0x8E);   idt_set_gate(3, (uint64_t)isr3, 0x08, 0x8E);
    idt_set_gate(4, (uint64_t)isr4, 0x08, 0x8E);   idt_set_gate(5, (uint64_t)isr5, 0x08, 0x8E);
    idt_set_gate(6, (uint64_t)isr6, 0x08, 0x8E);   idt_set_gate(7, (uint64_t)isr7, 0x08, 0x8E);
    idt_set_gate(8, (uint64_t)isr8, 0x08, 0x8E);   idt_set_gate(9, (uint64_t)isr9, 0x08, 0x8E);
    idt_set_gate(10, (uint64_t)isr10, 0x08, 0x8E); idt_set_gate(11, (uint64_t)isr11, 0x08, 0x8E);
    idt_set_gate(12, (uint64_t)isr12, 0x08, 0x8E); idt_set_gate(13, (uint64_t)isr13, 0x08, 0x8E);
    idt_set_gate(14, (uint64_t)isr14, 0x08, 0x8E); idt_set_gate(15, (uint64_t)isr15, 0x08, 0x8E);
    idt_set_gate(16, (uint64_t)isr16, 0x08, 0x8E); idt_set_gate(17, (uint64_t)isr17, 0x08, 0x8E);
    idt_set_gate(18, (uint64_t)isr18, 0x08, 0x8E); idt_set_gate(19, (uint64_t)isr19, 0x08, 0x8E);
    idt_set_gate(20, (uint64_t)isr20, 0x08, 0x8E); idt_set_gate(21, (uint64_t)isr21, 0x08, 0x8E);
    idt_set_gate(22, (uint64_t)isr22, 0x08, 0x8E); idt_set_gate(23, (uint64_t)isr23, 0x08, 0x8E);
    idt_set_gate(24, (uint64_t)isr24, 0x08, 0x8E); idt_set_gate(25, (uint64_t)isr25, 0x08, 0x8E);
    idt_set_gate(26, (uint64_t)isr26, 0x08, 0x8E); idt_set_gate(27, (uint64_t)isr27, 0x08, 0x8E);
    idt_set_gate(28, (uint64_t)isr28, 0x08, 0x8E); idt_set_gate(29, (uint64_t)isr29, 0x08, 0x8E);
    idt_set_gate(30, (uint64_t)isr30, 0x08, 0x8E); idt_set_gate(31, (uint64_t)isr31, 0x08, 0x8E);

    /* Hardware Interrupts (IRQs) */
    extern void irq0(); extern void irq1(); extern void irq2(); extern void irq3();
    extern void irq4(); extern void irq5(); extern void irq6(); extern void irq7();
    extern void irq8(); extern void irq9(); extern void irq10(); extern void irq11();
    extern void irq12(); extern void irq13(); extern void irq14(); extern void irq15();

    idt_set_gate(32, (uint64_t)irq0, 0x08, 0x8E);
    idt_set_gate(33, (uint64_t)irq1, 0x08, 0x8E);
    idt_set_gate(34, (uint64_t)irq2, 0x08, 0x8E);
    idt_set_gate(35, (uint64_t)irq3, 0x08, 0x8E);
    idt_set_gate(36, (uint64_t)irq4, 0x08, 0x8E);
    idt_set_gate(37, (uint64_t)irq5, 0x08, 0x8E);
    idt_set_gate(38, (uint64_t)irq6, 0x08, 0x8E);
    idt_set_gate(39, (uint64_t)irq7, 0x08, 0x8E);
    idt_set_gate(40, (uint64_t)irq8, 0x08, 0x8E);
    idt_set_gate(41, (uint64_t)irq9, 0x08, 0x8E);
    idt_set_gate(42, (uint64_t)irq10, 0x08, 0x8E);
    idt_set_gate(43, (uint64_t)irq11, 0x08, 0x8E);
    idt_set_gate(44, (uint64_t)irq12, 0x08, 0x8E);
    idt_set_gate(45, (uint64_t)irq13, 0x08, 0x8E);
    idt_set_gate(46, (uint64_t)irq14, 0x08, 0x8E);
    idt_set_gate(47, (uint64_t)irq15, 0x08, 0x8E);

    idt_flush((uint64_t)&idt_ptr);
}

void register_interrupt_handler(uint8_t n, isr_t handler) {
    interrupt_handlers[n] = handler;
}

static void print_hex(uint64_t val) {
    char buf[20];
    char* hex = "0123456789ABCDEF";
    buf[19] = '\0';
    int i = 18;
    if (val == 0) {
        buf[i--] = '0';
    } else {
        while (val > 0 && i >= 0) {
            buf[i--] = hex[val % 16];
            val /= 16;
        }
    }
    print_serial("0x");
    print_serial(&buf[i+1]);
}

registers_t* isr_handler(registers_t* r) {
    /* System calls are not faults: dispatch them silently and return whichever
     * frame should resume (sys_yield/sys_exit hand back a different task). */
    if (r->int_no == 128) {
        return syscall_dispatch(r);
    }

    /* Any other vector with a registered handler is also handled silently. */
    if (interrupt_handlers[r->int_no] != 0) {
        isr_t handler = interrupt_handlers[r->int_no];
        handler(r);
        return r;
    }

    uint64_t cr2;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));

    /* Ring 3 fault containment: a fault that happened at CPL 3 (a buggy user
     * task) must never take down the kernel. Log it, retire that task, and
     * schedule another -- the desktop keeps running. Kernel-mode faults
     * (CPL 0) fall through to the diagnostics + BSOD below. */
    if ((r->cs & 3) == 3) {
        print_serial("[Kernel] Ring 3 task fault (vector ");
        print_hex(r->int_no);
        print_serial(", RIP ");
        print_hex(r->rip);
        print_serial(", CR2 ");
        print_hex(cr2);
        print_serial(") -- terminating task.\n");
        task_exit();
        return schedule(r);
    }

    print_serial("Exception received! Vector: ");
    print_hex(r->int_no);
    print_serial(" | Error Code: ");
    print_hex(r->err_code);
    print_serial(" | RIP: ");
    print_hex(r->rip);
    print_serial(" | CR2 (Fault Address): ");
    print_hex(cr2);
    print_serial("\n");

    /* Fault isolation: if the fault happened inside a game's code, abort
     * only the game and unwind back to the desktop loop instead of
     * halting the entire OS. The interrupt gate cleared IF, so re-enable
     * interrupts before abandoning this exception frame via longjmp. */
    if (quake_running) {
        print_serial("[Kernel] Fault inside Quake. Terminating game, returning to desktop.\n");
        quake_running = 0;
        __asm__ volatile("sti");
        longjmp(quake_exit_jmp, 1);
    }
    if (doom_running) {
        print_serial("[Kernel] Fault inside Doom. Terminating game, returning to desktop.\n");
        doom_running = 0;
        __asm__ volatile("sti");
        longjmp(doom_exit_jmp, 1);
    }

    print_serial("Unhandled CPU Exception! System Halted.\n");
    gui_bsod(r, cr2);
    for (;;) __asm__ volatile("cli; hlt");
    return r; // unreachable; satisfies the non-void return type
}

registers_t* irq_handler(registers_t* r) {
    /* Send End of Interrupt (EOI) to PICs */
    if (r->int_no >= 40) {
        outb(0xA0, 0x20); // Slave PIC EOI
    }
    outb(0x20, 0x20);     // Master PIC EOI

    if (interrupt_handlers[r->int_no] != 0) {
        isr_t handler = interrupt_handlers[r->int_no];
        handler(r);
    }

    if (r->int_no == 32) { // IRQ0 Timer Interrupt -> schedule
        return schedule(r);
    }

    return r;
}

