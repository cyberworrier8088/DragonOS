#include "mouse.h"
#include "../cpu/ports.h"

int mouse_x = 400;
int mouse_y = 300;
int mouse_l_click = 0;
int mouse_r_click = 0;

static uint8_t mouse_cycle = 0;
static int8_t  mouse_byte[3];

static void mouse_wait(uint8_t a_type) {
    uint32_t timeout = 100000;
    if (a_type == 0) {
        // Wait for input buffer to clear (bit 1 = 0)
        while (timeout--) {
            if ((inb(0x64) & 2) == 0) return;
        }
    } else {
        // Wait for output buffer to fill (bit 0 = 1)
        while (timeout--) {
            if ((inb(0x64) & 1) == 1) return;
        }
    }
}

static void mouse_write(uint8_t a_write) {
    mouse_wait(0);
    outb(0x64, 0xD4); // Write to Auxiliary Device
    mouse_wait(0);
    outb(0x60, a_write);
}

static uint8_t mouse_read(void) {
    mouse_wait(1);
    return inb(0x60);
}

void init_mouse(void) {
    uint8_t status;

    // Enable the auxiliary mouse device
    mouse_wait(0);
    outb(0x64, 0xA8);

    // Read controller command byte
    mouse_wait(0);
    outb(0x64, 0x20);
    mouse_wait(1);
    status = (inb(0x60) | 2); // Enable IRQ12
    status &= ~(1 << 5);      // Clear disable auxiliary clock bit (enables mouse)

    // Write controller command byte
    mouse_wait(0);
    outb(0x64, 0x60);
    mouse_wait(0);
    outb(0x60, status);

    // Tell the mouse to use default settings
    mouse_write(0xF6);
    mouse_read(); // Acknowledge

    // Enable packet streaming
    mouse_write(0xF4);
    mouse_read(); // Acknowledge

    mouse_x = 400;
    mouse_y = 300;
    mouse_l_click = 0;
    mouse_r_click = 0;
    mouse_cycle = 0;
}

void mouse_handler(registers_t* r) {
    (void)r;
    uint8_t status = inb(0x64);
    if (!(status & 0x20)) {
        return; // Data not from mouse
    }

    mouse_byte[mouse_cycle++] = inb(0x60);

    if (mouse_cycle == 3) {
        mouse_cycle = 0;

        uint8_t flags = mouse_byte[0];
        int dx = mouse_byte[1];
        int dy = mouse_byte[2];

        // Sign extension for negative displacement
        if (flags & 0x10) dx |= ~0xFF;
        if (flags & 0x20) dy |= ~0xFF;

        // Invert Y axis
        dy = -dy;

        mouse_x += dx;
        mouse_y += dy;

        // Clip to screen boundaries
        if (mouse_x < 0) mouse_x = 0;
        if (mouse_y < 0) mouse_y = 0;
        if (mouse_x >= 800) mouse_x = 799;
        if (mouse_y >= 600) mouse_y = 599;

        mouse_l_click = flags & 0x01;
        mouse_r_click = flags & 0x02;
    }
}
