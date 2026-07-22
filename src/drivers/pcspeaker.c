#include "pcspeaker.h"
#include "../cpu/ports.h"
#include "timer.h"

#define PIT_CHANNEL2   0x42
#define PIT_COMMAND    0x43
#define KBC_PORT_B     0x61   // bit0: timer2 gate, bit1: speaker data enable

void pcspeaker_play(uint32_t freq) {
    if (freq == 0) return;
    uint32_t divisor = 1193180 / freq;

    // Channel 2, access lo/hi byte, mode 3 (square wave), binary.
    outb(PIT_COMMAND, 0xB6);
    outb(PIT_CHANNEL2, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL2, (uint8_t)((divisor >> 8) & 0xFF));

    // Connect channel 2 to the speaker (set the low two bits of port 0x61).
    uint8_t tmp = inb(KBC_PORT_B);
    if ((tmp & 0x03) != 0x03) {
        outb(KBC_PORT_B, tmp | 0x03);
    }
}

void pcspeaker_stop(void) {
    // Clear the gate/data bits; leave the rest of the port untouched.
    outb(KBC_PORT_B, inb(KBC_PORT_B) & 0xFC);
}

void pcspeaker_beep(uint32_t freq, uint32_t ms) {
    pcspeaker_play(freq);
    sleep_ms(ms);
    pcspeaker_stop();
}
