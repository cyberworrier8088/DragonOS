#ifndef STR_H
#define STR_H

#include <stddef.h>
#include <stdint.h>

size_t strlen(const char* s);
void int_to_ascii(int n, char str[]);
void reverse(char s[]);
int strcmp(const char* s1, const char* s2);
int strncmp(const char* s1, const char* s2, size_t n);
void append(char s[], char n);
void backspace(char s[]);
void* memset(void* dest, int val, size_t len);
void* memcpy(void* dest, const void* src, size_t len);
char* strcpy(char* dest, const char* src);
char* strcat(char* dest, const char* src);

#endif
