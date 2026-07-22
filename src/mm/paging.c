#include "paging.h"
#include "pmm.h"
#include "../libc/string.h"
#include "../drivers/serial.h"

static uint64_t* pml4_table = 0;

static inline void invlpg(uint64_t addr) {
    __asm__ volatile("invlpg (%0)" :: "r"(addr) : "memory");
}

void init_paging(void) {
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    
    // Convert CR3 physical address to virtual address using HHDM
    pml4_table = (uint64_t*)(cr3 + pmm_hhdm_offset);
    print_serial("[DragonOS] Paging initialized. PML4 table located.\n");
}

static uint64_t* get_next_level(uint64_t* current_level, uint64_t index, int allocate) {
    uint64_t entry = current_level[index];
    if (entry & PAGE_PRESENT) {
        uint64_t phys = entry & 0x000FFFFFFFFFF000ULL;
        return (uint64_t*)(phys + pmm_hhdm_offset);
    }
    
    if (!allocate) return 0;
    
    // Allocate new page for the next level table
    void* new_table_virt = pmm_alloc_page();
    if (!new_table_virt) {
        print_serial("[DragonOS] Error: Paging failed to allocate a sub-table!\n");
        return 0;
    }
    
    // Zero out the new page table
    memset(new_table_virt, 0, PAGE_SIZE);
    
    uint64_t phys = (uint64_t)new_table_virt - pmm_hhdm_offset;
    current_level[index] = phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    
    return (uint64_t*)new_table_virt;
}

void paging_map(uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt >> 12) & 0x1FF;
    
    uint64_t* pdpt = get_next_level(pml4_table, pml4_idx, 1);
    if (!pdpt) return;
    
    uint64_t* pd = get_next_level(pdpt, pdpt_idx, 1);
    if (!pd) return;
    
    uint64_t* pt = get_next_level(pd, pd_idx, 1);
    if (!pt) return;
    
    pt[pt_idx] = (phys & 0x000FFFFFFFFFF000ULL) | flags | PAGE_PRESENT;
    invlpg(virt);
}

void paging_unmap(uint64_t virt) {
    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt >> 12) & 0x1FF;
    
    uint64_t* pdpt = get_next_level(pml4_table, pml4_idx, 0);
    if (!pdpt) return;
    
    uint64_t* pd = get_next_level(pdpt, pdpt_idx, 0);
    if (!pd) return;
    
    uint64_t* pt = get_next_level(pd, pd_idx, 0);
    if (!pt) return;
    
    pt[pt_idx] = 0;
    invlpg(virt);
}

void paging_make_user(uint64_t virt, uint64_t size) {
    if (size == 0) return;

    uint64_t start = virt & ~0xFFFULL;
    uint64_t end   = (virt + size + PAGE_SIZE - 1) & ~0xFFFULL;

    for (uint64_t addr = start; addr < end; addr += PAGE_SIZE) {
        uint64_t i4 = (addr >> 39) & 0x1FF;
        uint64_t i3 = (addr >> 30) & 0x1FF;
        uint64_t i2 = (addr >> 21) & 0x1FF;
        uint64_t i1 = (addr >> 12) & 0x1FF;

        // The User bit is AND-ed across all levels, so every table entry on the
        // path -- plus the final leaf -- must have it set. Stop early at a huge
        // page (PS bit): that entry *is* the leaf.
        if (!(pml4_table[i4] & PAGE_PRESENT)) continue;
        pml4_table[i4] |= PAGE_USER;
        uint64_t* pdpt = (uint64_t*)((pml4_table[i4] & 0x000FFFFFFFFFF000ULL) + pmm_hhdm_offset);

        if (!(pdpt[i3] & PAGE_PRESENT)) continue;
        pdpt[i3] |= PAGE_USER;
        if (pdpt[i3] & PAGE_HUGE) { invlpg(addr); continue; } // 1GB page
        uint64_t* pd = (uint64_t*)((pdpt[i3] & 0x000FFFFFFFFFF000ULL) + pmm_hhdm_offset);

        if (!(pd[i2] & PAGE_PRESENT)) continue;
        pd[i2] |= PAGE_USER;
        if (pd[i2] & PAGE_HUGE) { invlpg(addr); continue; } // 2MB page
        uint64_t* pt = (uint64_t*)((pd[i2] & 0x000FFFFFFFFFF000ULL) + pmm_hhdm_offset);

        if (!(pt[i1] & PAGE_PRESENT)) continue;
        pt[i1] |= PAGE_USER;
        invlpg(addr);
    }
}

uint64_t paging_walk(uint64_t virt) {
    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt >> 12) & 0x1FF;
    
    uint64_t* pdpt = get_next_level(pml4_table, pml4_idx, 0);
    if (!pdpt) return 0;
    
    uint64_t* pd = get_next_level(pdpt, pdpt_idx, 0);
    if (!pd) return 0;
    
    uint64_t* pt = get_next_level(pd, pd_idx, 0);
    if (!pt) return 0;
    
    uint64_t entry = pt[pt_idx];
    if (!(entry & PAGE_PRESENT)) return 0;
    
    return (entry & 0x000FFFFFFFFFF000ULL) | (virt & 0xFFFULL);
}
