#include "cpu/idt.h"
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
#include "../limine-bin/limine.h"

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

void kernel_main(void) {
    /* Initialize serial port for debug logs */
    init_serial();
    print_serial("[DragonOS] Booting 64-bit kernel under Limine Boot Protocol...\n");

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
        
        init_paging();
        // Test Paging by mapping 0x1000000000 -> 0x1000000 physical
        paging_map(0x1000000000ULL, 0x1000000ULL, PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
        uint64_t walk = paging_walk(0x1000000000ULL);
        if ((walk & ~0xFFFULL) == 0x1000000ULL) {
            print_serial("[DragonOS] Paging Test SUCCESS: 64-bit page mapped dynamically!\n");
        } else {
            print_serial("[DragonOS] Paging Test FAILED!\n");
        }
    } else {
        print_serial("[DragonOS] Error: Limine did not provide a memory map or HHDM!\n");
    }

    /* Initialize mouse handler and register interrupt vector 44 (IRQ12) */
    register_interrupt_handler(44, mouse_handler);
    init_mouse();
    print_serial("[DragonOS] PS/2 mouse interface initialized.\n");

    /* Initialize PCI Bus scanner */
    pci_init();

    /* Initialize POSIX VFS */
    init_vfs();

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
    print_serial("[DragonOS] Windows 11 Fluent Design Desktop initialized.\n");

    /* Enable hardware CPU interrupts */
    __asm__ volatile("sti");
    print_serial("[DragonOS] Hardware interrupts enabled. Starting desktop loop...\n");

    /* Desktop Rendering loop */
    while (1) {
        gui_handle_mouse(mouse_x, mouse_y, mouse_l_click, mouse_r_click);
        gui_draw();
    }
}
