#include "vfs.h"
#include "../libc/string.h"
#include "../drivers/serial.h"
#include "../shell/gui.h"
#include "../mm/pmm.h"

static file_desc_t fd_table[MAX_FD];
static vfs_node_t vfs_nodes[32];
static int vfs_node_count = 0;

static int dev_null_write(vfs_node_t* node, uint32_t offset, uint32_t size, const uint8_t* buffer);

static int dynamic_mem_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    if (offset >= (uint32_t)node->size) return 0;
    uint32_t to_read = (uint32_t)node->size - offset;
    if (to_read > size) to_read = size;
    memcpy(buffer, (uint8_t*)node->private_data + offset, to_read);
    return to_read;
}

void vfs_register_file(const char* name, void* buffer, uint32_t size) {
    if (vfs_node_count >= 32) return;
    vfs_node_t* node = &vfs_nodes[vfs_node_count++];
    strcpy(node->name, name);
    node->size = size;
    node->read = dynamic_mem_read;
    node->write = dev_null_write; // writing is ignored
    node->private_data = buffer;
}

// Standard stream nodes
static vfs_node_t stdin_node;
static vfs_node_t stdout_node;
static vfs_node_t stderr_node;

// Mock virtual file operations
static int dev_null_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    (void)node; (void)offset; (void)size; (void)buffer;
    return 0;
}

static int dev_null_write(vfs_node_t* node, uint32_t offset, uint32_t size, const uint8_t* buffer) {
    (void)node; (void)offset; (void)buffer;
    return size;
}

static int dev_random_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    (void)node; (void)offset;
    static uint32_t seed = 0x12345678;
    for (uint32_t i = 0; i < size; i++) {
        seed = seed * 1103515245 + 12345;
        buffer[i] = (uint8_t)(seed / 65536) % 256;
    }
    return size;
}

static int sys_cpuinfo_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    (void)node;
    const char* cpuinfo = "processor\t: 0\nvendor_id\t: GenuineIntel\ncpu family\t: 6\nmodel name\t: DragonOS 64-bit Core Emulator\n";
    uint32_t len = strlen(cpuinfo);
    if (offset >= len) return 0;
    uint32_t to_read = len - offset;
    if (to_read > size) to_read = size;
    memcpy(buffer, cpuinfo + offset, to_read);
    return to_read;
}

static int sys_meminfo_read(vfs_node_t* node, uint32_t node_offset, uint32_t size, uint8_t* buffer) {
    (void)node;
    char meminfo[256];
    char num_buf[32];
    
    strcpy(meminfo, "MemTotal: ");
    int_to_ascii(pmm_total_memory / 1024, num_buf);
    strcat(meminfo, num_buf);
    strcat(meminfo, " kB\nMemFree: ");
    int_to_ascii(pmm_free_memory / 1024, num_buf);
    strcat(meminfo, num_buf);
    strcat(meminfo, " kB\nMemUsed: ");
    int_to_ascii(pmm_used_memory / 1024, num_buf);
    strcat(meminfo, num_buf);
    strcat(meminfo, " kB\n");
    
    uint32_t len = strlen(meminfo);
    if (node_offset >= len) return 0;
    uint32_t to_read = len - node_offset;
    if (to_read > size) to_read = size;
    memcpy(buffer, meminfo + node_offset, to_read);
    return to_read;
}

void init_vfs(void) {
    memset(fd_table, 0, sizeof(fd_table));
    
    // Set up standard streams
    strcpy(stdin_node.name, "/dev/stdin");
    strcpy(stdout_node.name, "/dev/stdout");
    strcpy(stderr_node.name, "/dev/stderr");
    
    fd_table[0].node = &stdin_node;
    fd_table[0].used = 1;
    fd_table[1].node = &stdout_node;
    fd_table[1].used = 1;
    fd_table[2].node = &stderr_node;
    fd_table[2].used = 1;

    // Create Virtual files
    vfs_node_t* dev_null = &vfs_nodes[vfs_node_count++];
    strcpy(dev_null->name, "/dev/null");
    dev_null->read = dev_null_read;
    dev_null->write = dev_null_write;

    vfs_node_t* dev_random = &vfs_nodes[vfs_node_count++];
    strcpy(dev_random->name, "/dev/random");
    dev_random->read = dev_random_read;
    dev_random->write = dev_null_write;

    vfs_node_t* sys_cpuinfo = &vfs_nodes[vfs_node_count++];
    strcpy(sys_cpuinfo->name, "/sys/cpuinfo");
    sys_cpuinfo->read = sys_cpuinfo_read;
    sys_cpuinfo->write = dev_null_write;

    vfs_node_t* sys_meminfo = &vfs_nodes[vfs_node_count++];
    strcpy(sys_meminfo->name, "/sys/meminfo");
    sys_meminfo->read = sys_meminfo_read;
    sys_meminfo->write = dev_null_write;
    
    print_serial("[DragonOS] POSIX Virtual File System initialized.\n");
}

int open(const char* pathname, int flags) {
    for (int i = 0; i < vfs_node_count; i++) {
        if (strcmp(vfs_nodes[i].name, pathname) == 0) {
            // Find free File Descriptor
            for (int fd = 3; fd < MAX_FD; fd++) {
                if (!fd_table[fd].used) {
                    fd_table[fd].node = &vfs_nodes[i];
                    fd_table[fd].offset = 0;
                    fd_table[fd].flags = flags;
                    fd_table[fd].used = 1;
                    return fd;
                }
            }
            return -1; // No free file descriptors
        }
    }
    return -1; // File not found
}

int close(int fd) {
    if (fd < 0 || fd >= MAX_FD || !fd_table[fd].used) return -1;
    fd_table[fd].used = 0;
    return 0;
}

int read(int fd, void* buf, int count) {
    if (fd < 0 || fd >= MAX_FD || !fd_table[fd].used) return -1;
    file_desc_t* f = &fd_table[fd];
    
    if (f->node == &stdin_node) {
        // Simple polling keyboard read for standard input emulation
        // Return 0 as non-blocking read
        return 0;
    }
    
    if (!f->node->read) return -1;
    
    int bytes_read = f->node->read(f->node, f->offset, count, (uint8_t*)buf);
    if (bytes_read > 0) {
        f->offset += bytes_read;
    }
    return bytes_read;
}

int write(int fd, const void* buf, int count) {
    if (fd < 0 || fd >= MAX_FD || !fd_table[fd].used) return -1;
    file_desc_t* f = &fd_table[fd];
    
    if (f->node == &stdout_node) {
        // Output to GUI Terminal console
        // Terminate string for safety
        char temp[512];
        int to_copy = (count >= 511) ? 511 : count;
        memcpy(temp, buf, to_copy);
        temp[to_copy] = '\0';
        gui_write_string(temp);
        return to_copy;
    }
    
    if (f->node == &stderr_node) {
        // Output to serial COM1 debug console
        char temp[512];
        int to_copy = (count >= 511) ? 511 : count;
        memcpy(temp, buf, to_copy);
        temp[to_copy] = '\0';
        print_serial(temp);
        return to_copy;
    }
    
    if (!f->node->write) return -1;
    
    int bytes_written = f->node->write(f->node, f->offset, count, (const uint8_t*)buf);
    if (bytes_written > 0) {
        f->offset += bytes_written;
    }
    return bytes_written;
}

int lseek(int fd, int offset, int whence) {
    if (fd < 0 || fd >= MAX_FD || !fd_table[fd].used) return -1;
    file_desc_t* f = &fd_table[fd];
    
    if (whence == 0) { // SEEK_SET
        f->offset = offset;
    } else if (whence == 1) { // SEEK_CUR
        f->offset += offset;
    } else if (whence == 2) { // SEEK_END
        f->offset = f->node->size + offset;
    } else {
        return -1;
    }
    return f->offset;
}

int stat(const char* pathname, struct stat* statbuf) {
    if (!statbuf) return -1;
    for (int i = 0; i < vfs_node_count; i++) {
        if (strcmp(vfs_nodes[i].name, pathname) == 0) {
            memset(statbuf, 0, sizeof(struct stat));
            statbuf->st_size = vfs_nodes[i].size;
            statbuf->st_mode = 0100666; // S_IFREG | 0666
            return 0;
        }
    }
    return -1;
}

int fstat(int fd, struct stat* statbuf) {
    if (!statbuf) return -1;
    if (fd < 0 || fd >= MAX_FD || !fd_table[fd].used) return -1;
    memset(statbuf, 0, sizeof(struct stat));
    statbuf->st_size = fd_table[fd].node->size;
    statbuf->st_mode = 0100666;
    return 0;
}

int access(const char* pathname, int mode) {
    (void)mode;
    for (int i = 0; i < vfs_node_count; i++) {
        if (strcmp(vfs_nodes[i].name, pathname) == 0) {
            return 0;
        }
    }
    return -1;
}
