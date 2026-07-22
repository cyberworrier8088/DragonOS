#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>
#include <stddef.h>

#define PAGE_PRESENT  (1ULL << 0)
#define PAGE_WRITE    (1ULL << 1)
#define PAGE_USER     (1ULL << 2)
#define PAGE_HUGE     (1ULL << 7)   // PS bit: entry maps a 2MB/1GB page, not a table

void init_paging(void);
void paging_map(uint64_t virt, uint64_t phys, uint64_t flags);
void paging_unmap(uint64_t virt);
uint64_t paging_walk(uint64_t virt);

// Grant Ring 3 access to an already-mapped virtual range by setting the User
// bit on every paging level that covers it. Needed to run user code / use a
// user stack that lives in supervisor-mapped kernel or HHDM memory.
void paging_make_user(uint64_t virt, uint64_t size);

#endif
