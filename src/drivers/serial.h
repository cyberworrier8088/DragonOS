#ifndef SERIAL_H
#define SERIAL_H

#include <stddef.h>

int init_serial(void);
void write_serial(char c);
void print_serial(const char* data);

// Receive side: non-blocking availability check and a blocking read.
int  serial_available(void);   // 1 if a byte is waiting in the UART, else 0
char serial_read_char(void);   // block until a byte arrives, then return it

#endif
