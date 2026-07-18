#ifndef SERIAL_H
#define SERIAL_H

#include <stddef.h>

int init_serial(void);
void write_serial(char c);
void print_serial(const char* data);

#endif
