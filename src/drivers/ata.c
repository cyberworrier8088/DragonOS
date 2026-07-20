#include "ata.h"
#include "../cpu/ports.h"
#include "serial.h"

static void ata_wait_400ns(void) {
    inb(ATA_PRIMARY_STATUS);
    inb(ATA_PRIMARY_STATUS);
    inb(ATA_PRIMARY_STATUS);
    inb(ATA_PRIMARY_STATUS);
}

void ata_init(void) {
    // Select master drive
    outb(ATA_PRIMARY_DRIVE, 0xA0);
    // Send identify command
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_IDENTIFY);
    
    // Wait 400ns
    ata_wait_400ns();

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
    uint16_t identify_data[256];
    for (int i = 0; i < 256; i++) {
        identify_data[i] = inw(ATA_PRIMARY_DATA);
    }
    
    // Extract and format Model Number (Words 27-46)
    char model_string[41];
    for (int i = 0; i < 20; i++) {
        uint16_t w = identify_data[27 + i];
        model_string[i * 2] = (char)(w >> 8);
        model_string[i * 2 + 1] = (char)(w & 0xFF);
    }
    model_string[40] = '\0';
    
    // Trim trailing spaces
    for (int i = 39; i >= 0; i--) {
        if (model_string[i] == ' ') model_string[i] = '\0';
        else break;
    }
    
    print_serial("[ATA] Primary master drive detected successfully.\n");
    print_serial("[ATA] Drive Model: ");
    print_serial(model_string);
    print_serial("\n");
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
        ata_wait_400ns();
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
        ata_wait_400ns();
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
