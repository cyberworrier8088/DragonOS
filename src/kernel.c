#include "cpu/idt.h"
#include "drivers/serial.h"
#include "drivers/timer.h"
#include "drivers/keyboard.h"
#include "drivers/mouse.h"
#include "drivers/graphics.h"
#include "shell/gui.h"
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
        // Pace drawing loop
        for (volatile int d = 0; d < 20000; d++) {}
    }
}
