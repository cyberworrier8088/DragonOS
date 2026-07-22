#include "cpu/gdt.h"
#include "cpu/idt.h"
#include "cpu/scheduler.h"
#include "drivers/serial.h"
#include "drivers/timer.h"
#include "drivers/keyboard.h"
#include "drivers/mouse.h"
#include "drivers/graphics.h"
#include "shell/gui.h"
#include "mm/pmm.h"
#include "mm/kheap.h"
#include "mm/paging.h"
#include "drivers/pci.h"
#include "fs/vfs.h"
#include "libc/string.h"
#include "libc/stdlib.h"
#include "../limine-bin/limine.h"

static void user_mode_task_1(void) {
    const char* msg = "[UserTask 1] Hello from Ring 3 (User Mode)! Executing sys_print syscall...\n";
    __asm__ volatile(
        "mov $1, %%rax\n"
        "mov %0, %%rbx\n"
        "int $0x80\n"
        : : "r"(msg) : "rax", "rbx"
    );

    for (;;) {
        for (volatile int i = 0; i < 50000000; i++);
        const char* tick_msg = "[UserTask 1] Ring 3 alive & preempted smoothly.\n";
        __asm__ volatile(
            "mov $1, %%rax\n"
            "mov %0, %%rbx\n"
            "int $0x80\n"
            : : "r"(tick_msg) : "rax", "rbx"
        );
    }
}

static void user_mode_task_2(void) {
    const char* msg = "[UserTask 2] Hello from Ring 3 (User Mode Task 2)!\n";
    __asm__ volatile(
        "mov $1, %%rax\n"
        "mov %0, %%rbx\n"
        "int $0x80\n"
        : : "r"(msg) : "rax", "rbx"
    );

    for (;;) {
        for (volatile int i = 0; i < 50000000; i++);
        const char* tick_msg = "[UserTask 2] Ring 3 alive & preempted smoothly.\n";
        __asm__ volatile(
            "mov $1, %%rax\n"
            "mov %0, %%rbx\n"
            "int $0x80\n"
            : : "r"(tick_msg) : "rax", "rbx"
        );
    }
}

// Set the base revision to 4
__attribute__((used, section(".limine_requests")))
static volatile LIMINE_BASE_REVISION(4);

// Request a graphical framebuffer
__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

// Request memory map from bootloader
__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0
};

// Request Higher Half Direct Map (HHDM)
__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0
};

// Request bootloader modules
__attribute__((used, section(".limine_requests")))
static volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST,
    .revision = 0
};

static void enable_sse(void) {
    uint64_t cr0;
    uint64_t cr4;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1ULL << 2); // Clear EM (Emulation)
    cr0 |= (1ULL << 1);  // Set MP (Monitor Co-processor)
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));

    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (3ULL << 9);  // Set OSFXSR and OSXMMEXCPT
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4));
    print_serial("[DragonOS] CPU SSE/SSE2 instructions enabled.\n");
}

void kernel_main(void) {
    /* Initialize serial port for debug logs */
    init_serial();
    print_serial("[DragonOS] Booting 64-bit kernel under Limine Boot Protocol...\n");
    enable_sse();

    /* Initialize GDT and TSS for Ring 0 & Ring 3 support */
    gdt_init();

    /* Initialize CPU IDT */
    idt_init();
    print_serial("[DragonOS] 64-bit IDT loaded.\n");

    /* Framebuffer variables */
    uint64_t fb_addr = 0;
    uint32_t fb_width = 800;
    uint32_t fb_height = 600;
    uint32_t fb_pitch = 800 * 4;

    /* Verify if the bootloader provided a valid linear framebuffer */
    if (framebuffer_request.response != NULL && framebuffer_request.response->framebuffer_count >= 1) {
        struct limine_framebuffer* fb = framebuffer_request.response->framebuffers[0];
        fb_addr = (uint64_t)fb->address;
        fb_width = fb->width;
        fb_height = fb->height;
        fb_pitch = fb->pitch;
        print_serial("[DragonOS] Framebuffer details acquired from Limine protocol.\n");
    } else {
        print_serial("[DragonOS] Error: Limine did not provide a linear framebuffer! System halted.\n");
        for (;;) {
            __asm__ volatile("cli; hlt");
        }
    }

    /* Initialize linear graphics framebuffer */
    graphics_init(fb_addr, fb_width, fb_height, fb_pitch);
    print_serial("[DragonOS] Linear Graphics Framebuffer active.\n");

    /* Initialize keyboard and timer interrupts */
    init_timer(100);
    init_keyboard();
    print_serial("[DragonOS] Timer (100Hz) & Keyboard drivers active.\n");

    /* Initialize Advanced Memory Management (PMM & KHeap) */
    if (memmap_request.response != NULL && hhdm_request.response != NULL) {
        pmm_init(memmap_request.response, hhdm_request.response->offset);
        kheap_init();
        print_serial("[DragonOS] Physical Memory Manager and Kernel Heap initialized.\n");

        // Paging must be live before the scheduler creates user tasks:
        // create_user_task() calls paging_make_user() to grant Ring 3 access
        // to each task's code and stack, which needs the active PML4.
        init_paging();
        // Test Paging by mapping 0x1000000000 -> 0x1000000 physical
        paging_map(0x1000000000ULL, 0x1000000ULL, PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
        uint64_t walk = paging_walk(0x1000000000ULL);
        if ((walk & ~0xFFFULL) == 0x1000000ULL) {
            print_serial("[DragonOS] Paging Test SUCCESS: 64-bit page mapped dynamically!\n");
        } else {
            print_serial("[DragonOS] Paging Test FAILED!\n");
        }

        scheduler_init();
        create_user_task(user_mode_task_1, "UserTask1");
        create_user_task(user_mode_task_2, "UserTask2");
    } else {
        print_serial("[DragonOS] Error: Limine did not provide a memory map or HHDM!\n");
    }

    /* Initialize mouse handler and register interrupt vector 44 (IRQ12) */
    register_interrupt_handler(44, mouse_handler);
    init_mouse();
    print_serial("[DragonOS] PS/2 mouse interface initialized.\n");

    /* Initialize PCI Bus scanner */
    pci_init();

    /* Initialize ATA/IDE Disk Controller */
    extern void ata_init(void);
    ata_init();

    /* Initialize POSIX VFS */
    init_vfs();

    /* Register bootloader modules in POSIX VFS */
    if (module_request.response != NULL) {
        for (uint64_t i = 0; i < module_request.response->module_count; i++) {
            struct limine_file* file = module_request.response->modules[i];
            print_serial("[Limine] Loaded module path: ");
            print_serial(file->path);
            print_serial("\n");
            
            // Map the raw path
            vfs_register_file(file->path, file->address, file->size);
            
            // Also map basename
            const char* base = file->path;
            for (const char* p = file->path; *p; p++) {
                if (*p == '/') base = p + 1;
            }
            if (base != file->path) {
                vfs_register_file(base, file->address, file->size);
            }
            
            // Also map leading slash variants
            if (file->path[0] != '/') {
                char temp[128] = "/";
                strcat(temp, file->path);
                vfs_register_file(temp, file->address, file->size);
            } else {
                vfs_register_file(file->path + 1, file->address, file->size);
            }
        }
    }

    // Verify POSIX VFS & File Descriptors
    int fd = open("/sys/meminfo", 0);
    if (fd >= 0) {
        char buf[256];
        int bytes = read(fd, buf, sizeof(buf) - 1);
        if (bytes > 0) {
            buf[bytes] = '\0';
            print_serial("[DragonOS] POSIX VFS Test SUCCESS: Read /sys/meminfo contents:\n");
            write(2, buf, bytes); // write to stderr (2)
        }
        close(fd);
    } else {
        print_serial("[DragonOS] POSIX VFS Test FAILED: Could not open /sys/meminfo\n");
    }

    /* Initialize Window Manager GUI */
    init_gui();
    extern void init_2048(void);
    init_2048();
    print_serial("[DragonOS] Windows 11 Fluent Design Desktop initialized.\n");

    /* Enable hardware CPU interrupts */
    __asm__ volatile("sti");
    print_serial("[DragonOS] Hardware interrupts enabled. Starting desktop loop...\n");

    /* Desktop Rendering loop */
    extern uint32_t get_ticks(void);
    uint32_t last_game_ticks = get_ticks();
    while (1) {
        extern int doom_running;
        extern int quake_running;
        if (doom_running) {
            extern void doomgeneric_Tick(void);
            doomgeneric_Tick();
        }
        if (quake_running) {
            /* Pass real elapsed wall time so Host_FilterTime paces the
             * game correctly instead of a hardcoded 10ms guess. */
            uint32_t now = get_ticks();
            double dt = (double)(now - last_game_ticks) * 0.01;
            last_game_ticks = now;

            extern jmp_buf quake_exit_jmp;
            if (setjmp(quake_exit_jmp) == 0) {
                extern void QG_Tick(double);
                QG_Tick(dt);
            } else {
                print_serial("[Quake] Game aborted during tick. Returning to desktop.\n");
                quake_running = 0;
                extern void gui_close_quake(void);
                gui_close_quake();
            }
        } else {
            last_game_ticks = get_ticks();
        }
        gui_handle_mouse(mouse_x, mouse_y, mouse_l_click, mouse_r_click);
        gui_draw();

        extern void sleep_ms(uint32_t ms);
        if (doom_running || quake_running) {
            /* While a game runs, just yield until the next hardware
             * interrupt (<=10ms). A fixed 16ms sleep on top of the game's
             * own frame cost made input feel frozen. */
            __asm__ volatile("hlt");
        } else {
            /* Idle desktop: cap at ~60 FPS to keep host CPU usage low. */
            sleep_ms(16);
        }
    }
}
