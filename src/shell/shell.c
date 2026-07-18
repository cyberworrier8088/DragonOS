#include "shell.h"
#include "../drivers/screen.h"
#include "../drivers/serial.h"
#include "../drivers/timer.h"
#include "../cpu/ports.h"
#include "../libc/string.h"

static char input_buffer[256];

void print_prompt(void) {
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_writestring("dragonos> ");
    terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
}

void init_shell(void) {
    input_buffer[0] = '\0';
    print_prompt();
}

static void execute_command(char* input) {
    if (strlen(input) == 0) {
        return;
    }

    if (strcmp(input, "help") == 0) {
        terminal_writestring("Available commands:\n");
        terminal_writestring("  help        - Show this help menu\n");
        terminal_writestring("  about       - Display OS information\n");
        terminal_writestring("  clear       - Clear the screen\n");
        terminal_writestring("  ticks       - Show system timer ticks elapsed\n");
        terminal_writestring("  ping        - Test command response\n");
        terminal_writestring("  echo <msg>  - Echo input text back to the screen\n");
        terminal_writestring("  serial <msg>- Write message to COM1 serial port\n");
        terminal_writestring("  reboot      - Reboot the computer\n");
        terminal_writestring("  halt        - Halt the CPU safely\n");
    } else if (strcmp(input, "about") == 0) {
        terminal_writestring("DragonOS x86 Kernel (32-bit)\n");
        terminal_writestring("Build date  : July 2026\n");
        terminal_writestring("Bootloader  : Multiboot 1 compliant (GRUB)\n");
        terminal_writestring("Design      : Monolithic architecture & modular drivers\n");
    } else if (strcmp(input, "clear") == 0) {
        terminal_clear();
    } else if (strcmp(input, "ping") == 0) {
        terminal_writestring("pong!\n");
    } else if (strcmp(input, "ticks") == 0) {
        char tick_str[32];
        int_to_ascii(get_ticks(), tick_str);
        terminal_writestring("System timer ticks: ");
        terminal_writestring(tick_str);
        terminal_writestring("\n");
    } else if (strncmp(input, "echo ", 5) == 0) {
        terminal_writestring(input + 5);
        terminal_writestring("\n");
    } else if (strncmp(input, "serial ", 7) == 0) {
        print_serial(input + 7);
        print_serial("\n");
        terminal_writestring("Message sent to COM1 serial port.\n");
    } else if (strcmp(input, "reboot") == 0) {
        terminal_writestring("Rebooting system...\n");
        /* Pulse CPU reset line using keyboard controller */
        outb(0x64, 0xFE);
        while (1) {
            __asm__ volatile("hlt");
        }
    } else if (strcmp(input, "halt") == 0) {
        terminal_writestring("System halted safely. Goodbye!\n");
        __asm__ volatile("cli; hlt");
    } else {
        terminal_writestring("Unknown command: ");
        terminal_writestring(input);
        terminal_writestring("\nType 'help' for a list of commands.\n");
    }
}

void shell_input_char(char c) {
    if (c == '\n') {
        terminal_putchar('\n');
        execute_command(input_buffer);
        input_buffer[0] = '\0';
        print_prompt();
    } else if (c == '\b') {
        int len = strlen(input_buffer);
        if (len > 0) {
            backspace(input_buffer);
            terminal_backspace();
        }
    } else {
        int len = strlen(input_buffer);
        if (len < 255) {
            append(input_buffer, c);
            terminal_putchar(c);
        }
    }
}
