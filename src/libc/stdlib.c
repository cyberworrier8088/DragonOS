#include "stdlib.h"
#include "../mm/kheap.h"

void* malloc(size_t size) {
    return kmalloc(size);
}

void free(void* ptr) {
    kfree(ptr);
}

void* realloc(void* ptr, size_t size) {
    return krealloc(ptr, size);
}

void* calloc(size_t num, size_t size) {
    return kcalloc(num, size);
}

void exit(int status) {
    (void)status;
    extern int doom_running;
    extern jmp_buf doom_exit_jmp;
    typedef struct {
        int x, y, w, h;
        char title[64];
        int active;
        int minimized;
        int closed;
        int dragging;
        int drag_off_x, drag_off_y;
        int id;
        int maximized;
        int old_x, old_y, old_w, old_h;
    } stdlib_gui_window_t;
    extern stdlib_gui_window_t* windows;

    if (doom_running) {
        extern void print_serial(const char* str);
        print_serial("[Doom] exit() called. Gracefully returning to desktop...\n");
        doom_running = 0;
        if (windows) {
            windows[4].closed = 1;
            windows[4].minimized = 0;
        }
        extern int active_win_id;
        active_win_id = -1;
        extern int gui_was_clicked;
        gui_was_clicked = 0;
        longjmp(doom_exit_jmp, 1);
    }
    // Standard system halt
    __asm__ volatile("cli; hlt");
}

char* getenv(const char* name) {
    (void)name;
    return 0; // Empty environment
}

int abs(int x) {
    return (x < 0) ? -x : x;
}

int atoi(const char* str) {
    int res = 0;
    int sign = 1;
    int i = 0;
    if (str[0] == '-') {
        sign = -1;
        i++;
    }
    for (; str[i] != '\0'; ++i) {
        if (str[i] < '0' || str[i] > '9') break;
        res = res * 10 + str[i] - '0';
    }
    return sign * res;
}

double atof(const char* str) {
    double res = 0.0;
    double factor = 1.0;
    int sign = 1;
    int i = 0;
    
    if (str[0] == '-') {
        sign = -1;
        i++;
    }
    
    for (; str[i] != '\0'; ++i) {
        if (str[i] == '.') {
            i++;
            break;
        }
        if (str[i] < '0' || str[i] > '9') break;
        res = res * 10.0 + (str[i] - '0');
    }
    
    for (; str[i] != '\0'; ++i) {
        if (str[i] < '0' || str[i] > '9') break;
        res = res * 10.0 + (str[i] - '0');
        factor *= 10.0;
    }
    
    return (double)sign * res / factor;
}

int setjmp(jmp_buf env) {
    uint64_t val;
    __asm__ volatile(
        "mov %%rbx, 0(%1)\n"
        "mov %%rsp, 8(%1)\n"
        "mov %%rbp, 16(%1)\n"
        "mov %%r12, 24(%1)\n"
        "mov %%r13, 32(%1)\n"
        "mov %%r14, 40(%1)\n"
        "mov %%r15, 48(%1)\n"
        "lea 1f(%%rip), %%rax\n"
        "mov %%rax, 56(%1)\n"
        "mov $0, %%rax\n"
        "1:\n"
        "mov %%rax, %0\n"
        : "=r"(val) : "r"(env) : "rax", "memory"
    );
    return val;
}

void longjmp(jmp_buf env, int val) {
    __asm__ volatile(
        "mov %1, %%eax\n"
        "test %%eax, %%eax\n"
        "jnz 1f\n"
        "mov $1, %%eax\n"
        "1:\n"
        "mov 0(%0), %%rbx\n"
        "mov 8(%0), %%rsp\n"
        "mov 16(%0), %%rbp\n"
        "mov 24(%0), %%r12\n"
        "mov 32(%0), %%r13\n"
        "mov 40(%0), %%r14\n"
        "mov 48(%0), %%r15\n"
        "jmp *56(%0)\n"
        : : "r"(env), "r"(val) : "memory"
    );
}

#include "string.h"
#include <stdarg.h>

double strtod(const char* str, char** endptr) {
    double res = atof(str);
    if (endptr) {
        *endptr = (char*)str + strlen(str);
    }
    return res;
}

int __isoc99_sscanf(const char* str, const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    int count = 0;
    if (strcmp(format, "%d") == 0) {
        int* val = va_arg(ap, int*);
        *val = atoi(str);
        count = 1;
    } else if (strcmp(format, "%s") == 0) {
        char* dest = va_arg(ap, char*);
        strcpy(dest, str);
        count = 1;
    }
    va_end(ap);
    return count;
}

static int errno_val = 0;
int* __errno_location(void) {
    return &errno_val;
}

static uint16_t ctype_b_table_data[384];
static const uint16_t* ctype_b_table = 0;

const uint16_t** __ctype_b_loc(void) {
    if (!ctype_b_table) {
        for (int i = 0; i < 384; i++) {
            int val = i - 128;
            uint16_t mask = 0;
            if (val == ' ' || val == '\t' || val == '\n' || val == '\r' || val == '\v' || val == '\f') {
                mask |= 0x2000; // _ISspace
            }
            if (val >= 'a' && val <= 'z') {
                mask |= 0x0200; // _ISlower
                mask |= 0x0400; // _ISalpha
                mask |= 0x0008; // _ISalnum
            }
            if (val >= 'A' && val <= 'Z') {
                mask |= 0x0100; // _ISupper
                mask |= 0x0400; // _ISalpha
                mask |= 0x0008; // _ISalnum
            }
            if (val >= '0' && val <= '9') {
                mask |= 0x0800; // _ISdigit
                mask |= 0x0008; // _ISalnum
                mask |= 0x1000; // _ISxdigit
            }
            if ((val >= 'a' && val <= 'f') || (val >= 'A' && val <= 'F')) {
                mask |= 0x1000; // _ISxdigit
            }
            if (val == ' ' || val == '\t') {
                mask |= 0x0001; // _ISblank
            }
            if ((val >= 0 && val < 32) || val == 127) {
                mask |= 0x0002; // _IScntrl
            }
            ctype_b_table_data[i] = mask;
        }
        ctype_b_table = &ctype_b_table_data[128];
    }
    return &ctype_b_table;
}

static int32_t toupper_table_data[384];
static const int32_t* toupper_table = 0;

const int32_t** __ctype_toupper_loc(void) {
    if (!toupper_table) {
        for (int i = 0; i < 384; i++) {
            int val = i - 128;
            if (val >= 'a' && val <= 'z') {
                toupper_table_data[i] = val - 'a' + 'A';
            } else {
                toupper_table_data[i] = val;
            }
        }
        toupper_table = &toupper_table_data[128];
    }
    return &toupper_table;
}

double fabs(double x) {
    return (x < 0) ? -x : x;
}

int system(const char* command) {
    (void)command;
    return -1;
}

int mkdir(const char* pathname, uint32_t mode) {
    (void)pathname; (void)mode;
    return -1;
}

int _setjmp(jmp_buf env) {
    return setjmp(env);
}

unsigned long strtoul(const char* nptr, char** endptr, int base) {
    const char* s = nptr;
    unsigned long acc = 0;
    int c;
    unsigned long any = 0;

    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') {
        s++;
    }

    if (base == 0) {
        if (*s == '0') {
            s++;
            if (*s == 'x' || *s == 'X') {
                s++;
                base = 16;
            } else {
                base = 8;
            }
        } else {
            base = 10;
        }
    } else if (base == 16) {
        if (*s == '0' && (s[1] == 'x' || s[1] == 'X')) {
            s += 2;
        }
    }

    while ((c = *s) != '\0') {
        if (c >= '0' && c <= '9') {
            c -= '0';
        } else if (c >= 'a' && c <= 'z') {
            c -= 'a' - 10;
        } else if (c >= 'A' && c <= 'Z') {
            c -= 'A' - 10;
        } else {
            break;
        }
        if (c >= base) {
            break;
        }
        acc = acc * base + c;
        any = 1;
        s++;
    }

    if (endptr != 0) {
        *endptr = (char*)(any ? s : nptr);
    }

    return acc;
}

static int32_t tolower_table_data[384];
static const int32_t* tolower_table = 0;

const int32_t** __ctype_tolower_loc(void) {
    if (!tolower_table) {
        for (int i = 0; i < 384; i++) {
            int val = i - 128;
            if (val >= 'A' && val <= 'Z') {
                tolower_table_data[i] = val - 'A' + 'a';
            } else {
                tolower_table_data[i] = val;
            }
        }
        tolower_table = &tolower_table_data[128];
    }
    return &tolower_table;
}

double floor(double x) {
    int i = (int)x;
    if (x < 0 && x != (double)i) {
        return (double)(i - 1);
    }
    return (double)i;
}

double pow(double base, double exp) {
    if (exp == 0.0) return 1.0;
    if (exp == 1.0) return base;
    if (exp < 0.0) return 1.0 / pow(base, -exp);
    int iexp = (int)exp;
    if ((double)iexp == exp) {
        double res = 1.0;
        for (int i = 0; i < iexp; i++) res *= base;
        return res;
    }
    return base;
}

long strtol(const char* nptr, char** endptr, int base) {
    const char* s = nptr;
    long acc = 0;
    int c;
    long any = 0;
    int neg = 0;

    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') {
        s++;
    }

    if (*s == '-') {
        neg = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }

    if (base == 0) {
        if (*s == '0') {
            s++;
            if (*s == 'x' || *s == 'X') {
                s++;
                base = 16;
            } else {
                base = 8;
            }
        } else {
            base = 10;
        }
    } else if (base == 16) {
        if (*s == '0' && (s[1] == 'x' || s[1] == 'X')) {
            s += 2;
        }
    }

    while ((c = *s) != '\0') {
        if (c >= '0' && c <= '9') {
            c -= '0';
        } else if (c >= 'a' && c <= 'z') {
            c -= 'a' - 10;
        } else if (c >= 'A' && c <= 'Z') {
            c -= 'A' - 10;
        } else {
            break;
        }
        if (c >= base) {
            break;
        }
        acc = acc * base + c;
        any = 1;
        s++;
    }

    if (endptr != 0) {
        *endptr = (char*)(any ? s : nptr);
    }

    return neg ? -acc : acc;
}
