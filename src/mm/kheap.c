#include "kheap.h"
#include "pmm.h"
#include "../libc/string.h"
#include "../drivers/serial.h"

typedef struct heap_block {
    size_t size;
    int free;
    struct heap_block* next;
} heap_block_t;

static heap_block_t* heap_head = 0;

void kheap_init(void) {
    // Initial heap allocation (4MB, which fits Buddy Allocator maximum order of 10)
    void* initial_pages = pmm_alloc_pages(1024);
    if (!initial_pages) {
        print_serial("[kheap] Fatal: failed to allocate initial heap pages!\n");
        return;
    }
    heap_head = (heap_block_t*)initial_pages;
    heap_head->size = 1024 * PAGE_SIZE - sizeof(heap_block_t);
    heap_head->free = 1;
    heap_head->next = 0;
}

void* kmalloc(size_t size) {
    if (size == 0) return 0;
    
    // 8-byte alignment
    if (size % 8 != 0) {
        size += 8 - (size % 8);
    }

    heap_block_t* curr = heap_head;
    while (curr) {
        if (curr->free && curr->size >= size) {
            // Found a block. Can we split it?
            if (curr->size > size + sizeof(heap_block_t) + 8) {
                heap_block_t* new_block = (heap_block_t*)((uint8_t*)curr + sizeof(heap_block_t) + size);
                new_block->size = curr->size - size - sizeof(heap_block_t);
                new_block->free = 1;
                new_block->next = curr->next;
                
                curr->size = size;
                curr->next = new_block;
            }
            curr->free = 0;
            return (void*)((uint8_t*)curr + sizeof(heap_block_t));
        }
        curr = curr->next;
    }

    // Need more memory - allocate a chunk that is at least large enough for the requested size
    uint64_t pages_needed = (size + sizeof(heap_block_t) + PAGE_SIZE - 1) / PAGE_SIZE;
    if (pages_needed < 256) {
        pages_needed = 256; // Allocate at least 1MB to prevent fragmenting PMM
    }

    void* new_pages = pmm_alloc_pages(pages_needed);
    if (!new_pages) return 0; // OOM

    heap_block_t* new_block = (heap_block_t*)new_pages;
    new_block->size = pages_needed * PAGE_SIZE - sizeof(heap_block_t);
    new_block->free = 1;
    new_block->next = heap_head;
    heap_head = new_block;

    // Retry allocation
    return kmalloc(size);
}

void kfree(void* ptr) {
    if (!ptr) return;
    heap_block_t* block = (heap_block_t*)((uint8_t*)ptr - sizeof(heap_block_t));
    block->free = 1;
    
    // Basic coalescing of adjacent free blocks (forward)
    heap_block_t* curr = heap_head;
    while (curr) {
        if (curr->free && curr->next && curr->next->free) {
            // Check if contiguous in memory
            if ((uint8_t*)curr + sizeof(heap_block_t) + curr->size == (uint8_t*)curr->next) {
                curr->size += sizeof(heap_block_t) + curr->next->size;
                curr->next = curr->next->next;
            } else {
                curr = curr->next;
            }
        } else {
            curr = curr->next;
        }
    }
}

void* krealloc(void* ptr, size_t size) {
    if (!ptr) return kmalloc(size);
    if (size == 0) {
        kfree(ptr);
        return 0;
    }
    
    heap_block_t* block = (heap_block_t*)((uint8_t*)ptr - sizeof(heap_block_t));
    if (block->size >= size) {
        return ptr; // Existing block is large enough
    }
    
    void* new_ptr = kmalloc(size);
    if (!new_ptr) return 0; // OOM
    
    // Copy old contents
    memcpy(new_ptr, ptr, block->size);
    kfree(ptr);
    return new_ptr;
}

void* kcalloc(size_t num, size_t size) {
    size_t total = num * size;
    void* ptr = kmalloc(total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}
