#include "quakegeneric.h"
#include "../libc/stdlib.h"
#include "../libc/string.h"
#include "../shell/gui.h"
#include "../drivers/graphics.h"
#include "../drivers/mouse.h"
// No setjmp.h needed

uint32_t* quake_window_buffer = 0;
int quake_running = 0;
jmp_buf quake_exit_jmp;

static uint32_t quake_palette[256];

// Key Queue Implementation
typedef struct {
    int pressed;
    int key;
} quake_key_event_t;

static quake_key_event_t key_queue[256];
static int key_head = 0;
static int key_tail = 0;
static int last_l_click = 0;

void QG_Init(void)
{
    __asm__ volatile("cli");
    key_head = key_tail = 0;
    mouse_dx = mouse_dy = 0;
    last_l_click = mouse_l_click;
    __asm__ volatile("sti");
}

static void push_quake_key(int pressed, int key) {
    int next = (key_head + 1) % 256;
    if (next != key_tail) {
        key_queue[key_head].pressed = pressed;
        key_queue[key_head].key = key;
        key_head = next;
    }
}

void quake_handle_keyboard_raw(uint8_t scancode) {
    if (scancode == 0xE0) {
        return;
    }
    
    int pressed = !(scancode & 0x80);
    uint8_t code = scancode & 0x7F;
    int quake_key = 0;
    
    switch (code) {
        case 0x01: quake_key = 27; break;   // Esc -> K_ESCAPE
        case 0x1C: quake_key = 13; break;   // Enter -> K_ENTER
        case 0x39: quake_key = 32; break;   // Space -> K_SPACE
        case 0x1D: quake_key = 133; break;  // Left Ctrl -> K_CTRL
        case 0x2A: quake_key = 134; break;  // Left Shift -> K_SHIFT
        case 0x0E: quake_key = 127; break;  // Backspace -> K_BACKSPACE
        case 0x0F: quake_key = 9; break;    // Tab -> K_TAB

        // Numbers
        case 0x02: quake_key = '1'; break;
        case 0x03: quake_key = '2'; break;
        case 0x04: quake_key = '3'; break;
        case 0x05: quake_key = '4'; break;
        case 0x06: quake_key = '5'; break;
        case 0x07: quake_key = '6'; break;
        case 0x08: quake_key = '7'; break;
        case 0x09: quake_key = '8'; break;
        case 0x0a: quake_key = '9'; break;
        case 0x0b: quake_key = '0'; break;

        // Letters
        case 0x11: quake_key = 'w'; break;
        case 0x1E: quake_key = 'a'; break;
        case 0x1F: quake_key = 's'; break;
        case 0x20: quake_key = 'd'; break;
        case 0x12: quake_key = 'e'; break;
        case 0x10: quake_key = 'q'; break;
        case 0x2C: quake_key = 'z'; break;
        case 0x2D: quake_key = 'x'; break;
        case 0x2E: quake_key = 'c'; break;
        
        // Punctuation
        case 0x29: quake_key = '`'; break;

        // Arrow Keys
        case 0x48: quake_key = 128; break; // Up Arrow -> K_UPARROW
        case 0x50: quake_key = 129; break; // Down Arrow -> K_DOWNARROW
        case 0x4B: quake_key = 130; break; // Left Arrow -> K_LEFTARROW
        case 0x4D: quake_key = 131; break; // Right Arrow -> K_RIGHTARROW

        default: break;
    }
    if (quake_key != 0) {
        push_quake_key(pressed, quake_key);
    }
}

int QG_GetKey(int *down, int *key)
{
    if (key_tail == key_head) return 0;
    *down = key_queue[key_tail].pressed;
    *key = key_queue[key_tail].key;
    key_tail = (key_tail + 1) % 256;
    return 1;
}

void QG_GetMouseMove(int *x, int *y)
{
    __asm__ volatile("cli");
    *x = mouse_dx;
    *y = mouse_dy;
    mouse_dx = 0;
    mouse_dy = 0;
    __asm__ volatile("sti");
}

void QG_GetJoyAxes(float *axes)
{
    axes[0] = 0;
    axes[1] = 0;
    axes[2] = 0;
    axes[3] = 0;
}

void QG_Quit(void)
{
    quake_running = 0;
    longjmp(quake_exit_jmp, 1);
}

void QG_DrawFrame(void *pixels)
{
    if (!quake_window_buffer) return;

    uint8_t* p = (uint8_t*)pixels;
    
    // Scale 320x240 to 640x480 (2x scale)
    for (int y = 0; y < 240; y++) {
        for (int x = 0; x < 320; x++) {
            uint32_t color = quake_palette[p[y * 320 + x]];
            quake_window_buffer[(y * 2) * 640 + (x * 2)] = color;
            quake_window_buffer[(y * 2) * 640 + (x * 2 + 1)] = color;
            quake_window_buffer[(y * 2 + 1) * 640 + (x * 2)] = color;
            quake_window_buffer[(y * 2 + 1) * 640 + (x * 2 + 1)] = color;
        }
    }
    
    extern int mouse_l_click;
    extern gui_window_t* windows;
    
    if (mouse_l_click != last_l_click) {
        __asm__ volatile("cli");
        push_quake_key(mouse_l_click, 200); // 200 = K_MOUSE1
        __asm__ volatile("sti");
        last_l_click = mouse_l_click;
    }
    
    if (windows && windows[7].closed) {
        quake_running = 0;
        longjmp(quake_exit_jmp, 1);
    }
}

void QG_SetPalette(unsigned char palette[768])
{
    for (int i = 0; i < 256; i++) {
        uint8_t r = palette[i * 3 + 0];
        uint8_t g = palette[i * 3 + 1];
        uint8_t b = palette[i * 3 + 2];
        quake_palette[i] = (r << 16) | (g << 8) | b;
    }
}
