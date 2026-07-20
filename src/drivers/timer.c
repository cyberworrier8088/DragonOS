#include "timer.h"
#include "../cpu/idt.h"
#include "../cpu/ports.h"

uint32_t tick = 0;

static void timer_callback(registers_t* regs) {
    (void)regs;
    tick++;
}

void init_timer(uint32_t frequency) {
    /* Register timer handler (IRQ0 corresponds to vector 32) */
    register_interrupt_handler(32, timer_callback);

    /* The value we send to the PIT is the value to divide its input clock
     * (1193180 Hz) by, to yield our desired frequency. */
    uint32_t divisor = 1193180 / frequency;

    /* Send the command byte. */
    outb(0x43, 0x36); // Square wave generator, rate generator, LSB then MSB

    /* Divisor must be sent byte-wise, LSB then MSB. */
    uint8_t l = (uint8_t)(divisor & 0xFF);
    uint8_t h = (uint8_t)((divisor >> 8) & 0xFF);

    /* Send the frequency divisor. */
    outb(0x40, l);
    outb(0x40, h);
}

uint32_t get_ticks(void) {
    return tick;
}

void sleep_ms(uint32_t ms) {
    uint32_t start_tick = tick;
    uint32_t ticks_to_wait = ms / 10;
    if (ticks_to_wait == 0 && ms > 0) ticks_to_wait = 1;
    
    while (tick - start_tick < ticks_to_wait) {
        __asm__ volatile("hlt");
    }
}
