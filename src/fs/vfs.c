#include "vfs.h"
#include "../libc/string.h"
#include "../drivers/serial.h"
#include "../drivers/ata.h"
#include "../drivers/nvme.h"
#include "../shell/gui.h"
#include "../mm/pmm.h"
#include "../mm/kheap.h"
#include <errno.h>

// File-type bits for st_mode (standard POSIX values: S_IFREG=0100000,
// S_IFCHR=0020000, S_IFBLK=0060000), permission bits kept permissive since
// this OS has no user/permission model.
#define VFS_MODE_REG  0100666
#define VFS_MODE_CHR  0020666
#define VFS_MODE_BLK  0060666

static uint32_t node_mode(vfs_node_t* node) {
    if (strncmp(node->name, "/dev/sda", 8) == 0) return VFS_MODE_BLK;
    if (strncmp(node->name, "/dev/nvme0n1", 12) == 0) return VFS_MODE_BLK;
    if (strncmp(node->name, "/dev/", 5) == 0) return VFS_MODE_CHR;
    return VFS_MODE_REG;
}

#define VFS_MAX_NODES 32

file_desc_t fd_table[MAX_FD];
static vfs_node_t vfs_nodes[VFS_MAX_NODES];
static int vfs_node_count = 0;

// /dev/stdin ring buffer, fed by the keyboard IRQ (see vfs_stdin_push) and
// drained by read(0, ...). Power-of-two size lets index wraparound use a
// simple mask instead of a modulo.
#define STDIN_BUF_SIZE 256
static char stdin_buf[STDIN_BUF_SIZE];
static volatile uint32_t stdin_head = 0; // next slot to write (IRQ context)
static volatile uint32_t stdin_tail = 0; // next slot to read (read() callers)

void vfs_stdin_push(char c) {
    uint32_t next = (stdin_head + 1) & (STDIN_BUF_SIZE - 1);
    if (next == stdin_tail) return; // buffer full: drop the oldest-pending byte's slot
    stdin_buf[stdin_head] = c;
    stdin_head = next;
}

static int dev_null_write(vfs_node_t* node, uint32_t offset, uint32_t size, const uint8_t* buffer);

static int dynamic_mem_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    // Bounded by the logical length, not the allocated capacity -- otherwise
    // reading a file shorter than its capacity returns zeroed padding out to
    // whatever it was first allocated at, instead of stopping at EOF.
    if (offset >= (uint32_t)node->length) return 0;
    uint32_t to_read = (uint32_t)node->length - offset;
    if (to_read > size) to_read = size;
    memcpy(buffer, (uint8_t*)node->private_data + offset, to_read);
    return to_read;
}

void vfs_register_file(const char* name, void* buffer, uint32_t size) {
    if (vfs_node_count >= VFS_MAX_NODES) {
        print_serial("[VFS] Warning: node table full, dropped file: ");
        print_serial(name);
        print_serial("\n");
        return;
    }
    vfs_node_t* node = &vfs_nodes[vfs_node_count++];
    strncpy(node->name, name, sizeof(node->name) - 1);
    node->name[sizeof(node->name) - 1] = '\0';
    node->size = size;
    node->length = size; // fixed content (e.g. a boot module): size == length
    node->read = dynamic_mem_read;
    node->write = dev_null_write; // writing is ignored
    node->private_data = buffer;
}

static int dynamic_mem_write(vfs_node_t* node, uint32_t offset, uint32_t size, const uint8_t* buffer) {
    // Bounds-check the write itself against capacity (size) -- writes are
    // allowed anywhere within the allocated buffer, unlike reads which stop
    // at the logical length.
    if (offset >= (uint32_t)node->size) return 0;
    uint32_t to_write = (uint32_t)node->size - offset;
    if (to_write > size) to_write = size;
    memcpy((uint8_t*)node->private_data + offset, buffer, to_write);
    // Grow the logical length if this write extended past it (never shrink
    // it here -- only O_TRUNC/truncation should do that).
    uint32_t new_end = offset + to_write;
    if (new_end > (uint32_t)node->length) node->length = (int)new_end;
    return to_write;
}

int vfs_create_file(const char* name, uint32_t size) {
    // Idempotent by name: without this check, calling create on a file that
    // already exists (e.g. the code editor's Save button, which calls this
    // unconditionally every click) published a duplicate node every time --
    // permanently leaking a kmalloc'd buffer and a node-table slot per save.
    for (int i = 0; i < vfs_node_count; i++) {
        if (strcmp(vfs_nodes[i].name, name) == 0) {
            return 0;
        }
    }

    if (vfs_node_count >= VFS_MAX_NODES) {
        print_serial("[VFS] Warning: node table full, cannot create file: ");
        print_serial(name);
        print_serial("\n");
        return -1;
    }
    vfs_node_t* node = &vfs_nodes[vfs_node_count];
    strncpy(node->name, name, sizeof(node->name) - 1);
    node->name[sizeof(node->name) - 1] = '\0';
    node->size = size;
    node->length = 0; // newly created file starts empty; size is just capacity
    node->read = dynamic_mem_read;
    node->write = dynamic_mem_write;
    node->private_data = kmalloc(size);
    if (!node->private_data) {
        print_serial("[VFS] Warning: out of memory creating file: ");
        print_serial(name);
        print_serial("\n");
        errno = ENOMEM;
        return -1; // Do not publish a node with no backing storage.
    }
    // memset is handled by kmalloc now, but explicit memset doesn't hurt.
    memset(node->private_data, 0, size);
    vfs_node_count++;
    return 0;
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

static int dev_sda_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    if (size == 0) return 0;
    // Check offset against the limit FIRST: "offset + size > limit" can
    // silently wrap around when offset is near UINT32_MAX (reachable via
    // lseek() with an arbitrary offset), which would let a bogus offset
    // through unclamped and hand the ATA layer a huge/garbage LBA.
    if (offset >= (uint32_t)node->size) return 0;
    if (size > (uint32_t)node->size - offset) {
        size = (uint32_t)node->size - offset;
    }

    uint32_t bytes_read = 0;
    while (bytes_read < size) {
        uint32_t chunk_offset = offset + bytes_read;
        uint32_t chunk_size = size - bytes_read;
        if (chunk_size > 32768) {
            chunk_size = 32768;
        }

        uint32_t lba = chunk_offset / 512;
        uint32_t end_lba = (chunk_offset + chunk_size - 1) / 512;
        uint32_t count = end_lba - lba + 1;

        void* temp = kmalloc(count * 512);
        if (!temp) return bytes_read > 0 ? (int)bytes_read : -1;

        if (ata_read_sectors(lba, (uint8_t)count, (uint32_t*)temp) < 0) {
            kfree(temp);
            return bytes_read > 0 ? (int)bytes_read : -1;
        }

        uint32_t off_in_sector = chunk_offset % 512;
        memcpy(buffer + bytes_read, (uint8_t*)temp + off_in_sector, chunk_size);
        kfree(temp);

        bytes_read += chunk_size;
    }
    return (int)bytes_read;
}

static int dev_sda_write(vfs_node_t* node, uint32_t offset, uint32_t size, const uint8_t* buffer) {
    if (size == 0) return 0;
    // Same overflow-safe ordering as dev_sda_read: check the offset bound
    // before ever adding offset+size.
    if (offset >= (uint32_t)node->size) return -1;
    if (size > (uint32_t)node->size - offset) {
        size = (uint32_t)node->size - offset;
    }

    uint32_t bytes_written = 0;
    while (bytes_written < size) {
        uint32_t chunk_offset = offset + bytes_written;
        uint32_t chunk_size = size - bytes_written;
        if (chunk_size > 32768) {
            chunk_size = 32768;
        }

        uint32_t lba = chunk_offset / 512;
        uint32_t end_lba = (chunk_offset + chunk_size - 1) / 512;
        uint32_t count = end_lba - lba + 1;

        void* temp = kmalloc(count * 512);
        if (!temp) return bytes_written > 0 ? (int)bytes_written : -1;

        if ((chunk_offset % 512 != 0) || (chunk_size < count * 512)) {
            if (ata_read_sectors(lba, (uint8_t)count, (uint32_t*)temp) < 0) {
                kfree(temp);
                return bytes_written > 0 ? (int)bytes_written : -1;
            }
        }

        uint32_t off_in_sector = chunk_offset % 512;
        memcpy((uint8_t*)temp + off_in_sector, buffer + bytes_written, chunk_size);

        if (ata_write_sectors(lba, (uint8_t)count, (const uint32_t*)temp) < 0) {
            kfree(temp);
            return bytes_written > 0 ? (int)bytes_written : -1;
        }

        kfree(temp);
        bytes_written += chunk_size;
    }
    return (int)bytes_written;
}

// Same shape as dev_sda_read/write, but driven by the namespace's actual
// LBA size (512 or 4096 bytes) instead of ATA's fixed 512, since NVMe
// namespaces are commonly formatted 4Kn.
static int dev_nvme_read(vfs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    if (size == 0) return 0;
    uint32_t sector_size = nvme_get_sector_size();
    if (sector_size == 0) return -1;

    if (offset >= (uint32_t)node->size) return 0;
    if (size > (uint32_t)node->size - offset) {
        size = (uint32_t)node->size - offset;
    }

    uint32_t bytes_read = 0;
    while (bytes_read < size) {
        uint32_t chunk_offset = offset + bytes_read;
        uint32_t chunk_size = size - bytes_read;
        if (chunk_size > 32768) {
            chunk_size = 32768;
        }

        uint64_t lba = chunk_offset / sector_size;
        uint64_t end_lba = (chunk_offset + chunk_size - 1) / sector_size;
        uint32_t count = (uint32_t)(end_lba - lba + 1);

        void* temp = kmalloc((size_t)count * sector_size);
        if (!temp) return bytes_read > 0 ? (int)bytes_read : -1;

        if (nvme_read_sectors(lba, count, temp) < 0) {
            kfree(temp);
            return bytes_read > 0 ? (int)bytes_read : -1;
        }

        uint32_t off_in_sector = chunk_offset % sector_size;
        memcpy(buffer + bytes_read, (uint8_t*)temp + off_in_sector, chunk_size);
        kfree(temp);

        bytes_read += chunk_size;
    }
    return (int)bytes_read;
}

static int dev_nvme_write(vfs_node_t* node, uint32_t offset, uint32_t size, const uint8_t* buffer) {
    if (size == 0) return 0;
    uint32_t sector_size = nvme_get_sector_size();
    if (sector_size == 0) return -1;

    if (offset >= (uint32_t)node->size) return -1;
    if (size > (uint32_t)node->size - offset) {
        size = (uint32_t)node->size - offset;
    }

    uint32_t bytes_written = 0;
    while (bytes_written < size) {
        uint32_t chunk_offset = offset + bytes_written;
        uint32_t chunk_size = size - bytes_written;
        if (chunk_size > 32768) {
            chunk_size = 32768;
        }

        uint64_t lba = chunk_offset / sector_size;
        uint64_t end_lba = (chunk_offset + chunk_size - 1) / sector_size;
        uint32_t count = (uint32_t)(end_lba - lba + 1);

        void* temp = kmalloc((size_t)count * sector_size);
        if (!temp) return bytes_written > 0 ? (int)bytes_written : -1;

        if ((chunk_offset % sector_size != 0) || (chunk_size < count * sector_size)) {
            if (nvme_read_sectors(lba, count, temp) < 0) {
                kfree(temp);
                return bytes_written > 0 ? (int)bytes_written : -1;
            }
        }

        uint32_t off_in_sector = chunk_offset % sector_size;
        memcpy((uint8_t*)temp + off_in_sector, buffer + bytes_written, chunk_size);

        if (nvme_write_sectors(lba, count, temp) < 0) {
            kfree(temp);
            return bytes_written > 0 ? (int)bytes_written : -1;
        }

        kfree(temp);
        bytes_written += chunk_size;
    }
    return (int)bytes_written;
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

    // Only publish /dev/sda if ata_init() actually found a real drive.
    // Previously this always claimed a 100MB mock size regardless of whether
    // a disk was attached, so reads/writes past whatever was really backing
    // it (or past nothing at all) would issue bogus ATA commands instead of
    // cleanly failing at the VFS layer.
    if (ata_disk_present()) {
        vfs_node_t* dev_sda = &vfs_nodes[vfs_node_count++];
        strcpy(dev_sda->name, "/dev/sda");
        dev_sda->read = dev_sda_read;
        dev_sda->write = dev_sda_write;
        dev_sda->size = (int)(ata_get_sector_count() * 512ULL);
        dev_sda->length = dev_sda->size; // raw block device: whole disk is always addressable
    }

    // Only publish /dev/nvme0n1 if nvme_init() found a controller with a
    // usable namespace. node->size/length are 32-bit int (see vfs.h), so a
    // namespace larger than INT32_MAX bytes is clamped here instead of
    // silently wrapping negative and defeating the offset bounds checks in
    // dev_nvme_read/write above.
    if (nvme_disk_present()) {
        uint64_t total_bytes = nvme_get_sector_count() * (uint64_t)nvme_get_sector_size();
        if (total_bytes > 0x7FFFFFFFULL) total_bytes = 0x7FFFFFFFULL;

        vfs_node_t* dev_nvme = &vfs_nodes[vfs_node_count++];
        strcpy(dev_nvme->name, "/dev/nvme0n1");
        dev_nvme->read = dev_nvme_read;
        dev_nvme->write = dev_nvme_write;
        dev_nvme->size = (int)total_bytes;
        dev_nvme->length = dev_nvme->size;
    }

    print_serial("[DragonOS] POSIX Virtual File System initialized.\n");
}

int open(const char* pathname, int flags) {
    int found_index = -1;
    for (int i = 0; i < vfs_node_count; i++) {
        if (strcmp(vfs_nodes[i].name, pathname) == 0) {
            found_index = i;
            break;
        }
    }

    // Fallback for Quake 1 shareware PAK file mapped via Limine ISO module
    if (found_index == -1 && (strcmp(pathname, "id1/pak0.pak") == 0 || strcmp(pathname, "./id1/pak0.pak") == 0)) {
        for (int i = 0; i < vfs_node_count; i++) {
            if (strcmp(vfs_nodes[i].name, "pak0.pak") == 0 || strcmp(vfs_nodes[i].name, "/boot/pak0.pak") == 0) {
                found_index = i;
                break;
            }
        }
    }

    if (found_index == -1) {
        if (flags & O_CREAT) {
            // Create a 64KB file in RAM by default (standard buffer size).
            // If creation fails (node table full or OOM), fall through to the
            // "not found" path instead of silently opening whatever file
            // happened to be last in the table.
            if (vfs_create_file(pathname, 64 * 1024) != 0) {
                return -1; // errno already set by vfs_create_file
            }
            found_index = vfs_node_count - 1;
        } else {
            errno = ENOENT;
            return -1; // File not found
        }
    }

    vfs_node_t* node = &vfs_nodes[found_index];

    if (flags & O_TRUNC) {
        if (node->private_data) {
            memset(node->private_data, 0, node->size);
        }
        node->length = 0; // the file is now logically empty, not just zeroed
    }

    // Find free File Descriptor
    for (int fd = 3; fd < MAX_FD; fd++) {
        if (!fd_table[fd].used) {
            fd_table[fd].node = node;
            // Append at the actual content end (length), not the allocated
            // capacity (size) -- appending used to seek past the real data
            // into the unused tail of the buffer, leaving a gap of stale/zero
            // bytes before whatever got written next.
            fd_table[fd].offset = (flags & O_APPEND) ? (uint32_t)node->length : 0;
            fd_table[fd].flags = flags;
            fd_table[fd].used = 1;
            return fd;
        }
    }
    errno = EMFILE;
    return -1; // No free file descriptors
}

int unlink(const char* pathname) {
    for (int i = 0; i < vfs_node_count; i++) {
        if (strcmp(vfs_nodes[i].name, pathname) == 0) {
            vfs_node_t* node = &vfs_nodes[i];
            if (node->private_data && node->write == dynamic_mem_write) {
                kfree(node->private_data);
            }
            
            // Remove from vfs_nodes by shifting
            for (int j = i; j < vfs_node_count - 1; j++) {
                vfs_nodes[j] = vfs_nodes[j + 1];
            }
            vfs_node_count--;

            // Close all active file descriptors using this node
            for (int fd = 0; fd < MAX_FD; fd++) {
                if (fd_table[fd].used && fd_table[fd].node == node) {
                    fd_table[fd].used = 0;
                }
            }
            return 0;
        }
    }
    errno = ENOENT;
    return -1;
}

int close(int fd) {
    if (fd < 0 || fd >= MAX_FD || !fd_table[fd].used) { errno = EBADF; return -1; }
    fd_table[fd].used = 0;
    return 0;
}

int read(int fd, void* buf, int count) {
    if (fd < 0 || fd >= MAX_FD || !fd_table[fd].used) { errno = EBADF; return -1; }
    file_desc_t* f = &fd_table[fd];
    
    if (f->node == &stdin_node) {
        // Non-blocking: drain whatever the keyboard IRQ has queued so far.
        // stdin_head is written from IRQ context; re-reading it each
        // iteration (rather than snapshotting once) is safe since we only
        // ever compare it for equality against our own advancing tail.
        int n = 0;
        while (n < count && stdin_tail != stdin_head) {
            ((char*)buf)[n++] = stdin_buf[stdin_tail];
            stdin_tail = (stdin_tail + 1) & (STDIN_BUF_SIZE - 1);
        }
        return n;
    }
    
    if (!f->node->read) { errno = EBADF; return -1; }

    int bytes_read = f->node->read(f->node, f->offset, count, (uint8_t*)buf);
    if (bytes_read > 0) {
        f->offset += bytes_read;
    }
    return bytes_read;
}

int write(int fd, const void* buf, int count) {
    if (fd < 0 || fd >= MAX_FD || !fd_table[fd].used) { errno = EBADF; return -1; }
    file_desc_t* f = &fd_table[fd];
    
    if (f->node == &stdout_node) {
        // Output to GUI Terminal console
        // Terminate string for safety
        char temp[4096];
        int to_copy = (count >= 4095) ? 4095 : count;
        memcpy(temp, buf, to_copy);
        temp[to_copy] = '\0';
        gui_write_string(temp);
        return to_copy;
    }
    
    if (f->node == &stderr_node) {
        // Output to serial COM1 debug console
        char temp[4096];
        int to_copy = (count >= 4095) ? 4095 : count;
        memcpy(temp, buf, to_copy);
        temp[to_copy] = '\0';
        print_serial(temp);
        return to_copy;
    }
    
    if (!f->node->write) { errno = EBADF; return -1; }

    int bytes_written = f->node->write(f->node, f->offset, count, (const uint8_t*)buf);
    if (bytes_written > 0) {
        f->offset += bytes_written;
    }
    return bytes_written;
}

int lseek(int fd, int offset, int whence) {
    if (fd < 0 || fd >= MAX_FD || !fd_table[fd].used) { errno = EBADF; return -1; }
    file_desc_t* f = &fd_table[fd];

    if (whence == 0) { // SEEK_SET
        f->offset = offset;
    } else if (whence == 1) { // SEEK_CUR
        f->offset += offset;
    } else if (whence == 2) { // SEEK_END
        // The logical content length, not the allocated capacity -- seeking
        // to "the end" of a file used to land past the real data, in the
        // unused tail of its pre-allocated buffer.
        f->offset = f->node->length + offset;
    } else {
        errno = EINVAL;
        return -1;
    }
    return f->offset;
}

int stat(const char* pathname, struct stat* statbuf) {
    if (!statbuf) { errno = EFAULT; return -1; }
    for (int i = 0; i < vfs_node_count; i++) {
        if (strcmp(vfs_nodes[i].name, pathname) == 0) {
            memset(statbuf, 0, sizeof(struct stat));
            statbuf->st_size = vfs_nodes[i].length;
            statbuf->st_mode = node_mode(&vfs_nodes[i]);
            return 0;
        }
    }
    errno = ENOENT;
    return -1;
}

int fstat(int fd, struct stat* statbuf) {
    if (!statbuf) { errno = EFAULT; return -1; }
    if (fd < 0 || fd >= MAX_FD || !fd_table[fd].used) { errno = EBADF; return -1; }
    memset(statbuf, 0, sizeof(struct stat));
    statbuf->st_size = fd_table[fd].node->length;
    statbuf->st_mode = node_mode(fd_table[fd].node);
    return 0;
}

int access(const char* pathname, int mode) {
    (void)mode; // no user/permission model to check R_OK/W_OK/X_OK against
    for (int i = 0; i < vfs_node_count; i++) {
        if (strcmp(vfs_nodes[i].name, pathname) == 0) {
            return 0;
        }
    }
    errno = ENOENT;
    return -1;
}

int vfs_get_count(void) {
    return vfs_node_count;
}

vfs_node_t* vfs_get_node(int index) {
    if (index >= 0 && index < vfs_node_count) {
        return &vfs_nodes[index];
    }
    return NULL;
}
