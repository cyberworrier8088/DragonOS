#ifndef STDIO_H
#define STDIO_H

#include <stddef.h>
#include <stdarg.h>

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define EOF (-1)

typedef int FILE;

extern FILE* stdin;
extern FILE* stdout;
extern FILE* stderr;

FILE* fopen(const char* filename, const char* mode);
int fclose(FILE* stream);
size_t fread(void* ptr, size_t size, size_t nmemb, FILE* stream);
size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* stream);
int fseek(FILE* stream, long offset, int whence);
long ftell(FILE* stream);

int printf(const char* format, ...);
int sprintf(char* str, const char* format, ...);
int fprintf(FILE* stream, const char* format, ...);
int vsprintf(char* str, const char* format, va_list ap);
int vsnprintf(char* str, size_t size, const char* format, va_list ap);
int snprintf(char* str, size_t size, const char* format, ...);
int puts(const char* s);
int putc(int c, FILE* stream);
int remove(const char* pathname);
int rename(const char* oldpath, const char* newpath);
int fflush(FILE* stream);
int vfprintf(FILE* stream, const char* format, va_list ap);

#endif
