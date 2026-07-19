#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include <stddef.h>

#define MAX_FD 32

typedef struct vfs_node {
    char name[32];
    int size;
    int (*read)(struct vfs_node* node, uint32_t offset, uint32_t size, uint8_t* buffer);
    int (*write)(struct vfs_node* node, uint32_t offset, uint32_t size, const uint8_t* buffer);
    void* private_data;
} vfs_node_t;

typedef struct {
    vfs_node_t* node;
    uint32_t offset;
    int flags;
    int used;
} file_desc_t;

void init_vfs(void);
void vfs_register_file(const char* name, void* buffer, uint32_t size);

// POSIX System Calls Emulation
int open(const char* pathname, int flags);
int close(int fd);
int read(int fd, void* buf, int count);
int write(int fd, const void* buf, int count);
int lseek(int fd, int offset, int whence);

struct stat {
    uint32_t st_dev;
    uint32_t st_ino;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t st_rdev;
    uint64_t st_size;
    uint64_t st_atime;
    uint64_t st_mtime;
    uint64_t st_ctime;
};

int stat(const char* pathname, struct stat* statbuf);
int fstat(int fd, struct stat* statbuf);
int access(const char* pathname, int mode);

#endif
