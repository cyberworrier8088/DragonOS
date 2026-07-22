#include "gdt.h"
#include "../drivers/serial.h"
#include "../libc/string.h"

static struct {
    struct gdt_entry null_desc;
    struct gdt_entry kernel_code;
    struct gdt_entry kernel_data;
    struct gdt_entry user_data;
    struct gdt_entry user_code;
    struct gdt_tss_entry tss_desc;
} __attribute__((packed)) gdt;

static struct gdt_ptr gp;
static tss_entry_t tss;

static void set_gdt_entry(struct gdt_entry* entry, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    entry->base_low = (base & 0xFFFF);
    entry->base_middle = (base >> 16) & 0xFF;
    entry->base_high = (base >> 24) & 0xFF;

    entry->limit_low = (limit & 0xFFFF);
    entry->granularity = (limit >> 16) & 0x0F;
    entry->granularity |= gran & 0xF0;
    entry->access = access;
}

static void set_tss_descriptor(struct gdt_tss_entry* tss_desc, uint64_t base, uint32_t limit) {
    tss_desc->limit_low = limit & 0xFFFF;
    tss_desc->base_low = base & 0xFFFF;
    tss_desc->base_middle = (base >> 16) & 0xFF;
    tss_desc->access = 0x89; // Present, Ring 0, 64-bit TSS (Available)
    tss_desc->granularity = (limit >> 16) & 0x0F;
    tss_desc->base_high = (base >> 24) & 0xFF;
    tss_desc->base_upper = (base >> 32) & 0xFFFFFFFF;
    tss_desc->reserved = 0;
}

void tss_set_rsp0(uint64_t rsp) {
    tss.rsp0 = rsp;
}

void gdt_init(void) {
    memset(&gdt, 0, sizeof(gdt));
    memset(&tss, 0, sizeof(tss));

    // 0x00: Null Descriptor
    set_gdt_entry(&gdt.null_desc, 0, 0, 0, 0);

    // 0x08: Kernel Code (64-bit, Ring 0)
    set_gdt_entry(&gdt.kernel_code, 0, 0xFFFFFFFF, 0x9A, 0x20);

    // 0x10: Kernel Data (64-bit, Ring 0)
    set_gdt_entry(&gdt.kernel_data, 0, 0xFFFFFFFF, 0x92, 0x00);

    // 0x18: User Data (64-bit, Ring 3)
    set_gdt_entry(&gdt.user_data, 0, 0xFFFFFFFF, 0xF2, 0x00);

    // 0x20: User Code (64-bit, Ring 3)
    set_gdt_entry(&gdt.user_code, 0, 0xFFFFFFFF, 0xFA, 0x20);

    // 0x28: TSS Descriptor (16 bytes in 64-bit mode)
    tss.iomap_base = sizeof(tss_entry_t);
    set_tss_descriptor(&gdt.tss_desc, (uint64_t)&tss, sizeof(tss_entry_t) - 1);

    gp.limit = sizeof(gdt) - 1;
    gp.base = (uint64_t)&gdt;

    // Load GDT
    __asm__ volatile("lgdt %0" :: "m"(gp));

    // Reload segment registers
    __asm__ volatile(
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        : : : "rax"
    );

    // Load TSS
    __asm__ volatile("ltr %%ax" :: "a"((uint16_t)TSS_SEL));

    print_serial("[DragonOS] GDT and 64-bit TSS loaded successfully (TSS at 0x28).\n");
}
