#include "pmm.h"
#include "../libc/string.h"

uint64_t pmm_total_memory = 0;
uint64_t pmm_used_memory = 0;
uint64_t pmm_free_memory = 0;
uint64_t pmm_hhdm_offset = 0;

static uint8_t* pmm_bitmap = 0;
static uint64_t pmm_bitmap_size = 0;
static uint64_t pmm_bitmap_pages = 0;

#define BITMAP_SET(bit) (pmm_bitmap[(bit) / 8] |= (1 << ((bit) % 8)))
#define BITMAP_CLEAR(bit) (pmm_bitmap[(bit) / 8] &= ~(1 << ((bit) % 8)))
#define BITMAP_TEST(bit) (pmm_bitmap[(bit) / 8] & (1 << ((bit) % 8)))

void pmm_init(struct limine_memmap_response* memmap, uint64_t hhdm_offset) {
    pmm_hhdm_offset = hhdm_offset;
    uint64_t highest_address = 0;
    
    // 1. Find the highest physical address to size the bitmap
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry* entry = memmap->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE || 
            entry->type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE ||
            entry->type == LIMINE_MEMMAP_KERNEL_AND_MODULES) {
            uint64_t top = entry->base + entry->length;
            if (top > highest_address) {
                highest_address = top;
            }
        }
        
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            pmm_total_memory += entry->length;
        }
    }

    pmm_bitmap_pages = highest_address / PAGE_SIZE;
    pmm_bitmap_size = pmm_bitmap_pages / 8 + 1; // bytes needed for bitmap

    // 2. Find a usable memory region large enough for the bitmap
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry* entry = memmap->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE && entry->length >= pmm_bitmap_size) {
            pmm_bitmap = (uint8_t*)(entry->base + pmm_hhdm_offset);
            // The bitmap occupies this space, so mark it as not fully usable
            entry->base += pmm_bitmap_size;
            entry->length -= pmm_bitmap_size;
            pmm_total_memory -= pmm_bitmap_size;
            break;
        }
    }

    // 3. Initialize all memory as USED initially (1)
    memset(pmm_bitmap, 0xFF, pmm_bitmap_size);

    // 4. Mark only LIMINE_MEMMAP_USABLE memory as FREE (0)
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry* entry = memmap->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            uint64_t start_page = entry->base / PAGE_SIZE;
            uint64_t pages = entry->length / PAGE_SIZE;
            for (uint64_t j = 0; j < pages; j++) {
                BITMAP_CLEAR(start_page + j);
            }
            pmm_free_memory += entry->length;
        }
    }
}

void* pmm_alloc_pages(uint64_t count) {
    if (count == 0) return 0;
    
    uint64_t free_run = 0;
    uint64_t start_idx = 0;

    for (uint64_t i = 1; i < pmm_bitmap_pages; i++) {
        if (!BITMAP_TEST(i)) {
            if (free_run == 0) start_idx = i;
            free_run++;
            
            if (free_run == count) {
                // Found enough contiguous pages!
                for (uint64_t j = 0; j < count; j++) {
                    BITMAP_SET(start_idx + j);
                }
                pmm_used_memory += count * PAGE_SIZE;
                pmm_free_memory -= count * PAGE_SIZE;
                return (void*)(start_idx * PAGE_SIZE + pmm_hhdm_offset);
            }
        } else {
            free_run = 0;
        }
    }

    // Out of memory
    return 0;
}

void* pmm_alloc_page(void) {
    return pmm_alloc_pages(1);
}

void pmm_free_pages(void* ptr, uint64_t count) {
    uint64_t phys_addr = (uint64_t)ptr - pmm_hhdm_offset;
    uint64_t start_idx = phys_addr / PAGE_SIZE;
    for (uint64_t i = 0; i < count; i++) {
        BITMAP_CLEAR(start_idx + i);
    }
    pmm_used_memory -= count * PAGE_SIZE;
    pmm_free_memory += count * PAGE_SIZE;
}

void pmm_free_page(void* ptr) {
    pmm_free_pages(ptr, 1);
}
