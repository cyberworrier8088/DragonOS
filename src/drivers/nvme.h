#ifndef NVME_H
#define NVME_H

#include <stdint.h>

void nvme_init(void);

// Real capacity/presence, populated from the namespace's Identify data.
// Mirrors ata_disk_present()/ata_get_sector_count() so callers (the VFS
// /dev/nvme0n1 node) can size themselves to the actual attached namespace.
int nvme_disk_present(void);
uint64_t nvme_get_sector_count(void); // 0 if no controller/namespace was found
uint32_t nvme_get_sector_size(void);  // bytes per LBA (512 or 4096), 0 if absent

int nvme_read_sectors(uint64_t lba, uint32_t count, void* buffer);
int nvme_write_sectors(uint64_t lba, uint32_t count, const void* buffer);

#endif
