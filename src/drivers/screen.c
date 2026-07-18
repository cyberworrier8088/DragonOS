#include "screen.h"
#include "serial.h"
#include "../cpu/ports.h"

const size_t VGA_WIDTH = 80;
const size_t VGA_HEIGHT = 25;

size_t terminal_row;
size_t terminal_column;
uint8_t terminal_color;
uint16_t* terminal_buffer;

void terminal_initialize(void) {
    terminal_row = 0;
    terminal_column = 0;
    terminal_color = vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    terminal_buffer = (uint16_t*) 0xB8000;
    terminal_clear();
}

void terminal_setcolor(uint8_t color) {
    terminal_color = color;
}

void terminal_putentryat(char c, uint8_t color, size_t x, size_t y) {
    const size_t index = y * VGA_WIDTH + x;
    terminal_buffer[index] = vga_entry(c, color);
}

void terminal_scroll(void) {
    for (size_t y = 1; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t src_index = y * VGA_WIDTH + x;
            const size_t dest_index = (y - 1) * VGA_WIDTH + x;
            terminal_buffer[dest_index] = terminal_buffer[src_index];
        }
    }
    for (size_t x = 0; x < VGA_WIDTH; x++) {
        const size_t index = (VGA_HEIGHT - 1) * VGA_WIDTH + x;
        terminal_buffer[index] = vga_entry(' ', terminal_color);
    }
    terminal_row = VGA_HEIGHT - 1;
}

void terminal_putchar(char c) {
    if (c == '\n') {
        write_serial('\r');
        write_serial('\n');
        terminal_column = 0;
        if (++terminal_row == VGA_HEIGHT) {
            terminal_scroll();
        }
    } else if (c == '\r') {
        write_serial('\r');
        terminal_column = 0;
    } else if (c == '\t') {
        write_serial('\t');
        terminal_column = (terminal_column + 8) & ~7;
        if (terminal_column >= VGA_WIDTH) {
            terminal_column = 0;
            if (++terminal_row == VGA_HEIGHT) {
                terminal_scroll();
            }
        }
    } else {
        write_serial(c);
        terminal_putentryat(c, terminal_color, terminal_column, terminal_row);
        if (++terminal_column == VGA_WIDTH) {
            terminal_column = 0;
            if (++terminal_row == VGA_HEIGHT) {
                terminal_scroll();
            }
        }
    }
    update_cursor(terminal_column, terminal_row);
}

void terminal_write(const char* data, size_t size) {
    for (size_t i = 0; i < size; i++)
        terminal_putchar(data[i]);
}

void terminal_writestring(const char* data) {
    size_t len = 0;
    while (data[len] != '\0') {
        len++;
    }
    terminal_write(data, len);
}

void terminal_clear(void) {
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t index = y * VGA_WIDTH + x;
            terminal_buffer[index] = vga_entry(' ', terminal_color);
        }
    }
    terminal_row = 0;
    terminal_column = 0;
    update_cursor(0, 0);
}

void terminal_backspace(void) {
    if (terminal_column > 0) {
        terminal_column--;
        terminal_putentryat(' ', terminal_color, terminal_column, terminal_row);
    } else if (terminal_row > 0) {
        terminal_row--;
        terminal_column = VGA_WIDTH - 1;
        terminal_putentryat(' ', terminal_color, terminal_column, terminal_row);
    }
    write_serial('\b');
    write_serial(' ');
    write_serial('\b');
    update_cursor(terminal_column, terminal_row);
}

void update_cursor(int x, int y) {
    uint16_t position = y * VGA_WIDTH + x;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(position & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((position >> 8) & 0xFF));
}
