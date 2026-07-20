#include "string.h"
#include <stdint.h>

size_t strlen(const char* s) {
    size_t len;
    asm volatile(
        "repne scasb\n"
        "not %%ecx\n"
        "dec %%ecx\n"
        : "=c" (len), "+D" (s)
        : "c" (-1), "a" (0)
        : "memory"
    );
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
    uint8_t v = (uint8_t)val;
    uint64_t v64 = ((uint64_t)v << 56) | ((uint64_t)v << 48) | ((uint64_t)v << 40) | ((uint64_t)v << 32) |
                   ((uint64_t)v << 24) | ((uint64_t)v << 16) | ((uint64_t)v << 8)  | (uint64_t)v;
    size_t qwords = len / 8;
    size_t bytes = len % 8;
    void* d = dest;

    __asm__ volatile (
        "rep stosq\n"
        "mov %3, %%rcx\n"
        "rep stosb\n"
        : "+D"(d), "+c"(qwords)
        : "a"(v64), "r"(bytes)
        : "memory"
    );

    return dest;
}

void* memcpy(void* dest, const void* src, size_t len) {
    size_t qwords = len / 8;
    size_t bytes = len % 8;
    void* d = dest;
    const void* s = src;

    __asm__ volatile (
        "rep movsq\n"
        "mov %4, %%rcx\n"
        "rep movsb\n"
        : "+D"(d), "+S"(s), "+c"(qwords)
        : "a"(0), "r"(bytes)
        : "memory"
    );

    return dest;
}

char* strcpy(char* dest, const char* src) {
    char* ret = dest;
    size_t len = strlen(src) + 1; // Include null terminator
    asm volatile(
        "rep movsb\n"
        : "+D" (dest), "+S" (src), "+c" (len)
        :
        : "memory"
    );
    return ret;
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

#include "../mm/kheap.h"

int strcasecmp(const char* s1, const char* s2) {
    int i = 0;
    while (s1[i] && s2[i]) {
        char c1 = s1[i];
        char c2 = s2[i];
        if (c1 >= 'A' && c1 <= 'Z') c1 = c1 - 'A' + 'a';
        if (c2 >= 'A' && c2 <= 'Z') c2 = c2 - 'A' + 'a';
        if (c1 != c2) return c1 - c2;
        i++;
    }
    char c1 = s1[i];
    char c2 = s2[i];
    if (c1 >= 'A' && c1 <= 'Z') c1 = c1 - 'A' + 'a';
    if (c2 >= 'A' && c2 <= 'Z') c2 = c2 - 'A' + 'a';
    return c1 - c2;
}

int strncasecmp(const char* s1, const char* s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        char c1 = s1[i];
        char c2 = s2[i];
        if (c1 >= 'A' && c1 <= 'Z') c1 = c1 - 'A' + 'a';
        if (c2 >= 'A' && c2 <= 'Z') c2 = c2 - 'A' + 'a';
        if (c1 != c2) return c1 - c2;
        if (s1[i] == '\0' || s2[i] == '\0') break;
    }
    return 0;
}

char* strrchr(const char* s, int c) {
    char* last = 0;
    while (*s) {
        if (*s == c) last = (char*)s;
        s++;
    }
    return last;
}

char* strstr(const char* haystack, const char* needle) {
    size_t n_len = strlen(needle);
    if (n_len == 0) return (char*)haystack;
    while (*haystack) {
        if (strncmp(haystack, needle, n_len) == 0) {
            return (char*)haystack;
        }
        haystack++;
    }
    return 0;
}

char* strdup(const char* s) {
    size_t len = strlen(s);
    char* d = kmalloc(len + 1);
    if (d) {
        memcpy(d, s, len + 1);
    }
    return d;
}

char* strncpy(char* dest, const char* src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    return dest;
}

char* strchr(const char* s, int c) {
    while (*s) {
        if (*s == c) return (char*)s;
        s++;
    }
    if (c == '\0') return (char*)s;
    return 0;
}

int memcmp(const void* s1, const void* s2, size_t n) {
    const uint8_t* p1 = (const uint8_t*)s1;
    const uint8_t* p2 = (const uint8_t*)s2;
    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) return p1[i] - p2[i];
    }
    return 0;
}

void* memchr(const void* s, int c, size_t n) {
    const uint8_t* p = (const uint8_t*)s;
    for (size_t i = 0; i < n; i++) {
        if (p[i] == (uint8_t)c) return (void*)(p + i);
    }
    return 0;
}

size_t strcspn(const char* s1, const char* s2) {
    size_t i = 0;
    while (s1[i] != '\0') {
        if (strchr(s2, s1[i]) != 0) return i;
        i++;
    }
    return i;
}

char* strncat(char* dest, const char* src, size_t n) {
    char* d = dest;
    while (*d) d++;
    size_t i = 0;
    while (i < n && *src) {
        *d++ = *src++;
        i++;
    }
    *d = '\0';
    return dest;
}

int strcoll(const char* s1, const char* s2) {
    return strcmp(s1, s2);
}

char* strpbrk(const char* s1, const char* s2) {
    while (*s1) {
        if (strchr(s2, *s1) != 0) return (char*)s1;
        s1++;
    }
    return 0;
}
