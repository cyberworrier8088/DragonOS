#include "e1000.h"
#include "../mm/kheap.h"
#include "../mm/pmm.h"
#include "../mm/paging.h"
#include "serial.h"
#include "../libc/string.h"

#define E1000_TX_BUF_SIZE 2048

// Register window to map for the NIC BAR. Limine's HHDM only guarantees
// coverage of memory listed in the bootloader's memory map; a PCI BAR window
// (assigned by firmware into the "PCI hole" below 4GB) usually is NOT listed
// there, so the HHDM alias of it is not actually backed by a present PTE --
// confirmed by a page fault at hhdm_offset+phys even after adding the offset
// correctly. 128KB covers every register this driver touches with headroom.
#define E1000_MMIO_MAP_SIZE 0x20000

static pci_device_t* e1000_device = 0;
static uint64_t e1000_mmio_base = 0; // VIRTUAL (HHDM) base of the NIC registers
static uint8_t mac_addr[6];

static struct e1000_rx_desc* rx_descs;
static struct e1000_tx_desc* tx_descs;
static void* tx_bufs[E1000_NUM_TX_DESC]; // per-descriptor bounce buffers (HHDM)

static uint16_t tx_cur = 0;

// The NIC's DMA engine works on PHYSICAL addresses. kmalloc/pmm hand back HHDM
// virtual pointers, so anything programmed into a descriptor or a base-address
// register must be translated first. Feeding the NIC a virtual address made it
// DMA to the wrong physical memory (silently on QEMU, corruption on real HW).
static inline uint64_t virt_to_phys(void* v) {
    return (uint64_t)v - pmm_hhdm_offset;
}

static void write_command(uint16_t p_address, uint32_t p_value) {
    (*(volatile uint32_t*)((uint64_t)e1000_mmio_base + p_address)) = p_value;
}

static uint32_t read_command(uint16_t p_address) {
    return (*(volatile uint32_t*)((uint64_t)e1000_mmio_base + p_address));
}

static int detect_eeprom() {
    uint32_t val = 0;
    write_command(REG_EEPROM, 0x1); // set EEPROM request bit
    
    for(int i = 0; i < 1000; i++) {
        val = read_command(REG_EEPROM);
        if(val & 0x10) return 1; // EEPROM ready
    }
    return 0;
}

static uint16_t eeprom_read(uint8_t addr) {
    uint32_t tmp = 0;
    write_command(REG_EEPROM, (1) | ((uint32_t)(addr) << 8));
    // Bounded wait for the DONE bit so a missing/odd EEPROM can't hang boot.
    for (int i = 0; i < 100000; i++) {
        tmp = read_command(REG_EEPROM);
        if (tmp & (1 << 4)) break;
    }
    return (uint16_t)((tmp >> 16) & 0xFFFF);
}

static void read_mac_address() {
    if (detect_eeprom()) {
        uint16_t temp;
        temp = eeprom_read(0);
        mac_addr[0] = temp & 0xFF;
        mac_addr[1] = temp >> 8;
        temp = eeprom_read(1);
        mac_addr[2] = temp & 0xFF;
        mac_addr[3] = temp >> 8;
        temp = eeprom_read(2);
        mac_addr[4] = temp & 0xFF;
        mac_addr[5] = temp >> 8;
    } else {
        // Fallback to MMIO MAC read
        uint8_t * mem_base_mac_8 = (uint8_t *)((uint64_t)e1000_mmio_base + REG_RAL);
        for(int i = 0; i < 6; i++) {
            mac_addr[i] = mem_base_mac_8[i];
        }
    }
}

static void rx_init() {
    rx_descs = (struct e1000_rx_desc*)kmalloc(sizeof(struct e1000_rx_desc) * E1000_NUM_RX_DESC);
    for(int i = 0; i < E1000_NUM_RX_DESC; i++) {
        rx_descs[i].addr = virt_to_phys(kmalloc(8192 + 16)); // physical buffer for DMA
        rx_descs[i].status = 0;
    }

    uint64_t ring_phys = virt_to_phys(rx_descs);
    write_command(REG_RDBAL, (uint32_t)(ring_phys & 0xFFFFFFFF));
    write_command(REG_RDBAH, (uint32_t)(ring_phys >> 32));
    write_command(REG_RDLEN, E1000_NUM_RX_DESC * 16);
    write_command(REG_RDH, 0);
    write_command(REG_RDT, E1000_NUM_RX_DESC - 1);

    write_command(REG_RCTL, RCTL_EN | RCTL_SBP | RCTL_UPE | RCTL_MPE | RCTL_LPE | RCTL_BAM | RCTL_SECRC);
}

static void tx_init() {
    tx_descs = (struct e1000_tx_desc*)kmalloc(sizeof(struct e1000_tx_desc) * E1000_NUM_TX_DESC);
    for(int i = 0; i < E1000_NUM_TX_DESC; i++) {
        tx_bufs[i] = kmalloc(E1000_TX_BUF_SIZE);
        tx_descs[i].addr = 0;
        tx_descs[i].cmd = 0;
        tx_descs[i].status = 1; // DD set: descriptor is free
    }

    uint64_t ring_phys = virt_to_phys(tx_descs);
    write_command(REG_TDBAL, (uint32_t)(ring_phys & 0xFFFFFFFF));
    write_command(REG_TDBAH, (uint32_t)(ring_phys >> 32));
    write_command(REG_TDLEN, E1000_NUM_TX_DESC * 16);
    write_command(REG_TDH, 0);
    write_command(REG_TDT, 0);

    write_command(REG_TCTL, TCTL_EN | TCTL_PSP);
}

void e1000_init(void) {
    e1000_device = pci_find_device(E1000_VENDOR_ID, E1000_DEVICE_ID);
    if (!e1000_device) {
        print_serial("[E1000] Device not found on PCI bus.\n");
        return;
    }
    
    // Enable bus mastering
    pci_enable_bus_master(e1000_device);

    // BAR0 holds a PHYSICAL memory-mapped register base (bit 0 = 0 for a memory
    // BAR; the low 4 bits are flags). The kernel runs in the higher half with no
    // identity map, so the registers are reachable only through the HHDM view of
    // physical memory -- using the raw BAR value as a pointer faults instantly.
    uint32_t bar0 = e1000_device->bar[0];
    uint32_t mmio_phys = bar0 & 0xFFFFFFF0;
    if ((bar0 & 0x1) || mmio_phys == 0) {
        print_serial("[E1000] BAR0 is not a usable memory-mapped region.\n");
        return;
    }
    e1000_mmio_base = (uint64_t)mmio_phys + pmm_hhdm_offset;

    // Explicitly back this MMIO window with present page table entries at its
    // HHDM address. Without this, the HHDM alias faults (not-present) even
    // though the offset arithmetic is correct, because Limine never mapped a
    // "hole" address like a PCI BAR in the first place.
    for (uint64_t off = 0; off < E1000_MMIO_MAP_SIZE; off += PAGE_SIZE) {
        paging_map(e1000_mmio_base + off, (uint64_t)mmio_phys + off, PAGE_PRESENT | PAGE_WRITE);
    }
    
    print_serial("[E1000] Device found! Init MMIO...\n");
    
    read_mac_address();
    
    print_serial("[E1000] MAC Address: ");
    const char* hex_chars = "0123456789ABCDEF";
    char mac_str[18] = {0};
    for(int i=0; i<6; i++) {
        mac_str[i*3] = hex_chars[(mac_addr[i] >> 4) & 0xF];
        mac_str[i*3+1] = hex_chars[mac_addr[i] & 0xF];
        if (i < 5) mac_str[i*3+2] = ':';
    }
    print_serial(mac_str);
    print_serial("\n");
    
    // Keep NIC interrupts MASKED. There is no e1000 IRQ handler registered, so
    // enabling them would let an unserviced, level-triggered interrupt latch on
    // and fire forever -- an interrupt storm that hangs the machine. RX still
    // fills descriptors via DMA and can be polled. (REG_IMC = interrupt mask
    // clear; writing all-ones disables every interrupt source.)
    write_command(REG_IMC, 0xFFFFFFFF);
    read_command(REG_ICR); // clear any latched cause

    rx_init();
    tx_init();

    print_serial("[E1000] Driver Initialization Complete.\n");
}

void e1000_send_packet(const void* p_data, uint16_t p_len) {
    if (!e1000_device) return;
    if (p_len > E1000_TX_BUF_SIZE) p_len = E1000_TX_BUF_SIZE;

    // Copy into the descriptor's bounce buffer and hand the NIC its physical
    // address. This makes any caller pointer valid and keeps the DMA source
    // stable for the duration of the transfer.
    memcpy(tx_bufs[tx_cur], p_data, p_len);
    tx_descs[tx_cur].addr = virt_to_phys(tx_bufs[tx_cur]);
    tx_descs[tx_cur].length = p_len;
    tx_descs[tx_cur].cmd = CMD_EOP | CMD_IFCS | CMD_RS;
    tx_descs[tx_cur].status = 0;

    uint16_t old_cur = tx_cur;
    tx_cur = (tx_cur + 1) % E1000_NUM_TX_DESC;
    write_command(REG_TDT, tx_cur);

    // Wait for the NIC to set the Descriptor Done bit, bounded so a stalled
    // link can never hang the caller forever.
    for (volatile int t = 0; t < 5000000; t++) {
        if (tx_descs[old_cur].status & 0xFF) break;
    }
}

uint8_t* e1000_get_mac_address(void) {
    return mac_addr;
}
