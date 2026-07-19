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
        case 0x01: doom_key = 27; break; // Esc -> escape
        case 0x1C: doom_key = 13; break; // Enter -> enter
        case 0x39: doom_key = ' '; break; // Space -> use
        case 0x1D: doom_key = 0xa0; break; // Left Ctrl -> fire (ctrl)
        case 0x2A: doom_key = 0xa1; break; // Left Shift -> run (shift)
        
        // WASD
        case 0x11: doom_key = 'w'; break;
        case 0x1E: doom_key = 'a'; break;
        case 0x1F: doom_key = 's'; break;
        case 0x20: doom_key = 'd'; break;
        case 0x12: doom_key = 'e'; break; // use
        
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
    // Use the kernel timer to sleep
    uint32_t start = get_ticks();
    uint32_t ticks_to_wait = ms / 10; // PIT runs at 100Hz (10ms per tick)
    if (ticks_to_wait == 0) ticks_to_wait = 1;
    while (get_ticks() - start < ticks_to_wait) {
        __asm__ volatile("hlt");
    }
}

uint32_t DG_GetTicksMs() {
    // Convert 100Hz ticks to milliseconds
    return get_ticks() * 10;
}

void DG_SetWindowTitle(const char* title) {
    (void)title;
}
