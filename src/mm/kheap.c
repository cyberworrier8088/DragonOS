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
    // Initial heap allocation (32MB, which fits Buddy Allocator maximum order of 13)
    void* initial_pages = pmm_alloc_pages(8192);
    if (!initial_pages) {
        print_serial("[kheap] Fatal: failed to allocate initial heap pages!\n");
        return;
    }
    heap_head = (heap_block_t*)initial_pages;
    heap_head->size = 8192 * PAGE_SIZE - sizeof(heap_block_t);
    heap_head->free = 1;
    heap_head->next = 0;
}

void* kmalloc(size_t size) {
    if (size == 0) return 0;
    
    uint64_t rflags;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(rflags));
    
    // 8-byte alignment using branchless bitwise math for maximum speed
    size = (size + 7) & ~7ULL;

    // Best-fit: pick the smallest free block that still satisfies the request.
    // First-fit tends to shred large blocks early and strand the tail ends;
    // best-fit keeps big blocks intact for big requests and cuts fragmentation
    // on the mixed allocation sizes the games hammer the heap with.
    heap_block_t* best = 0;
    for (heap_block_t* curr = heap_head; curr; curr = curr->next) {
        if (curr->free && curr->size >= size) {
            if (!best || curr->size < best->size) {
                best = curr;
                if (best->size == size) break; // exact fit -- can't do better
            }
        }
    }

    if (best) {
        // Split the block if the leftover can hold a header plus a little payload.
        if (best->size > size + sizeof(heap_block_t) + 8) {
            heap_block_t* new_block = (heap_block_t*)((uint8_t*)best + sizeof(heap_block_t) + size);
            new_block->size = best->size - size - sizeof(heap_block_t);
            new_block->free = 1;
            new_block->next = best->next;

            best->size = size;
            best->next = new_block;
        }
        best->free = 0;
        void* ret = (void*)((uint8_t*)best + sizeof(heap_block_t));
        memset(ret, 0, size); // Zero initialize for safety
        __asm__ volatile("push %0; popfq" :: "r"(rflags));
        return ret;
    }

    // Need more memory - allocate a chunk that is at least large enough for the requested size
    uint64_t pages_needed = (size + sizeof(heap_block_t) + PAGE_SIZE - 1) / PAGE_SIZE;
    if (pages_needed < 256) {
        pages_needed = 256; // Allocate at least 1MB to prevent fragmenting PMM
    }

    void* new_pages = pmm_alloc_pages(pages_needed);
    if (!new_pages) {
        __asm__ volatile("push %0; popfq" :: "r"(rflags));
        return 0; // OOM
    }

    heap_block_t* new_block = (heap_block_t*)new_pages;
    new_block->size = pages_needed * PAGE_SIZE - sizeof(heap_block_t);
    new_block->free = 1;
    new_block->next = heap_head;
    heap_head = new_block;

    __asm__ volatile("push %0; popfq" :: "r"(rflags));
    // Retry allocation
    return kmalloc(size);
}

void kfree(void* ptr) {
    if (!ptr) return;
    
    uint64_t rflags;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(rflags));
    
    heap_block_t* block = (heap_block_t*)((uint8_t*)ptr - sizeof(heap_block_t));
    block->free = 1;
    
    // Coalesce with next block if contiguous and free
    if (block->next && block->next->free) {
        if ((uint8_t*)block + sizeof(heap_block_t) + block->size == (uint8_t*)block->next) {
            block->size += sizeof(heap_block_t) + block->next->size;
            block->next = block->next->next;
        }
    }

    // Coalesce with previous block if contiguous and free
    heap_block_t* curr = heap_head;
    while (curr) {
        if (curr->next == block) {
            if (curr->free) {
                if ((uint8_t*)curr + sizeof(heap_block_t) + curr->size == (uint8_t*)block) {
                    curr->size += sizeof(heap_block_t) + block->size;
                    curr->next = block->next;
                }
            }
            break;
        }
        curr = curr->next;
    }
    
    __asm__ volatile("push %0; popfq" :: "r"(rflags));
}

void* krealloc(void* ptr, size_t size) {
    if (!ptr) return kmalloc(size);
    if (size == 0) {
        kfree(ptr);
        return 0;
    }
    
    uint64_t rflags;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(rflags));
    heap_block_t* block = (heap_block_t*)((uint8_t*)ptr - sizeof(heap_block_t));
    if (block->size >= size) {
        __asm__ volatile("push %0; popfq" :: "r"(rflags));
        return ptr; // Existing block is large enough
    }
    __asm__ volatile("push %0; popfq" :: "r"(rflags));
    
    void* new_ptr = kmalloc(size);
    if (!new_ptr) return 0; // OOM
    
    // Copy old contents
    // block->size might be larger than what we need, but we only copy up to block->size (or size if block->size is larger, but it isn't here)
    memcpy(new_ptr, ptr, block->size);
    kfree(ptr);
    return new_ptr;
}

void* kcalloc(size_t num, size_t size) {
    size_t total = num * size;
    void* ptr = kmalloc(total);
    // memset is already done in kmalloc now, but doing it again is harmless.
    // We can just return ptr.
    return ptr;
}
