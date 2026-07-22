#include "e1000.h"
#include "../mm/kheap.h"
#include "serial.h"
#include "../libc/string.h"

static pci_device_t* e1000_device = 0;
static uint32_t e1000_mmio_base = 0;
static uint8_t mac_addr[6];

static struct e1000_rx_desc* rx_descs;
static struct e1000_tx_desc* tx_descs;

static uint16_t tx_cur = 0;

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
    while(!((tmp = read_command(REG_EEPROM)) & (1 << 4)));
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
        rx_descs[i].addr = (uint64_t)kmalloc(8192 + 16);
        rx_descs[i].status = 0;
    }
    
    write_command(REG_RDBAL, (uint32_t)((uint64_t)rx_descs));
    write_command(REG_RDBAH, (uint32_t)(((uint64_t)rx_descs) >> 32));
    write_command(REG_RDLEN, E1000_NUM_RX_DESC * 16);
    write_command(REG_RDH, 0);
    write_command(REG_RDT, E1000_NUM_RX_DESC - 1);
    
    write_command(REG_RCTL, RCTL_EN | RCTL_SBP | RCTL_UPE | RCTL_MPE | RCTL_LPE | RCTL_BAM | RCTL_SECRC);
}

static void tx_init() {
    tx_descs = (struct e1000_tx_desc*)kmalloc(sizeof(struct e1000_tx_desc) * E1000_NUM_TX_DESC);
    for(int i = 0; i < E1000_NUM_TX_DESC; i++) {
        tx_descs[i].addr = 0;
        tx_descs[i].cmd = 0;
        tx_descs[i].status = 1;
    }
    
    write_command(REG_TDBAL, (uint32_t)((uint64_t)tx_descs));
    write_command(REG_TDBAH, (uint32_t)(((uint64_t)tx_descs) >> 32));
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
    
    // Find MMIO bar
    e1000_mmio_base = e1000_device->bar[0] & ~3; // Clear lower flags
    if (!e1000_mmio_base) {
        print_serial("[E1000] No MMIO base found in BAR0.\n");
        return;
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
    
    // Enable interrupts
    write_command(REG_IMS, 0x1F6DC); // Enable all standard E1000 interrupts
    
    rx_init();
    tx_init();
    
    print_serial("[E1000] Driver Initialization Complete.\n");
}

void e1000_send_packet(const void* p_data, uint16_t p_len) {
    if (!e1000_device) return;
    
    tx_descs[tx_cur].addr = (uint64_t)p_data;
    tx_descs[tx_cur].length = p_len;
    tx_descs[tx_cur].cmd = CMD_EOP | CMD_IFCS | CMD_RS;
    tx_descs[tx_cur].status = 0;
    
    uint8_t old_cur = tx_cur;
    tx_cur = (tx_cur + 1) % E1000_NUM_TX_DESC;
    write_command(REG_TDT, tx_cur);
    
    // Wait for packet to be sent
    while(!(tx_descs[old_cur].status & 0xFF));
}

uint8_t* e1000_get_mac_address(void) {
    return mac_addr;
}
