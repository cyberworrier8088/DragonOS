#include "stdio.h"
#include "../fs/vfs.h"
#include "../libc/string.h"
#include "../drivers/serial.h"
#include "../mm/kheap.h"

FILE* fopen(const char* filename, const char* mode) {
    (void)mode;
    // Map relative filename "doom1.wad" to "/boot/doom1.wad" if needed
    char path[64];
    if (filename[0] != '/') {
        strcpy(path, "/boot/");
        strcat(path, filename);
    } else {
        strcpy(path, filename);
    }

    int fd = open(path, 0);
    if (fd < 0) {
        // Fallback to checking exact path
        fd = open(filename, 0);
    }
    
    if (fd < 0) return 0;
    
    // We allocate a small block on heap to store the descriptor
    int* fd_ptr = (int*)kmalloc(sizeof(int));
    *fd_ptr = fd;
    return (FILE*)fd_ptr;
}

int fclose(FILE* stream) {
    if (!stream) return -1;
    int fd = *(int*)stream;
    close(fd);
    kfree(stream);
    return 0;
}

size_t fread(void* ptr, size_t size, size_t nmemb, FILE* stream) {
    if (!stream) return 0;
    int fd = *(int*)stream;
    int bytes = read(fd, ptr, size * nmemb);
    if (bytes < 0) return 0;
    return bytes / size;
}

size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* stream) {
    if (!stream) return 0;
    int fd = *(int*)stream;
    int bytes = write(fd, ptr, size * nmemb);
    if (bytes < 0) return 0;
    return bytes / size;
}

int fseek(FILE* stream, long offset, int whence) {
    if (!stream) return -1;
    int fd = *(int*)stream;
    lseek(fd, offset, whence);
    return 0;
}

long ftell(FILE* stream) {
    if (!stream) return -1;
    int fd = *(int*)stream;
    return lseek(fd, 0, SEEK_CUR);
}

static void long_to_ascii(long n, char str[]) {
    int i;
    long sign;
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

    int len = i;
    for (int a = 0, b = len - 1; a < b; a++, b--) {
        char temp = str[a];
        str[a] = str[b];
        str[b] = temp;
    }
}

// Simple vsprintf implementation supporting %d, %s, %c, %x, %u
int vsprintf(char* str, const char* format, va_list ap) {
    char* d = str;
    const char* f = format;

    while (*f) {
        if (*f == '%') {
            f++;

            int zero_pad = 0;
            int width = 0;
            int precision = -1;

            // Check for zero padding flag
            if (*f == '0') {
                zero_pad = 1;
                f++;
            }

            // Parse width
            while (*f >= '0' && *f <= '9') {
                width = width * 10 + (*f - '0');
                f++;
            }

            // Parse precision
            if (*f == '.') {
                f++;
                precision = 0;
                while (*f >= '0' && *f <= '9') {
                    precision = precision * 10 + (*f - '0');
                    f++;
                }
            }

            int is_long = 0;
            if (*f == 'l') {
                is_long = 1;
                f++;
            }

            // Format type
            if (*f == 'd' || *f == 'i') {
                long val = is_long ? va_arg(ap, long) : va_arg(ap, int);
                char num[64];
                long_to_ascii(val, num);

                int len = strlen(num);
                int neg = 0;
                char* p = num;
                if (num[0] == '-') {
                    neg = 1;
                    p++;
                    len--;
                }

                // Determine padding target size
                int target = len;
                if (precision > target) target = precision;
                if (width > target + neg && zero_pad) target = width - neg;

                // Write negative sign if applicable
                if (neg) {
                    *d++ = '-';
                }

                // Write leading zeros
                for (int i = len; i < target; i++) {
                    *d++ = '0';
                }

                // Write actual digits
                strcpy(d, p);
                d += len;

            } else if (*f == 'u') {
                unsigned long val = is_long ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int);
                char num[64];
                int idx = 0;
                if (val == 0) {
                    num[idx++] = '0';
                } else {
                    while (val > 0) {
                        num[idx++] = (val % 10) + '0';
                        val /= 10;
                    }
                }
                num[idx] = '\0';
                // reverse
                for (int a = 0, b = idx - 1; a < b; a++, b--) {
                    char temp = num[a];
                    num[a] = num[b];
                    num[b] = temp;
                }

                int len = idx;
                int target = len;
                if (precision > target) target = precision;
                if (width > target && zero_pad) target = width;

                for (int i = len; i < target; i++) {
                    *d++ = '0';
                }
                strcpy(d, num);
                d += len;

            } else if (*f == 'x') {
                unsigned long val = is_long ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int);
                char hex[64];
                int idx = 0;
                if (val == 0) {
                    hex[idx++] = '0';
                } else {
                    while (val > 0) {
                        int rem = val % 16;
                        hex[idx++] = (rem < 10) ? (rem + '0') : (rem - 10 + 'a');
                        val /= 16;
                    }
                }
                hex[idx] = '\0';
                // reverse hex
                for (int i = 0, j = idx - 1; i < j; i++, j--) {
                    char temp = hex[i];
                    hex[i] = hex[j];
                    hex[j] = temp;
                }

                int len = idx;
                int target = len;
                if (precision > target) target = precision;
                if (width > target && zero_pad) target = width;

                for (int i = len; i < target; i++) {
                    *d++ = '0';
                }
                strcpy(d, hex);
                d += len;
                
            } else if (*f == 'p') {
                unsigned long val = (unsigned long)va_arg(ap, void*);
                *d++ = '0'; *d++ = 'x';
                char hex[20];
                int idx = 0;
                if (val == 0) {
                    hex[idx++] = '0';
                } else {
                    while (val > 0) {
                        int rem = val % 16;
                        hex[idx++] = (rem < 10) ? (rem + '0') : (rem - 10 + 'a');
                        val /= 16;
                    }
                }
                hex[idx] = '\0';
                for (int i = 0, j = idx - 1; i < j; i++, j--) {
                    char temp = hex[i];
                    hex[i] = hex[j];
                    hex[j] = temp;
                }
                strcpy(d, hex);
                d += idx;

            } else if (*f == 's') {
                char* s = va_arg(ap, char*);
                if (!s) s = "(null)";
                int slen = strlen(s);
                if (precision >= 0 && precision < slen) slen = precision;
                /* Right-pad with spaces if width > slen */
                for (int i = slen; i < width; i++) *d++ = ' ';
                for (int i = 0; i < slen; i++) *d++ = s[i];
            } else if (*f == 'c') {
                char c = (char)va_arg(ap, int);
                *d++ = c;
            } else if (*f == '%') {
                *d++ = '%';
            } else {
                // Unknown format, copy raw
                *d++ = '%';
                if (is_long) *d++ = 'l';
                *d++ = *f;
            }
        } else {
            *d++ = *f;
        }
        f++;
    }
    *d = '\0';
    return d - str;
}

int sprintf(char* str, const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    int res = vsprintf(str, format, ap);
    va_end(ap);
    return res;
}

int printf(const char* format, ...) {
    char buf[4096];
    va_list ap;
    va_start(ap, format);
    int res = vsprintf(buf, format, ap);
    va_end(ap);
    write(1, buf, res); // stdout
    return res;
}

int fprintf(FILE* stream, const char* format, ...) {
    char buf[4096];
    va_list ap;
    va_start(ap, format);
    int res = vsprintf(buf, format, ap);
    va_end(ap);
    
    int fd = stream ? *(int*)stream : 1;
    write(fd, buf, res);
    return res;
}

static int fd_in = 0;
static int fd_out = 1;
static int fd_err = 2;
FILE* stdin = (FILE*)&fd_in;
FILE* stdout = (FILE*)&fd_out;
FILE* stderr = (FILE*)&fd_err;

int vsnprintf(char* str, size_t size, const char* format, va_list ap) {
    char temp[1024];
    int res = vsprintf(temp, format, ap);
    if (res >= 0) {
        size_t to_copy = (size_t)res;
        if (to_copy >= size) to_copy = size - 1;
        memcpy(str, temp, to_copy);
        str[to_copy] = '\0';
        return to_copy;
    }
    return -1;
}

int snprintf(char* str, size_t size, const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    int res = vsnprintf(str, size, format, ap);
    va_end(ap);
    return res;
}

int puts(const char* s) {
    int len = strlen(s);
    write(1, s, len);
    write(1, "\n", 1);
    return len + 1;
}

int putc(int c, FILE* stream) {
    char ch = (char)c;
    int fd = stream ? *(int*)stream : 1;
    write(fd, &ch, 1);
    return c;
}

int remove(const char* pathname) {
    (void)pathname;
    return -1;
}

int rename(const char* oldpath, const char* newpath) {
    (void)oldpath; (void)newpath;
    return -1;
}

int fflush(FILE* stream) {
    (void)stream;
    return 0;
}

int vfprintf(FILE* stream, const char* format, va_list ap) {
    char buf[512];
    int res = vsprintf(buf, format, ap);
    if (res > 0) {
        int fd = stream ? *(int*)stream : 1;
        write(fd, buf, res);
    }
    return res;
}

int feof(FILE* stream) {
    if (!stream) return 1;
    int fd = *(int*)stream;
    extern file_desc_t fd_table[];
    if (fd < 0 || fd >= MAX_FD || !fd_table[fd].used) return 1;
    return fd_table[fd].offset >= fd_table[fd].node->size;
}

int ferror(FILE* stream) {
    (void)stream;
    return 0;
}

void clearerr(FILE* stream) {
    (void)stream;
}

int getc(FILE* stream) {
    unsigned char c;
    if (fread(&c, 1, 1, stream) == 1) return c;
    return -1; // EOF
}

int ungetc(int c, FILE* stream) {
    if (!stream || c == -1) return -1;
    int fd = *(int*)stream;
    extern file_desc_t fd_table[];
    if (fd < 0 || fd >= MAX_FD || !fd_table[fd].used) return -1;
    if (fd_table[fd].offset > 0) {
        fd_table[fd].offset--;
    }
    return c;
}

char* fgets(char* str, int num, FILE* stream) {
    if (!str || num <= 0 || !stream) return 0;
    int i = 0;
    while (i < num - 1) {
        int c = getc(stream);
        if (c == -1) {
            if (i == 0) return 0;
            break;
        }
        str[i++] = (char)c;
        if (c == '\n') break;
    }
    str[i] = '\0';
    return str;
}

FILE* freopen(const char* filename, const char* mode, FILE* stream) {
    (void)mode; (void)stream;
    return fopen(filename, mode);
}

char* strerror(int errnum) {
    (void)errnum;
    return "Unknown error";
}

int __isoc99_fscanf(FILE* stream, const char* format, ...) {
    (void)stream; (void)format;
    return -1;
}
