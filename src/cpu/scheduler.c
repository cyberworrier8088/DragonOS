#include "scheduler.h"
#include "gdt.h"
#include "../mm/kheap.h"
#include "../mm/paging.h"
#include "../drivers/serial.h"
#include "../libc/string.h"

// Bytes of code, starting at the entry point's page, to expose to Ring 3.
// The demo user routines are tiny and self-contained; 16KB is ample headroom.
#define USER_CODE_MAP_SIZE 0x4000

static task_t* task_list = 0;
static task_t* current_task = 0;
static uint32_t next_pid = 1;
static int scheduler_enabled = 0;

task_t* get_current_task(void) {
    return current_task;
}

void scheduler_init(void) {
    // Create Task 0 (Main Kernel Idle Task)
    task_t* idle_task = (task_t*)kmalloc(sizeof(task_t));
    memset(idle_task, 0, sizeof(task_t));
    idle_task->pid = 0;
    idle_task->state = TASK_RUNNING;
    idle_task->is_user = 0;
    strncpy(idle_task->name, "kernel_main", sizeof(idle_task->name) - 1);
    
    // Allocate a dedicated kernel stack for Task 0 TSS updates
    idle_task->kernel_stack = (uint64_t)kmalloc(KERNEL_STACK_SIZE);
    
    task_list = idle_task;
    idle_task->next = idle_task; // Circular linked list
    current_task = idle_task;

    tss_set_rsp0(idle_task->kernel_stack + KERNEL_STACK_SIZE);
    scheduler_enabled = 1;

    print_serial("[DragonOS] Preemptive Multitasking initialized (Task 0 ready).\n");
}

task_t* create_kernel_task(void (*entry)(void), const char* name) {
    task_t* new_task = (task_t*)kmalloc(sizeof(task_t));
    memset(new_task, 0, sizeof(task_t));

    new_task->pid = next_pid++;
    new_task->state = TASK_READY;
    new_task->is_user = 0;
    strncpy(new_task->name, name, sizeof(new_task->name) - 1);

    new_task->kernel_stack = (uint64_t)kmalloc(KERNEL_STACK_SIZE);
    uint64_t stack_top = new_task->kernel_stack + KERNEL_STACK_SIZE;

    // Reserve registers_t on kernel stack
    registers_t* regs = (registers_t*)(stack_top - sizeof(registers_t));
    memset(regs, 0, sizeof(registers_t));

    regs->cs = KERNEL_CS;
    regs->ss = KERNEL_DS;
    regs->rsp = stack_top;
    regs->rflags = 0x202; // IF=1 (Interrupts Enabled)
    regs->rip = (uint64_t)entry;
    regs->int_no = 32;
    regs->err_code = 0;

    new_task->rsp = (uint64_t)regs;

    // Insert into circular task list
    new_task->next = task_list->next;
    task_list->next = new_task;

    print_serial("[DragonOS] Created Kernel Thread: ");
    print_serial(name);
    print_serial(" (PID=");
    char pid_str[16];
    int_to_ascii(new_task->pid, pid_str);
    print_serial(pid_str);
    print_serial(")\n");

    return new_task;
}

task_t* create_user_task(void (*entry)(void), const char* name) {
    task_t* new_task = (task_t*)kmalloc(sizeof(task_t));
    memset(new_task, 0, sizeof(task_t));

    new_task->pid = next_pid++;
    new_task->state = TASK_READY;
    new_task->is_user = 1;
    strncpy(new_task->name, name, sizeof(new_task->name) - 1);

    new_task->kernel_stack = (uint64_t)kmalloc(KERNEL_STACK_SIZE);
    new_task->user_stack = (uint64_t)kmalloc(USER_STACK_SIZE);

    uint64_t kstack_top = new_task->kernel_stack + KERNEL_STACK_SIZE;
    uint64_t ustack_top = new_task->user_stack + USER_STACK_SIZE;

    // Without this the first preemption into Ring 3 page-faults: the entry
    // code and the user stack both live in supervisor-only kernel/HHDM pages.
    // Mark them user-accessible so the CPU can fetch code and push to the
    // stack at CPL 3. The kernel_stack stays supervisor-only (it backs the
    // TSS RSP0 that interrupts land on, and must not be reachable from Ring 3).
    paging_make_user((uint64_t)entry, USER_CODE_MAP_SIZE);
    paging_make_user(new_task->user_stack, USER_STACK_SIZE);

    // Reserve registers_t on kernel stack
    registers_t* regs = (registers_t*)(kstack_top - sizeof(registers_t));
    memset(regs, 0, sizeof(registers_t));

    regs->cs = USER_CS;   // 0x23 (Ring 3 Code)
    regs->ss = USER_DS;   // 0x1B (Ring 3 Data)
    regs->rsp = ustack_top; // User Mode Stack Pointer
    regs->rflags = 0x202;   // IF=1 (Interrupts Enabled)
    regs->rip = (uint64_t)entry; // User Mode Entry Point
    regs->int_no = 32;
    regs->err_code = 0;

    new_task->rsp = (uint64_t)regs;

    // Insert into circular task list
    new_task->next = task_list->next;
    task_list->next = new_task;

    print_serial("[DragonOS] Created Ring 3 User Task: ");
    print_serial(name);
    print_serial(" (PID=");
    char pid_str[16];
    int_to_ascii(new_task->pid, pid_str);
    print_serial(pid_str);
    print_serial(")\n");

    return new_task;
}

registers_t* schedule(registers_t* regs) {
    if (!scheduler_enabled || !current_task) {
        return regs;
    }

    // Save current task context
    current_task->rsp = (uint64_t)regs;

    // Pick the next non-zombie task. The list is circular, so bound the search
    // by stopping if we come all the way back around -- otherwise a state where
    // every other task (or even the current one) is a zombie would spin forever.
    task_t* next = current_task->next;
    while (next != current_task && next->state == TASK_ZOMBIE) {
        next = next->next;
    }

    if (next == current_task || next->state == TASK_ZOMBIE) {
        // Nothing else is runnable; stay on the current task.
        return regs;
    }

    current_task = next;

    // Update TSS.RSP0 so interrupts in Ring 3 switch to this task's kernel stack
    tss_set_rsp0(current_task->kernel_stack + KERNEL_STACK_SIZE);

    return (registers_t*)current_task->rsp;
}

void task_exit(void) {
    if (current_task) {
        print_serial("[DragonOS] Task Exiting: ");
        print_serial(current_task->name);
        print_serial("\n");
        current_task->state = TASK_ZOMBIE;
    }
    for (;;) {
        __asm__ volatile("hlt");
    }
}

void task_yield(void) {
    __asm__ volatile("int $32"); // Trigger IRQ0 timer vector manually
}
