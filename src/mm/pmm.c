#include "pmm.h"
#include "../libc/string.h"
#include "../drivers/serial.h"

uint64_t pmm_total_memory = 0;
uint64_t pmm_used_memory = 0;
uint64_t pmm_free_memory = 0;
uint64_t pmm_hhdm_offset = 0;

#define BUDDY_MAX_ORDER 14  // Max block size = 2^14 pages = 64MB
#define TOTAL_PAGES_MAX (1024 * 1024) // Support up to 4GB of RAM (1M pages)

typedef struct free_block {
    uint64_t next;
    uint64_t prev;
} free_block_t;

// Buddy Allocator State
static uint8_t* buddy_orders = 0; // Size: TOTAL_PAGES_MAX bytes
static uint8_t* buddy_free_flags = 0; // Size: TOTAL_PAGES_MAX bytes (1 = free, 0 = used)
static uint64_t buddy_free_heads[BUDDY_MAX_ORDER + 1];

static uint64_t total_system_pages = 0;

static inline free_block_t* get_block(uint64_t page_idx) {
    return (free_block_t*)(page_idx * PAGE_SIZE + pmm_hhdm_offset);
}

static void add_to_list(uint64_t page_idx, int order) {
    free_block_t* block = get_block(page_idx);
    block->next = buddy_free_heads[order];
    block->prev = 0xFFFFFFFFFFFFFFFFULL;
    
    if (buddy_free_heads[order] != 0xFFFFFFFFFFFFFFFFULL) {
        free_block_t* next_block = get_block(buddy_free_heads[order]);
        next_block->prev = page_idx;
    }
    buddy_free_heads[order] = page_idx;
    buddy_orders[page_idx] = order;
    buddy_free_flags[page_idx] = 1;
}

static void remove_from_list(uint64_t page_idx, int order) {
    free_block_t* block = get_block(page_idx);
    uint64_t next = block->next;
    uint64_t prev = block->prev;
    
    if (prev != 0xFFFFFFFFFFFFFFFFULL) {
        get_block(prev)->next = next;
    } else {
        buddy_free_heads[order] = next;
    }
    
    if (next != 0xFFFFFFFFFFFFFFFFULL) {
        get_block(next)->prev = prev;
    }
    buddy_free_flags[page_idx] = 0;
}

void pmm_init(struct limine_memmap_response* memmap, uint64_t hhdm_offset) {
    pmm_hhdm_offset = hhdm_offset;
    uint64_t highest_address = 0;
    
    // Initialize heads
    for (int i = 0; i <= BUDDY_MAX_ORDER; i++) {
        buddy_free_heads[i] = 0xFFFFFFFFFFFFFFFFULL;
    }

    // 1. Find the highest physical address to size the allocation structures
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry* entry = memmap->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            uint64_t top = entry->base + entry->length;
            if (top > highest_address) {
                highest_address = top;
            }
            pmm_total_memory += entry->length;
        }
    }

    total_system_pages = highest_address / PAGE_SIZE;
    if (total_system_pages > TOTAL_PAGES_MAX) {
        total_system_pages = TOTAL_PAGES_MAX;
    }

    // 2. Allocate the arrays buddy_orders and buddy_free_flags
    uint64_t meta_size = total_system_pages * 2; // 1 byte order, 1 byte flag
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry* entry = memmap->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE && entry->length >= meta_size) {
            buddy_orders = (uint8_t*)(entry->base + pmm_hhdm_offset);
            buddy_free_flags = buddy_orders + total_system_pages;
            
            entry->base += meta_size;
            entry->length -= meta_size;
            pmm_total_memory -= meta_size;
            break;
        }
    }

    // Zero out metadata
    memset(buddy_orders, 0, total_system_pages);
    memset(buddy_free_flags, 0, total_system_pages);

    // 3. Populate buddy lists with physical usable regions
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry* entry = memmap->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            uint64_t start_page = entry->base / PAGE_SIZE;
            uint64_t end_page = (entry->base + entry->length) / PAGE_SIZE;
            
            uint64_t curr = start_page;
            while (curr < end_page) {
                // Find largest order we can allocate here
                int order = 0;
                while (order < BUDDY_MAX_ORDER) {
                    uint64_t block_size = 1ULL << (order + 1);
                    if (curr + block_size > end_page) break;
                    if ((curr % block_size) != 0) break;
                    order++;
                }
                
                add_to_list(curr, order);
                pmm_free_memory += (1ULL << order) * PAGE_SIZE;
                curr += (1ULL << order);
            }
        }
    }
    
    print_serial("[DragonOS] Buddy Allocator Initialized.\n");
}

void* pmm_alloc_pages(uint64_t count) {
    if (count == 0) return 0;
    
    uint64_t rflags;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(rflags));
    
    // Find the smallest order that fits count
    int target_order = 0;
    while ((1ULL << target_order) < count) {
        target_order++;
    }
    
    if (target_order > BUDDY_MAX_ORDER) {
        __asm__ volatile("push %0; popfq" :: "r"(rflags));
        return 0;
    }

    // Find a free block starting from target_order
    for (int order = target_order; order <= BUDDY_MAX_ORDER; order++) {
        if (buddy_free_heads[order] != 0xFFFFFFFFFFFFFFFFULL) {
            uint64_t found_idx = buddy_free_heads[order];
            remove_from_list(found_idx, order);
            
            // Split the block if order is too large
            while (order > target_order) {
                order--;
                uint64_t buddy_idx = found_idx + (1ULL << order);
                add_to_list(buddy_idx, order);
            }
            
            pmm_used_memory += (1ULL << target_order) * PAGE_SIZE;
            pmm_free_memory -= (1ULL << target_order) * PAGE_SIZE;
            
            void* ret = (void*)(found_idx * PAGE_SIZE + pmm_hhdm_offset);
            __asm__ volatile("push %0; popfq" :: "r"(rflags));
            return ret;
        }
    }
    
    __asm__ volatile("push %0; popfq" :: "r"(rflags));
    return 0; // Out of memory
}

void* pmm_alloc_page(void) {
    return pmm_alloc_pages(1);
}

void pmm_free_pages(void* ptr, uint64_t count) {
    uint64_t rflags;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(rflags));
    
    uint64_t phys = (uint64_t)ptr - pmm_hhdm_offset;
    uint64_t page_idx = phys / PAGE_SIZE;
    
    // Determine the order from the allocated size count
    int order = 0;
    while ((1ULL << order) < count) {
        order++;
    }
    
    pmm_used_memory -= (1ULL << order) * PAGE_SIZE;
    pmm_free_memory += (1ULL << order) * PAGE_SIZE;

    // Coalesce blocks with buddies
    while (order < BUDDY_MAX_ORDER) {
        uint64_t buddy_idx = page_idx ^ (1ULL << order);
        
        // Ensure buddy is in range and free with the same order
        if (buddy_idx >= total_system_pages) break;
        if (!buddy_free_flags[buddy_idx] || buddy_orders[buddy_idx] != order) break;
        
        // Remove buddy from its free list
        remove_from_list(buddy_idx, order);
        
        // Merge page_idx and buddy_idx
        if (buddy_idx < page_idx) {
            page_idx = buddy_idx;
        }
        order++;
    }
    
    // Add final coalesced block back to list
    add_to_list(page_idx, order);
    __asm__ volatile("push %0; popfq" :: "r"(rflags));
}

void pmm_free_page(void* ptr) {
    pmm_free_pages(ptr, 1);
}
