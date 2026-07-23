#include "nvme.h"
#include "pci.h"
#include "serial.h"
#include "../mm/pmm.h"
#include "../mm/paging.h"
#include "../libc/string.h"

// Register window to map for the controller's BAR0. Registers proper only
// span up to offset 0x38 (ACQ), but the doorbell array starts at 0x1000 and
// is indexed by queue id * doorbell stride (CAP.DSTRD) -- real/QEMU
// controllers almost always report DSTRD=0 (4-byte stride), so this covers
// the admin + one I/O queue pair's doorbells with generous headroom even if
// DSTRD is larger than expected.
#define NVME_MMIO_MAP_SIZE 0x4000

#define NVME_REG_CAP    0x00 // Controller Capabilities (8 bytes)
#define NVME_REG_VS     0x08 // Version
#define NVME_REG_INTMS  0x0C // Interrupt Mask Set
#define NVME_REG_CC     0x14 // Controller Configuration
#define NVME_REG_CSTS   0x1C // Controller Status
#define NVME_REG_AQA    0x24 // Admin Queue Attributes
#define NVME_REG_ASQ    0x28 // Admin Submission Queue Base (8 bytes)
#define NVME_REG_ACQ    0x30 // Admin Completion Queue Base (8 bytes)
#define NVME_REG_DOORBELL_BASE 0x1000

#define NVME_CSTS_RDY (1u << 0)
#define NVME_CSTS_CFS (1u << 1)

#define NVME_QUEUE_DEPTH 64

// Bounded busy-loop iteration cap for every polled wait in this driver
// (controller ready/reset transitions, command completion). nvme_init()
// runs before the kernel enables interrupts (see kernel.c), so PIT ticks
// never advance yet -- a get_ticks()-based timeout would spin forever.
// Raw iteration counts, like ata.c and e1000.c already use, work regardless
// of interrupt state. MMIO reads are cheap compared to ata.c's port I/O, so
// this is sized generously larger than ata.c's timeouts for an equivalent
// real-world margin.
#define NVME_TIMEOUT_ITERS 50000000ULL

// DMA bounce buffer: caller data is staged through this fixed, page-aligned,
// physically contiguous region instead of DMA'ing directly into/out of
// caller buffers, which may not be page-aligned. This keeps PRP construction
// trivial (every page of the buffer is base_phys + i*PAGE_SIZE) at the cost
// of a copy, mirroring the chunked-bounce-buffer pattern vfs.c already uses
// for /dev/sda.
#define NVME_BOUNCE_PAGES 32 // 128KB
#define NVME_BOUNCE_SIZE (NVME_BOUNCE_PAGES * PAGE_SIZE)

// 64-byte Submission Queue Entry (NVMe Base Spec common command format).
typedef struct {
    uint8_t  opcode;
    uint8_t  flags;
    uint16_t cid;
    uint32_t nsid;
    uint64_t rsvd2;
    uint64_t mptr;
    uint64_t prp1;
    uint64_t prp2;
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} __attribute__((packed)) nvme_cmd_t;

// 16-byte Completion Queue Entry. `status` holds DW3's upper 16 bits: bit 0
// is the Phase Tag, bits 1-15 are the 15-bit Status Field.
typedef struct {
    uint32_t dw0;
    uint32_t rsvd;
    uint16_t sq_head;
    uint16_t sq_id;
    uint16_t cid;
    uint16_t status;
} __attribute__((packed)) nvme_cpl_t;

typedef struct {
    nvme_cmd_t* sq;
    // volatile: this memory is written asynchronously by the controller's
    // DMA engine, invisible to the compiler's dataflow analysis. Without
    // volatile here, -O2 is free to hoist/cache a read of cq[i].status out
    // of nvme_submit()'s polling loop (only the loop counter was volatile),
    // so the loop would spin on a stale cached value instead of ever
    // observing the real completion -- confirmed by boot testing: the
    // second-ever admin command timed out nondeterministically depending on
    // exactly how the compiler scheduled that one read.
    volatile nvme_cpl_t* cq;
    uint64_t sq_phys;
    uint64_t cq_phys;
    uint16_t depth;
    uint16_t sq_tail;
    uint16_t cq_head;
    uint16_t phase; // expected Phase Tag value for the next completion
    uint32_t sq_db; // MMIO offset (from nvme_mmio_base) of this queue's SQ tail doorbell
    uint32_t cq_db; // MMIO offset of this queue's CQ head doorbell
} nvme_queue_t;

static pci_device_t* nvme_device = 0;
static uint64_t nvme_mmio_base = 0; // VIRTUAL (HHDM) base of the controller registers
static uint32_t nvme_doorbell_stride = 4; // bytes; CAP.DSTRD decodes to 4 << DSTRD

static nvme_queue_t admin_q;
static nvme_queue_t io_q;

static void* bounce_buf = 0;
static uint64_t bounce_phys = 0;
static uint64_t* prp_list = 0;
static uint64_t prp_list_phys = 0;

static int nvme_present = 0;
static uint64_t nvme_sector_count = 0;
static uint32_t nvme_sector_size = 0;

// The controller's DMA engine works on PHYSICAL addresses; kmalloc/pmm hand
// back HHDM virtual pointers, so anything programmed into a queue base
// register or a PRP entry must be translated first.
static inline uint64_t virt_to_phys(void* v) {
    return (uint64_t)v - pmm_hhdm_offset;
}

static inline void nvme_reg_write32(uint32_t offset, uint32_t value) {
    *(volatile uint32_t*)(nvme_mmio_base + offset) = value;
}

static inline uint32_t nvme_reg_read32(uint32_t offset) {
    return *(volatile uint32_t*)(nvme_mmio_base + offset);
}

static inline void nvme_reg_write64(uint32_t offset, uint64_t value) {
    *(volatile uint64_t*)(nvme_mmio_base + offset) = value;
}

static inline uint64_t nvme_reg_read64(uint32_t offset) {
    return *(volatile uint64_t*)(nvme_mmio_base + offset);
}

static inline uint32_t nvme_sq_doorbell_offset(int qid) {
    return NVME_REG_DOORBELL_BASE + (uint32_t)(2 * qid) * nvme_doorbell_stride;
}

static inline uint32_t nvme_cq_doorbell_offset(int qid) {
    return NVME_REG_DOORBELL_BASE + (uint32_t)(2 * qid + 1) * nvme_doorbell_stride;
}

static void nvme_trim_trailing_spaces(char* s, int last_idx) {
    for (int i = last_idx; i >= 0; i--) {
        if (s[i] == ' ' || s[i] == '\0') s[i] = '\0';
        else break;
    }
}

// Submit one command on `q`, ring its doorbell, and busy-poll the matching
// completion slot's Phase Tag. Synchronous and single-outstanding by design
// (this driver never has more than one command in flight), so reusing the
// submission slot index as the Command Identifier is always unambiguous.
static int nvme_submit(nvme_queue_t* q, nvme_cmd_t* cmd, nvme_cpl_t* out_cpl) {
    cmd->cid = q->sq_tail;
    memcpy(&q->sq[q->sq_tail], cmd, sizeof(nvme_cmd_t));
    q->sq_tail = (uint16_t)((q->sq_tail + 1) % q->depth);
    nvme_reg_write32(q->sq_db, q->sq_tail);

    int done = 0;
    for (volatile uint64_t i = 0; i < NVME_TIMEOUT_ITERS; i++) {
        uint16_t status = q->cq[q->cq_head].status;
        if ((status & 0x1) == q->phase) { done = 1; break; }
    }
    if (!done) {
        print_serial("[NVMe] Command timed out waiting for completion.\n");
        return -1;
    }

    uint16_t status = q->cq[q->cq_head].status;
    if (out_cpl) *out_cpl = q->cq[q->cq_head];

    q->cq_head++;
    if (q->cq_head == q->depth) {
        q->cq_head = 0;
        q->phase ^= 1;
    }
    nvme_reg_write32(q->cq_db, q->cq_head);

    uint16_t status_field = (status >> 1) & 0x7FFF;
    if (status_field != 0) {
        print_serial("[NVMe] Command completed with error status.\n");
        return -1;
    }
    return 0;
}

// Allocate and zero one page-aligned, physically contiguous page. Unlike
// kmalloc, pmm_alloc_pages() does not zero its memory -- leaving it dirty
// would risk a stale byte pattern in a freshly allocated completion queue
// whose Phase Tag bit already happens to equal the first-expected value of
// 1, which would make nvme_submit() report a fabricated instant completion.
static void* nvme_alloc_zeroed_pages(uint64_t count) {
    void* p = pmm_alloc_pages(count);
    if (p) memset(p, 0, count * PAGE_SIZE);
    return p;
}

static int nvme_identify(uint8_t cns, uint32_t nsid, uint64_t buf_phys) {
    nvme_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = 0x06; // Identify
    cmd.nsid = nsid;
    cmd.prp1 = buf_phys;
    cmd.cdw10 = cns;
    return nvme_submit(&admin_q, &cmd, 0);
}

static int nvme_create_io_queues(void) {
    void* sq_mem = nvme_alloc_zeroed_pages(1);
    void* cq_mem = nvme_alloc_zeroed_pages(1);
    if (!sq_mem || !cq_mem) {
        print_serial("[NVMe] Failed to allocate I/O queue memory.\n");
        return -1;
    }

    io_q.sq = (nvme_cmd_t*)sq_mem;
    io_q.cq = (volatile nvme_cpl_t*)cq_mem;
    io_q.sq_phys = virt_to_phys(sq_mem);
    io_q.cq_phys = virt_to_phys(cq_mem);
    io_q.depth = NVME_QUEUE_DEPTH;
    io_q.sq_tail = 0;
    io_q.cq_head = 0;
    io_q.phase = 1;
    io_q.sq_db = nvme_sq_doorbell_offset(1);
    io_q.cq_db = nvme_cq_doorbell_offset(1);

    // Create I/O Completion Queue first -- the Submission Queue references
    // it by CQID and the controller rejects creating an SQ against a CQ
    // that doesn't exist yet.
    nvme_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = 0x05; // Create I/O Completion Queue
    cmd.prp1 = io_q.cq_phys;
    cmd.cdw10 = ((uint32_t)(io_q.depth - 1) << 16) | 1; // QSIZE (0's based) | QID=1
    cmd.cdw11 = 0x1; // PC=1 (physically contiguous), IEN=0 (polled, no interrupts)
    if (nvme_submit(&admin_q, &cmd, 0) != 0) {
        print_serial("[NVMe] Failed to create I/O completion queue.\n");
        return -1;
    }

    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = 0x01; // Create I/O Submission Queue
    cmd.prp1 = io_q.sq_phys;
    cmd.cdw10 = ((uint32_t)(io_q.depth - 1) << 16) | 1; // QSIZE | QID=1
    cmd.cdw11 = (1u << 16) | 0x1; // CQID=1, QPRIO=0, PC=1
    if (nvme_submit(&admin_q, &cmd, 0) != 0) {
        print_serial("[NVMe] Failed to create I/O submission queue.\n");
        return -1;
    }

    return 0;
}

// Build PRP1/PRP2 (and a PRP list page for >2 pages) pointing at the shared
// bounce buffer and run one NVM Read/Write. `nsectors` is bounded by the
// caller to fit within NVME_BOUNCE_SIZE. Writes are followed by an NVM
// Flush so a reported success means the data actually reached the backing
// media -- the NVMe equivalent of ata_write_sectors()'s trailing
// ATA_CMD_CACHE_FLUSH.
static int nvme_do_io(uint8_t opcode, uint64_t lba, uint32_t nsectors, int is_write, void* xfer_buf) {
    if (!nvme_present) return -1;

    uint32_t bytes = nsectors * nvme_sector_size;
    uint32_t npages = (bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    if (npages == 0) npages = 1;

    if (is_write) memcpy(bounce_buf, xfer_buf, bytes);

    nvme_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = opcode;
    cmd.nsid = 1;
    cmd.prp1 = bounce_phys;
    if (npages == 1) {
        cmd.prp2 = 0;
    } else if (npages == 2) {
        cmd.prp2 = bounce_phys + PAGE_SIZE;
    } else {
        for (uint32_t i = 1; i < npages; i++) {
            prp_list[i - 1] = bounce_phys + (uint64_t)i * PAGE_SIZE;
        }
        cmd.prp2 = prp_list_phys;
    }
    cmd.cdw10 = (uint32_t)(lba & 0xFFFFFFFFu);
    cmd.cdw11 = (uint32_t)(lba >> 32);
    cmd.cdw12 = (nsectors - 1) & 0xFFFF; // NLB is 0's based

    if (nvme_submit(&io_q, &cmd, 0) != 0) return -1;

    if (!is_write) {
        memcpy(xfer_buf, bounce_buf, bytes);
        return 0;
    }

    nvme_cmd_t flush;
    memset(&flush, 0, sizeof(flush));
    flush.opcode = 0x00; // Flush
    flush.nsid = 1;
    return nvme_submit(&io_q, &flush, 0);
}

int nvme_read_sectors(uint64_t lba, uint32_t count, void* buffer) {
    if (!nvme_present || count == 0) return -1;
    uint8_t* out = (uint8_t*)buffer;
    uint32_t max_chunk = NVME_BOUNCE_SIZE / nvme_sector_size;
    while (count > 0) {
        uint32_t chunk = count > max_chunk ? max_chunk : count;
        if (nvme_do_io(0x02, lba, chunk, 0, out) < 0) return -1;
        out += (uint64_t)chunk * nvme_sector_size;
        lba += chunk;
        count -= chunk;
    }
    return 0;
}

int nvme_write_sectors(uint64_t lba, uint32_t count, const void* buffer) {
    if (!nvme_present || count == 0) return -1;
    const uint8_t* in = (const uint8_t*)buffer;
    uint32_t max_chunk = NVME_BOUNCE_SIZE / nvme_sector_size;
    while (count > 0) {
        uint32_t chunk = count > max_chunk ? max_chunk : count;
        if (nvme_do_io(0x01, lba, chunk, 1, (void*)in) < 0) return -1;
        in += (uint64_t)chunk * nvme_sector_size;
        lba += chunk;
        count -= chunk;
    }
    return 0;
}

int nvme_disk_present(void) { return nvme_present; }
uint64_t nvme_get_sector_count(void) { return nvme_present ? nvme_sector_count : 0; }
uint32_t nvme_get_sector_size(void) { return nvme_present ? nvme_sector_size : 0; }

void nvme_init(void) {
    nvme_device = pci_find_by_class(0x01, 0x08); // Mass Storage / NVM Controller
    if (!nvme_device) {
        print_serial("[NVMe] No NVMe controller found on PCI bus.\n");
        return;
    }

    // Enable Memory Space (bit 1) in addition to Bus Master (bit 2, set by
    // pci_enable_bus_master below): Bus Master alone only permits the
    // device to *initiate* DMA, the BAR itself must also be enabled as a
    // memory decode target or every MMIO register access below faults as
    // if nothing were mapped there.
    uint32_t pci_cmd = pci_read_config_dword(nvme_device->bus, nvme_device->slot, nvme_device->func, 0x04);
    pci_cmd |= (1u << 1) | (1u << 2);
    pci_write_config_dword(nvme_device->bus, nvme_device->slot, nvme_device->func, 0x04, pci_cmd);
    pci_enable_bus_master(nvme_device);

    // NVMe's BAR0 is virtually always a 64-bit MMIO BAR (bits 2:1 of the low
    // dword = 10b) -- the high 32 bits of the physical base live in BAR1,
    // not implicitly zero like the 32-bit BAR e1000.c only ever needed.
    uint32_t bar0 = nvme_device->bar[0];
    if (bar0 & 0x1) {
        print_serial("[NVMe] BAR0 is I/O space, not memory-mapped. Unsupported.\n");
        return;
    }
    uint8_t bar_type = (bar0 >> 1) & 0x3;
    uint64_t mmio_phys = bar0 & 0xFFFFFFF0u;
    if (bar_type == 0x2) {
        mmio_phys |= ((uint64_t)nvme_device->bar[1]) << 32;
    }
    if (mmio_phys == 0) {
        print_serial("[NVMe] BAR0 has no usable memory-mapped region.\n");
        return;
    }
    nvme_mmio_base = mmio_phys + pmm_hhdm_offset;

    // Explicitly back this MMIO window with present page table entries at
    // its HHDM address -- Limine never mapped a "hole" address like a PCI
    // BAR, so the HHDM alias would otherwise fault even though the offset
    // arithmetic is correct (same reasoning as e1000.c's MMIO mapping).
    for (uint64_t off = 0; off < NVME_MMIO_MAP_SIZE; off += PAGE_SIZE) {
        paging_map(nvme_mmio_base + off, mmio_phys + off, PAGE_PRESENT | PAGE_WRITE);
    }

    print_serial("[NVMe] Controller found. Initializing...\n");

    uint64_t cap = nvme_reg_read64(NVME_REG_CAP);
    uint32_t mqes = (uint32_t)(cap & 0xFFFF);
    uint8_t dstrd = (uint8_t)((cap >> 32) & 0xF);
    uint8_t mpsmin = (uint8_t)((cap >> 48) & 0xF);
    nvme_doorbell_stride = 4u << dstrd;

    if (mpsmin != 0) {
        // MPSMIN>0 means the controller's smallest supported page size is
        // larger than 4096 -- this driver always programs CC.MPS=0 (4KB),
        // which such a controller would reject. Not expected on QEMU or
        // any real NVMe controller in practice; fail cleanly rather than
        // enable with an invalid configuration.
        print_serial("[NVMe] Controller requires a page size larger than 4KB. Unsupported.\n");
        return;
    }

    uint16_t admin_depth = NVME_QUEUE_DEPTH;
    if (mqes + 1 < admin_depth) admin_depth = (uint16_t)(mqes + 1);

    // Reset the controller if a previous (unclean) boot left it enabled.
    if (nvme_reg_read32(NVME_REG_CC) & 1) {
        nvme_reg_write32(NVME_REG_CC, nvme_reg_read32(NVME_REG_CC) & ~1u);
        int cleared = 0;
        for (volatile uint64_t i = 0; i < NVME_TIMEOUT_ITERS; i++) {
            if ((nvme_reg_read32(NVME_REG_CSTS) & NVME_CSTS_RDY) == 0) { cleared = 1; break; }
        }
        if (!cleared) {
            print_serial("[NVMe] Controller failed to reset (CSTS.RDY stuck).\n");
            return;
        }
    }

    void* admin_sq_mem = nvme_alloc_zeroed_pages(1);
    void* admin_cq_mem = nvme_alloc_zeroed_pages(1);
    if (!admin_sq_mem || !admin_cq_mem) {
        print_serial("[NVMe] Failed to allocate admin queue memory.\n");
        return;
    }
    admin_q.sq = (nvme_cmd_t*)admin_sq_mem;
    admin_q.cq = (volatile nvme_cpl_t*)admin_cq_mem;
    admin_q.sq_phys = virt_to_phys(admin_sq_mem);
    admin_q.cq_phys = virt_to_phys(admin_cq_mem);
    admin_q.depth = admin_depth;
    admin_q.sq_tail = 0;
    admin_q.cq_head = 0;
    admin_q.phase = 1;
    admin_q.sq_db = nvme_sq_doorbell_offset(0);
    admin_q.cq_db = nvme_cq_doorbell_offset(0);

    nvme_reg_write32(NVME_REG_AQA, ((uint32_t)(admin_depth - 1) << 16) | (uint32_t)(admin_depth - 1));
    nvme_reg_write64(NVME_REG_ASQ, admin_q.sq_phys);
    nvme_reg_write64(NVME_REG_ACQ, admin_q.cq_phys);

    uint32_t cc = 0;
    cc |= (0u << 4);  // CSS = 000: NVM command set
    cc |= (0u << 7);  // MPS = 0: 2^(12+0) = 4096-byte pages
    cc |= (0u << 11); // AMS = 000: round robin arbitration
    cc |= (6u << 16); // IOSQES = 2^6 = 64 bytes
    cc |= (4u << 20); // IOCQES = 2^4 = 16 bytes
    cc |= (1u << 0);  // EN = 1
    nvme_reg_write32(NVME_REG_CC, cc);

    int ready = 0;
    for (volatile uint64_t i = 0; i < NVME_TIMEOUT_ITERS; i++) {
        uint32_t csts = nvme_reg_read32(NVME_REG_CSTS);
        if (csts & NVME_CSTS_CFS) {
            print_serial("[NVMe] Controller reported fatal status while enabling.\n");
            return;
        }
        if (csts & NVME_CSTS_RDY) { ready = 1; break; }
    }
    if (!ready) {
        print_serial("[NVMe] Controller failed to become ready.\n");
        return;
    }

    // Mask controller interrupts: this driver polls the Phase Tag bit for
    // every command instead of wiring MSI-X (which real NVMe hardware
    // expects), so there is no handler to deliver an interrupt to.
    nvme_reg_write32(NVME_REG_INTMS, 0xFFFFFFFF);

    void* identify_buf = nvme_alloc_zeroed_pages(1);
    if (!identify_buf) {
        print_serial("[NVMe] Failed to allocate Identify buffer.\n");
        return;
    }
    uint64_t identify_phys = virt_to_phys(identify_buf);

    if (nvme_identify(0x01, 0, identify_phys) == 0) {
        char serial[21];
        char model[41];
        memcpy(serial, (uint8_t*)identify_buf + 4, 20);
        serial[20] = '\0';
        nvme_trim_trailing_spaces(serial, 19);
        memcpy(model, (uint8_t*)identify_buf + 24, 40);
        model[40] = '\0';
        nvme_trim_trailing_spaces(model, 39);

        print_serial("[NVMe] Model: ");
        print_serial(model);
        print_serial(" Serial: ");
        print_serial(serial);
        print_serial("\n");
    } else {
        print_serial("[NVMe] Identify Controller failed.\n");
    }

    memset(identify_buf, 0, PAGE_SIZE);
    if (nvme_identify(0x00, 1, identify_phys) == 0) {
        uint64_t nsze;
        memcpy(&nsze, identify_buf, 8);
        uint8_t flbas = ((uint8_t*)identify_buf)[26] & 0xF;
        uint32_t lbaf;
        memcpy(&lbaf, (uint8_t*)identify_buf + 128 + (uint32_t)flbas * 4, 4);
        uint8_t lbads = (uint8_t)((lbaf >> 16) & 0xFF);
        uint32_t sector_size = (lbads > 0 && lbads < 32) ? (1u << lbads) : 0;

        if (nsze == 0 || sector_size < 512) {
            print_serial("[NVMe] Namespace 1 not usable (empty or unsupported LBA format).\n");
        } else {
            nvme_sector_count = nsze;
            nvme_sector_size = sector_size;
        }
    } else {
        print_serial("[NVMe] Identify Namespace failed.\n");
    }

    pmm_free_pages(identify_buf, 1);

    if (nvme_sector_size == 0) {
        return; // No usable namespace; leave nvme_present at 0.
    }

    if (nvme_create_io_queues() != 0) {
        return;
    }

    bounce_buf = nvme_alloc_zeroed_pages(NVME_BOUNCE_PAGES);
    prp_list = (uint64_t*)nvme_alloc_zeroed_pages(1);
    if (!bounce_buf || !prp_list) {
        print_serial("[NVMe] Failed to allocate DMA buffers.\n");
        return;
    }
    bounce_phys = virt_to_phys(bounce_buf);
    prp_list_phys = virt_to_phys(prp_list);

    nvme_present = 1;

    print_serial("[NVMe] Namespace 1 ready: ");
    char num_buf[32];
    // Sector count/size can each exceed 32-bit range on real hardware, but
    // logging is diagnostic-only here -- clamp for int_to_ascii rather than
    // pull in a 64-bit formatter for a boot log line.
    int_to_ascii((int)(nvme_sector_count > 0x7FFFFFFF ? 0x7FFFFFFF : nvme_sector_count), num_buf);
    print_serial(num_buf);
    print_serial(" sectors x ");
    int_to_ascii((int)nvme_sector_size, num_buf);
    print_serial(num_buf);
    print_serial(" bytes.\n");
}
