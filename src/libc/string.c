#include "string.h"
#include <stdint.h>

size_t strlen(const char* s) {
    size_t len = 0;
    while (s[len] != '\0') {
        len++;
    }
    return len;
}

void reverse(char s[]) {
    int i, j;
    char c;
    for (i = 0, j = strlen(s)-1; i < j; i++, j--) {
        c = s[i];
        s[i] = s[j];
        s[j] = c;
    }
}

void int_to_ascii(int n, char str[]) {
    int i, sign;
    if ((sign = n) < 0) {
        n = -n;
    }
    i = 0;
    do {
        str[i++] = n % 10 + '0';
    } while ((n /= 10) > 0);

    if (sign < 0) {
        str[i++] = '-';
    }
    str[i] = '\0';
    reverse(str);
}

int strcmp(const char* s1, const char* s2) {
    int i;
    for (i = 0; s1[i] == s2[i]; i++) {
        if (s1[i] == '\0') {
            return 0;
        }
    }
    return s1[i] - s2[i];
}

int strncmp(const char* s1, const char* s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i]) {
            return s1[i] - s2[i];
        }
        if (s1[i] == '\0') {
            return 0;
        }
    }
    return 0;
}

void append(char s[], char n) {
    int len = strlen(s);
    s[len] = n;
    s[len+1] = '\0';
}

void backspace(char s[]) {
    int len = strlen(s);
    if (len > 0) {
        s[len-1] = '\0';
    }
}

void* memset(void* dest, int val, size_t len) {
    uint64_t* d64 = (uint64_t*)dest;
    uint8_t v = (uint8_t)val;
    uint64_t v64 = ((uint64_t)v << 56) | ((uint64_t)v << 48) | ((uint64_t)v << 40) | ((uint64_t)v << 32) |
                   ((uint64_t)v << 24) | ((uint64_t)v << 16) | ((uint64_t)v << 8)  | (uint64_t)v;
    
    size_t len64 = len / 8;
    while (len64-- > 0) {
        *d64++ = v64;
    }
    
    uint8_t* d8 = (uint8_t*)d64;
    size_t len8 = len % 8;
    while (len8-- > 0) {
        *d8++ = v;
    }
    return dest;
}

void* memcpy(void* dest, const void* src, size_t len) {
    uint64_t* d64 = (uint64_t*)dest;
    const uint64_t* s64 = (const uint64_t*)src;
    
    size_t len64 = len / 8;
    while (len64-- > 0) {
        *d64++ = *s64++;
    }
    
    uint8_t* d8 = (uint8_t*)d64;
    const uint8_t* s8 = (const uint8_t*)s64;
    size_t len8 = len % 8;
    while (len8-- > 0) {
        *d8++ = *s8++;
    }
    return dest;
}

char* strcpy(char* dest, const char* src) {
    char* d = dest;
    while ((*d++ = *src++))
        ;
    return dest;
}

char* strcat(char* dest, const char* src) {
    char* d = dest;
    while (*d) {
        d++;
    }
    while ((*d++ = *src++))
        ;
    return dest;
}
