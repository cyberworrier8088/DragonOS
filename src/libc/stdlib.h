#ifndef STDLIB_H
#define STDLIB_H

#include <stddef.h>
#include <stdint.h>

void* malloc(size_t size);
void free(void* ptr);
void* realloc(void* ptr, size_t size);
void* calloc(size_t num, size_t size);
void exit(int status);
char* getenv(const char* name);
int abs(int x);
int atoi(const char* str);
double atof(const char* str);
long strtol(const char* nptr, char** endptr, int base);
unsigned long strtoul(const char* nptr, char** endptr, int base);

typedef uint64_t jmp_buf[8];
int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);

const uint16_t** __ctype_b_loc(void);
int system(const char* command);
int mkdir(const char* pathname, uint32_t mode);

#endif
