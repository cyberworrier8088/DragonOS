#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>
#include "idt.h"

#define TASK_READY   0
#define TASK_RUNNING 1
#define TASK_ZOMBIE  2

#define KERNEL_STACK_SIZE 0x10000 // 64KB
#define USER_STACK_SIZE   0x10000 // 64KB

typedef struct task {
    uint64_t rsp;             // Saved stack pointer (points to registers_t)
    uint32_t pid;             // Task ID
    uint32_t state;           // Task state
    uint64_t kernel_stack;    // Base address of kernel stack
    uint64_t user_stack;      // Base address of user stack (if user task)
    int is_user;              // 1 if Ring 3 user task, 0 if Ring 0 kernel thread
    char name[32];            // Task name
    struct task* next;        // Linked list pointer
} task_t;

void scheduler_init(void);
task_t* create_kernel_task(void (*entry)(void), const char* name);
task_t* create_user_task(void (*entry)(void), const char* name);
registers_t* schedule(registers_t* regs);
task_t* get_current_task(void);
void task_exit(void);
void task_yield(void);

#endif
