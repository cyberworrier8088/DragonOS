#include "pci.h"
#include "../cpu/ports.h"
#include "../mm/kheap.h"
#include "serial.h"
#include "../libc/string.h"

pci_device_t* pci_devices_head = 0;
int pci_device_count = 0;

uint32_t pci_read_config_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = ((uint32_t)bus << 16) |
                       ((uint32_t)slot << 11) |
                       ((uint32_t)func << 8) |
                       (offset & 0xFC) |
                       0x80000000;
    outl(0xCF8, address);
    return inl(0xCFC);
}

const char* pci_get_class_name(uint8_t class_code) {
    switch (class_code) {
        case 0x00: return "Unclassified";
        case 0x01: return "Mass Storage Controller";
        case 0x02: return "Network Controller";
        case 0x03: return "Display Controller";
        case 0x04: return "Multimedia Controller";
        case 0x05: return "Memory Controller";
        case 0x06: return "Bridge Device";
        case 0x07: return "Simple Comm Controller";
        case 0x08: return "Base System Peripheral";
        case 0x09: return "Input Device Controller";
        case 0x0A: return "Docking Station";
        case 0x0B: return "Processor";
        case 0x0C: return "Serial Bus Controller";
        case 0x0D: return "Wireless Controller";
        case 0x0E: return "Intelligent Controller";
        case 0x0F: return "Satellite Comm Controller";
        case 0x10: return "Encryption Controller";
        case 0x11: return "Signal Processing Controller";
        default:   return "Unknown Device";
    }
}

void pci_check_device(uint8_t bus, uint8_t slot) {
    uint32_t reg0 = pci_read_config_dword(bus, slot, 0, 0);
    uint16_t vendor_id = reg0 & 0xFFFF;
    if (vendor_id == 0xFFFF) return; // Device doesn't exist

    // Multiple functions support check
    uint32_t reg12 = pci_read_config_dword(bus, slot, 0, 0x0C);
    uint8_t header_type = (reg12 >> 16) & 0xFF;
    int max_funcs = (header_type & 0x80) ? 8 : 1;

    for (uint8_t func = 0; func < max_funcs; func++) {
        uint32_t r0 = pci_read_config_dword(bus, slot, func, 0);
        uint16_t ven = r0 & 0xFFFF;
        if (ven == 0xFFFF) continue;

        uint32_t r8 = pci_read_config_dword(bus, slot, func, 0x08);
        uint8_t class_code = (r8 >> 24) & 0xFF;
        uint8_t subclass = (r8 >> 16) & 0xFF;
        uint16_t dev = (r0 >> 16) & 0xFFFF;

        pci_device_t* device = (pci_device_t*)kmalloc(sizeof(pci_device_t));
        if (!device) return;

        device->bus = bus;
        device->slot = slot;
        device->func = func;
        device->vendor_id = ven;
        device->device_id = dev;
        device->class_code = class_code;
        device->subclass = subclass;
        device->class_name = pci_get_class_name(class_code);
        
        // Scan BAR0 to BAR5
        for (int i = 0; i < 6; i++) {
            uint32_t bar_val = pci_read_config_dword(bus, slot, func, 0x10 + (i * 4));
            device->bar[i] = bar_val;
        }

        device->next = pci_devices_head;
        pci_devices_head = device;
        pci_device_count++;

        // Print debug information to serial logs
        char log_buf[128];
        char num_buf[32];
        strcpy(log_buf, "[PCI] Scanned: ");
        strcat(log_buf, device->class_name);
        strcat(log_buf, " (Ven: ");
        int_to_ascii(ven, num_buf); // Hex representation would be better, but decimal works for debugging
        strcat(log_buf, num_buf);
        strcat(log_buf, " Dev: ");
        int_to_ascii(dev, num_buf);
        strcat(log_buf, num_buf);
        strcat(log_buf, ") BAR0: 0x");
        
        // Simple hex print for BAR0
        uint32_t b0 = device->bar[0];
        const char* hex_chars = "0123456789ABCDEF";
        for (int i = 7; i >= 0; i--) {
            int nibble = (b0 >> (i * 4)) & 0xF;
            char nstr[2] = {hex_chars[nibble], '\0'};
            strcat(log_buf, nstr);
        }
        strcat(log_buf, "\n");
        
        print_serial(log_buf);
    }
}

void pci_init(void) {
    print_serial("[DragonOS] Starting PCI Bus scan...\n");
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            pci_check_device(bus, slot);
        }
    }
    print_serial("[DragonOS] PCI Bus scan completed successfully.\n");
}

pci_device_t* pci_find_device(uint16_t vendor_id, uint16_t device_id) {
    pci_device_t* curr = pci_devices_head;
    while (curr) {
        if (curr->vendor_id == vendor_id && curr->device_id == device_id) {
            return curr;
        }
        curr = curr->next;
    }
    return 0;
}

pci_device_t* pci_find_by_class(uint8_t class_code, uint8_t subclass) {
    pci_device_t* curr = pci_devices_head;
    while (curr) {
        if (curr->class_code == class_code && curr->subclass == subclass) {
            return curr;
        }
        curr = curr->next;
    }
    return 0;
}
