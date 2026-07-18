#include "cpu/idt.h"
#include "drivers/serial.h"
#include "drivers/timer.h"
#include "drivers/keyboard.h"
#include "drivers/mouse.h"
#include "drivers/graphics.h"
#include "shell/gui.h"

/* Multiboot 1 Information structure definition */
struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;
    // Multiboot 1 Framebuffer info starts here
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;
} __attribute__((packed));

void kernel_main(uint32_t magic, uint64_t info_addr) {
    /* Initialize serial port for debug logs */
    init_serial();
    print_serial("[DragonOS] Booting 64-bit kernel...\n");

    /* Initialize CPU IDT */
    idt_init();
    print_serial("[DragonOS] 64-bit IDT loaded.\n");

    /* Default VBE settings */
    uint64_t fb_addr = 0xFD000000; // Fallback for standard QEMU stdvga
    uint32_t fb_width = 800;
    uint32_t fb_height = 600;
    uint32_t fb_pitch = 800 * 4;

    /* Parse Multiboot Information pointer if magic matches */
    if (magic == 0x2BADB002 && info_addr != 0) {
        struct multiboot_info* info = (struct multiboot_info*)info_addr;
        // Check if Bit 12 (framebuffer info) is present in flags
        if (info->flags & (1 << 12)) {
            fb_addr = info->framebuffer_addr;
            fb_width = info->framebuffer_width;
            fb_height = info->framebuffer_height;
            fb_pitch = info->framebuffer_pitch;
            print_serial("[DragonOS] Framebuffer details acquired from bootloader.\n");
        } else {
            print_serial("[DragonOS] Warning: Bootloader did not set graphics bit 12. Using standard fallback.\n");
        }
    } else {
        print_serial("[DragonOS] Warning: Invalid Multiboot magic. Using default VBE mapping.\n");
    }

    /* Initialize linear graphics framebuffer */
    graphics_init(fb_addr, fb_width, fb_height, fb_pitch);
    print_serial("[DragonOS] Linear Graphics Framebuffer active.\n");

    /* Initialize keyboard and timer interrupts */
    init_timer(100);
    init_keyboard();
    print_serial("[DragonOS] Timer (100Hz) & Keyboard drivers active.\n");

    /* Initialize mouse handler and register interrupt vector 44 (IRQ12) */
    register_interrupt_handler(44, mouse_handler);
    init_mouse();
    print_serial("[DragonOS] PS/2 mouse interface initialized.\n");

    /* Initialize Window Manager GUI */
    init_gui();
    print_serial("[DragonOS] Aero GUI Desktop initialized.\n");

    /* Enable hardware CPU interrupts */
    __asm__ volatile("sti");
    print_serial("[DragonOS] Hardware interrupts enabled. Starting desktop loop...\n");

    /* Desktop Rendering loop */
    while (1) {
        gui_draw();
        // Small delay to pace drawing loop
        for (volatile int d = 0; d < 20000; d++) {}
    }
}
