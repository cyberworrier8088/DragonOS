#ifndef PCI_H
#define PCI_H

#include <stdint.h>

typedef struct pci_device {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    const char* class_name;
    struct pci_device* next;
} pci_device_t;

extern pci_device_t* pci_devices_head;
extern int pci_device_count;

void pci_init(void);
pci_device_t* pci_find_device(uint16_t vendor_id, uint16_t device_id);
pci_device_t* pci_find_by_class(uint8_t class_code, uint8_t subclass);

#endif
