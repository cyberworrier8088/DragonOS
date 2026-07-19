#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include "../../limine-bin/limine.h"

#define PAGE_SIZE 4096

extern uint64_t pmm_total_memory;
extern uint64_t pmm_used_memory;
extern uint64_t pmm_free_memory;
extern uint64_t pmm_hhdm_offset;

void pmm_init(struct limine_memmap_response* memmap, uint64_t hhdm_offset);
void* pmm_alloc_page(void);
void* pmm_alloc_pages(uint64_t count);
void pmm_free_page(void* ptr);
void pmm_free_pages(void* ptr, uint64_t count);

#endif
