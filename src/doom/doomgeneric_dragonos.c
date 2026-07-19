#include "doomgeneric.h"
#include "../drivers/timer.h"
#include "../libc/string.h"
#include "../libc/stdio.h"
#include "../drivers/serial.h"
#include "../shell/gui.h"
#include "../libc/stdlib.h"

// Expose GUI buffer from gui.c
extern uint32_t* doom_window_buffer;
extern int doom_running;

// Key Queue Implementation
typedef struct {
    int pressed;
    unsigned char key;
} doom_key_event_t;

static doom_key_event_t key_queue[256];
static int key_head = 0;
static int key_tail = 0;

static void push_doom_key(int pressed, unsigned char key) {
    int next = (key_head + 1) % 256;
    if (next != key_tail) {
        key_queue[key_head].pressed = pressed;
        key_queue[key_head].key = key;
        key_head = next;
    }
}

int DG_GetKey(int* pressed, unsigned char* key) {
    if (key_tail == key_head) return 0;
    *pressed = key_queue[key_tail].pressed;
    *key = key_queue[key_tail].key;
    key_tail = (key_tail + 1) % 256;
    return 1;
}

void doom_handle_keyboard_raw(uint8_t scancode) {
    static int e0_received = 0;
    if (scancode == 0xE0) {
        e0_received = 1;
        return;
    }
    
    int pressed = !(scancode & 0x80);
    uint8_t code = scancode & 0x7F;
    unsigned char doom_key = 0;
    
    switch (code) {
        case 0x01: doom_key = 27; break;   // Esc -> escape
        case 0x1C: doom_key = 13; break;   // Enter -> enter
        case 0x39: doom_key = 0xa2; break; // Space -> use (KEY_USE)
        case 0x1D: doom_key = 0xa3; break; // Left Ctrl -> fire (KEY_FIRE)
        case 0x2A: doom_key = 0xb6; break; // Left Shift -> run (KEY_RSHIFT)
        case 0x0E: doom_key = 0x7f; break; // Backspace
        case 0x0F: doom_key = 9; break;    // Tab -> map

        // Weapon switching (1-9, 0)
        case 0x02: doom_key = '1'; break;
        case 0x03: doom_key = '2'; break;
        case 0x04: doom_key = '3'; break;
        case 0x05: doom_key = '4'; break;
        case 0x06: doom_key = '5'; break;
        case 0x07: doom_key = '6'; break;
        case 0x08: doom_key = '7'; break;
        case 0x09: doom_key = '8'; break;
        case 0x0a: doom_key = '9'; break;
        case 0x0b: doom_key = '0'; break;

        // WASD
        case 0x11: doom_key = 'W'; break;
        case 0x1E: doom_key = 'A'; break;
        case 0x1F: doom_key = 'S'; break;
        case 0x20: doom_key = 'D'; break;
        case 0x12: doom_key = 'E'; break; // use

        // Arrow Keys
        case 0x48: doom_key = 0xad; break; // Up Arrow
        case 0x50: doom_key = 0xaf; break; // Down Arrow
        case 0x4B: doom_key = 0xac; break; // Left Arrow
        case 0x4D: doom_key = 0xae; break; // Right Arrow

        default: break;
    }
    e0_received = 0;
    if (doom_key != 0) {
        push_doom_key(pressed, doom_key);
    }
}
// Doom Generic callbacks
void DG_Init() {
    print_serial("[Doom] DG_Init called.\n");
    // Clear screen buffer
    if (doom_window_buffer) {
        memset(doom_window_buffer, 0, 640 * 400 * 4);
    }
    doom_running = 1;
}

extern void gui_handle_mouse(int mx, int my, int click, int r_click);
extern void gui_draw(void);
extern int mouse_x, mouse_y, mouse_l_click, mouse_r_click;
extern gui_window_t* windows;
extern jmp_buf doom_exit_jmp;

void DG_DrawFrame() {
    // Copy the rendered frame into our GUI's window buffer
    if (DG_ScreenBuffer && doom_window_buffer) {
        memcpy(doom_window_buffer, DG_ScreenBuffer, 640 * 400 * 4);
    }
    
    // Cooperative GUI tick
    gui_handle_mouse(mouse_x, mouse_y, mouse_l_click, mouse_r_click);
    gui_draw();
    
    if (windows && windows[4].closed) {
        print_serial("[Doom] Window closed. Exiting Doom...\n");
        doom_running = 0;
        longjmp(doom_exit_jmp, 1);
    }
}

void DG_SleepMs(uint32_t ms) {
    if (ms == 0) return;
    if (ms < 10) {
        // High-precision pause loop for sub-tick sleeps
        for (volatile uint32_t i = 0; i < ms * 10000; i++) {
            __asm__ volatile("pause");
        }
        return;
    }
    uint32_t start = get_ticks();
    uint32_t ticks_to_wait = ms / 10;
    while (get_ticks() - start < ticks_to_wait) {
        __asm__ volatile("pause");
    }
}

uint32_t DG_GetTicksMs() {
    // Convert 100Hz ticks to milliseconds
    return get_ticks() * 10;
}

void DG_SetWindowTitle(const char* title) {
    (void)title;
}
