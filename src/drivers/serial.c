#include "serial.h"
#include "../cpu/ports.h"

#define SERIAL_PORT_COM1 0x3f8

int init_serial(void) {
    outb(SERIAL_PORT_COM1 + 1, 0x00);    // Disable all interrupts
    outb(SERIAL_PORT_COM1 + 3, 0x80);    // Enable DLAB (set divisor)
    outb(SERIAL_PORT_COM1 + 0, 0x03);    // Divisor 3 (lo) 38400 baud
    outb(SERIAL_PORT_COM1 + 1, 0x00);    //            (hi)
    outb(SERIAL_PORT_COM1 + 3, 0x03);    // 8 bits, no parity, one stop bit
    outb(SERIAL_PORT_COM1 + 2, 0xC7);    // Enable FIFO, clear them
    outb(SERIAL_PORT_COM1 + 4, 0x0F);    // IRQs enabled, RTS/DSR set
    return 0;
}

static int is_transmit_empty(void) {
    return inb(SERIAL_PORT_COM1 + 5) & 0x20;
}

void write_serial(char c) {
    while (is_transmit_empty() == 0);
    outb(SERIAL_PORT_COM1, c);
}

void print_serial(const char* data) {
    for (size_t i = 0; data[i] != '\0'; i++) {
        if (data[i] == '\n') {
            write_serial('\r');
        }
        write_serial(data[i]);
    }
}
