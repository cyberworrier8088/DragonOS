#include "kheap.h"
#include "pmm.h"

typedef struct heap_block {
    size_t size;
    int free;
    struct heap_block* next;
} heap_block_t;

static heap_block_t* heap_head = 0;

void kheap_init(void) {
    // Initial heap allocation (e.g. 1MB)
    void* initial_pages = pmm_alloc_pages(256);
    heap_head = (heap_block_t*)initial_pages;
    heap_head->size = 256 * PAGE_SIZE - sizeof(heap_block_t);
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

    // Need more memory - for simplicity, just allocate 1MB chunk from PMM and add to list
    void* new_pages = pmm_alloc_pages(256);
    if (!new_pages) return 0; // OOM

    heap_block_t* new_block = (heap_block_t*)new_pages;
    new_block->size = 256 * PAGE_SIZE - sizeof(heap_block_t);
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
