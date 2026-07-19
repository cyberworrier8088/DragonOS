#include "ata.h"
#include "../cpu/ports.h"
#include "serial.h"

void ata_init(void) {
    // Select master drive
    outb(ATA_PRIMARY_DRIVE, 0xA0);
    // Send identify command
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_IDENTIFY);
    
    // Read status
    uint8_t status = inb(ATA_PRIMARY_STATUS);
    if (status == 0) {
        print_serial("[ATA] No primary master drive found.\n");
        return;
    }
    
    // Poll until BSY is clear
    for (volatile int timeout = 0; timeout < 20000; timeout++) {
        status = inb(ATA_PRIMARY_STATUS);
        if (!(status & ATA_STATUS_BSY)) break;
    }
    
    if (status & ATA_STATUS_ERR) {
        print_serial("[ATA] Drive identify error.\n");
        return;
    }
    
    // Wait until DRQ is set
    for (volatile int timeout = 0; timeout < 20000; timeout++) {
        status = inb(ATA_PRIMARY_STATUS);
        if (status & ATA_STATUS_DRQ) break;
    }
    
    if (!(status & ATA_STATUS_DRQ)) {
        print_serial("[ATA] Drive identify timeout.\n");
        return;
    }
    
    // Read 256 words of identification info
    for (int i = 0; i < 256; i++) {
        inw(ATA_PRIMARY_DATA);
    }
    
    print_serial("[ATA] Primary master drive detected and initialized successfully.\n");
}

int ata_read_sectors(uint32_t lba, uint8_t count, uint32_t* buffer) {
    outb(ATA_PRIMARY_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_PRIMARY_SECCOUNT, count);
    outb(ATA_PRIMARY_LBA_LO, (uint8_t)lba);
    outb(ATA_PRIMARY_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_PRIMARY_LBA_HI, (uint8_t)(lba >> 16));
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_READ);

    uint16_t* ptr16 = (uint16_t*)buffer;
    for (int sector = 0; sector < count; sector++) {
        uint8_t status = 0;
        for (volatile int timeout = 0; timeout < 100000; timeout++) {
            status = inb(ATA_PRIMARY_STATUS);
            if (!(status & ATA_STATUS_BSY) && (status & ATA_STATUS_DRQ)) break;
        }
        
        if (!(status & ATA_STATUS_DRQ) || (status & ATA_STATUS_ERR)) {
            return -1;
        }
        
        for (int i = 0; i < 256; i++) {
            ptr16[sector * 256 + i] = inw(ATA_PRIMARY_DATA);
        }
    }
    return 0;
}

int ata_write_sectors(uint32_t lba, uint8_t count, const uint32_t* buffer) {
    outb(ATA_PRIMARY_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_PRIMARY_SECCOUNT, count);
    outb(ATA_PRIMARY_LBA_LO, (uint8_t)lba);
    outb(ATA_PRIMARY_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_PRIMARY_LBA_HI, (uint8_t)(lba >> 16));
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_WRITE);

    const uint16_t* ptr16 = (const uint16_t*)buffer;
    for (int sector = 0; sector < count; sector++) {
        uint8_t status = 0;
        for (volatile int timeout = 0; timeout < 100000; timeout++) {
            status = inb(ATA_PRIMARY_STATUS);
            if (!(status & ATA_STATUS_BSY) && (status & ATA_STATUS_DRQ)) break;
        }
        
        if (!(status & ATA_STATUS_DRQ) || (status & ATA_STATUS_ERR)) {
            return -1;
        }
        
        for (int i = 0; i < 256; i++) {
            outw(ATA_PRIMARY_DATA, ptr16[sector * 256 + i]);
        }
    }
    return 0;
}
