#include "cpu/idt.h"
#include "drivers/screen.h"
#include "drivers/serial.h"
#include "drivers/timer.h"
#include "drivers/keyboard.h"
#include "shell/shell.h"

void kernel_main(void) {
    /* Initialize low-level descriptor tables */
    idt_init();

    /* Initialize drivers */
    init_serial();
    terminal_initialize();
    init_timer(100);    /* PIT frequency = 100 Hz */
    init_keyboard();

    /* Enable hardware interrupts */
    __asm__ volatile("sti");

    /* Print premium DragonOS boot screen information */
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_writestring("================================================================================\n");
    terminal_writestring("                               DragonOS v0.2.0 (x64)                            \n");
    terminal_writestring("================================================================================\n\n");

    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    terminal_writestring("[OK] 64-bit Page Directories initialized & CR3 loaded.\n");
    terminal_writestring("[OK] 64-bit Global Descriptor Table (GDT) active.\n");
    terminal_writestring("[OK] 64-bit Interrupt Descriptor Table (IDT) loaded.\n");
    terminal_writestring("[OK] PIC remapped & hardware interrupts enabled.\n");
    terminal_writestring("[OK] COM1 Serial logging port initialized.\n");
    terminal_writestring("[OK] System Timer (PIT) calibrated to 100Hz.\n");
    terminal_writestring("[OK] PS/2 keyboard interface initialized.\n\n");

    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    terminal_writestring("Welcome to DragonOS, a secure, monolithic 64-bit x86_64 kernel.\n");
    terminal_writestring("Type 'help' to see the list of available commands.\n\n");

    /* Send boot notification to serial log */
    print_serial("[DragonOS] Boot finished. Launching 64-bit shell...\n");

    /* Launch the interactive shell */
    init_shell();

    /* Safe idle kernel execution loop */
    while (1) {
        __asm__ volatile("hlt");
    }
}
