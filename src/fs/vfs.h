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
} vfs_node_t;

typedef struct {
    vfs_node_t* node;
    uint32_t offset;
    int flags;
    int used;
} file_desc_t;

void init_vfs(void);

// POSIX System Calls Emulation
int open(const char* pathname, int flags);
int close(int fd);
int read(int fd, void* buf, int count);
int write(int fd, const void* buf, int count);

#endif
