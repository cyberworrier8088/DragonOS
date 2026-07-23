#ifndef E1000_H
#define E1000_H

#include <stdint.h>
#include "pci.h"

// Intel E1000 Vendor/Device IDs
#define E1000_VENDOR_ID 0x8086
#define E1000_DEVICE_ID 0x100E

// Register Offsets
#define REG_CTRL   0x0000
#define REG_STATUS 0x0008
#define REG_EEPROM 0x0014
#define REG_CTRL_EXT 0x0018
#define REG_ICR    0x00C0
#define REG_IMS    0x00D0
#define REG_IMC    0x00D8 // Interrupt Mask Clear

#define REG_RCTL   0x0100
#define REG_TCTL   0x0400

#define REG_RDBAL  0x2800 // RX Desc Base Addr Low
#define REG_RDBAH  0x2804 // RX Desc Base Addr High
#define REG_RDLEN  0x2808 // RX Desc Length
#define REG_RDH    0x2810 // RX Desc Head
#define REG_RDT    0x2818 // RX Desc Tail

#define REG_TDBAL  0x3800 // TX Desc Base Addr Low
#define REG_TDBAH  0x3804 // TX Desc Base Addr High
#define REG_TDLEN  0x3808 // TX Desc Length
#define REG_TDH    0x3810 // TX Desc Head
#define REG_TDT    0x3818 // TX Desc Tail

#define REG_RAL    0x5400 // Receive Addr Low
#define REG_RAH    0x5404 // Receive Addr High

// Structs for DMA Rings
struct e1000_rx_desc {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} __attribute__((packed));

struct e1000_tx_desc {
    uint64_t addr;
    uint16_t length;
    uint8_t cso;
    uint8_t cmd;
    uint8_t status;
    uint8_t css;
    uint16_t special;
} __attribute__((packed));

#define E1000_NUM_RX_DESC 32
#define E1000_NUM_TX_DESC 8

// RCTL Flags
#define RCTL_EN   (1 << 1)  // Receiver Enable
#define RCTL_SBP  (1 << 2)  // Store Bad Packets
#define RCTL_UPE  (1 << 3)  // Unicast Promiscuous Enabled
#define RCTL_MPE  (1 << 4)  // Multicast Promiscuous Enabled
#define RCTL_LPE  (1 << 5)  // Long Packet Reception Enable
#define RCTL_BAM  (1 << 15) // Broadcast Accept Mode
#define RCTL_SECRC (1 << 26) // Strip Ethernet CRC

// TCTL Flags
#define TCTL_EN   (1 << 1)  // Transmit Enable
#define TCTL_PSP  (1 << 3)  // Pad Short Packets

// CMD Flags
#define CMD_EOP   (1 << 0)  // End of Packet
#define CMD_IFCS  (1 << 1)  // Insert FCS (CRC)
#define CMD_RS    (1 << 3)  // Report Status

#define ICR_LSC    (1 << 2)  // Link Status Change
#define ICR_RXO    (1 << 6)  // Receiver Overrun
#define ICR_RXT0   (1 << 7)  // Receiver Timer Interrupt
#define IMS_LSC    (1 << 2)
#define IMS_RXO    (1 << 6)
#define IMS_RXT0   (1 << 7)

void e1000_init(void);
void e1000_send_packet(const void* p_data, uint16_t p_len);
void e1000_send_broadcast(const char* payload);
uint8_t* e1000_get_mac_address(void);

#endif
