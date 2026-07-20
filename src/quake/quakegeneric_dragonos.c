#include "quakegeneric.h"
#include "../libc/stdlib.h"
#include "../libc/string.h"
#include "../shell/gui.h"
#include "../drivers/graphics.h"
#include "../drivers/mouse.h"
// No setjmp.h needed

uint32_t* quake_window_buffer = 0;
int quake_running = 0;
int quake_initialized = 0;
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

/* Full PS/2 scancode set 1 -> Quake key map for make-codes 0x00-0x58.
 * Quake expects lowercased ASCII for printable keys and the K_* codes
 * from quakekeys.h for everything else. 0 = no mapping. The previous
 * switch only covered 9 letters, so keys like 'y' (quit confirmation)
 * and most of the alphabet were dead. */
static const unsigned char quake_scancode_map[0x59] = {
    /* 0x00 */ 0,    27,  '1', '2', '3', '4', '5', '6',
    /* 0x08 */ '7',  '8', '9', '0', '-', '=', 127, 9,
    /* 0x10 */ 'q',  'w', 'e', 'r', 't', 'y', 'u', 'i',
    /* 0x18 */ 'o',  'p', '[', ']', 13,  133, 'a', 's',
    /* 0x20 */ 'd',  'f', 'g', 'h', 'j', 'k', 'l', ';',
    /* 0x28 */ '\'', '`', 134, '\\','z', 'x', 'c', 'v',
    /* 0x30 */ 'b',  'n', 'm', ',', '.', '/', 134, '*',
    /* 0x38 */ 132,  32,  0,   135, 136, 137, 138, 139, /* 0x3A CapsLock ignored */
    /* 0x40 */ 140,  141, 142, 143, 144, 0,   0,   151, /* NumLock/Scroll ignored */
    /* 0x48 */ 128,  150, '-', 130, '5', 131, '+', 152,
    /* 0x50 */ 129,  149, 147, 148, 0,   0,   0,   145,
    /* 0x58 */ 146,
};

void quake_handle_keyboard_raw(uint8_t scancode) {
    /* E0/E1-prefixed keys (arrows, right ctrl/alt, ins/del block) share
     * make codes with the numpad; the shared table entries already map
     * both variants to the key Quake cares about, so the prefix byte is
     * simply skipped. */
    if (scancode == 0xE0 || scancode == 0xE1) {
        return;
    }

    int pressed = !(scancode & 0x80);
    uint8_t code = scancode & 0x7F;

    if (code >= sizeof(quake_scancode_map)) {
        return;
    }

    int quake_key = quake_scancode_map[code];
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
