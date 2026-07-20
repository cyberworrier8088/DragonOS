#include "keyboard.h"
#include "../cpu/ports.h"
#include "../cpu/idt.h"

extern void shell_input_char(char c);

static int shift_pressed = 0;
static int caps_lock = 0;
static int e0_prefix = 0;

static const char scancode_map[] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8',	/* 9 */
  '9', '0', '-', '=', '\b',	/* Backspace */
  '\t',			/* Tab */
  'q', 'w', 'e', 'r',	/* 19 */
  't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',	/* Enter key */
    0,			/* 29   - Control */
  'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',	/* 39 */
 '\'', '`',   0,		/* Left shift */
 '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.',	/* 49 */
  '/',   0,				/* Right shift */
  '*',
    0,	/* Alt */
  ' ',	/* Space bar */
    0,	/* Caps lock */
    0,	/* 59 - F1 key ... > */
    0,   0,   0,   0,   0,   0,   0,   0,
    0,	/* < ... F10 */
    0,	/* 69 - Num lock*/
    0,	/* Scroll Lock */
    0,	/* Home key */
    0,	/* Up Arrow */
    0,	/* Page Up */
  '-',
    0,	/* Left Arrow */
    0,
    0,	/* Right Arrow */
  '+',
    0,	/* 79 - End key*/
    0,	/* Down Arrow */
    0,	/* Page Down */
    0,	/* Insert Key */
    0,	/* Delete Key */
    0,   0,   0,
    0,	/* F11 Key */
    0,	/* F12 Key */
    0,	/* All other keys are undefined */
};

static const char scancode_shift_map[] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*',	/* 9 */
  '(', ')', '_', '+', '\b',	/* Backspace */
  '\t',			/* Tab */
  'Q', 'W', 'E', 'R',	/* 19 */
  'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',	/* Enter key */
    0,			/* 29   - Control */
  'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',	/* 39 */
 '"', '~',   0,		/* Left shift */
  '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>',	/* 49 */
  '?',   0,				/* Right shift */
  '*',
    0,	/* Alt */
  ' ',	/* Space bar */
    0,	/* Caps lock */
};

static void keyboard_callback(registers_t* regs) {
    (void)regs;
    uint8_t scancode = inb(0x60);

    // Call Doom and Quake raw keyboard hooks
    extern void doom_handle_keyboard_raw(uint8_t scancode);
    doom_handle_keyboard_raw(scancode);
    extern void quake_handle_keyboard_raw(uint8_t scancode);
    quake_handle_keyboard_raw(scancode);

    if (scancode == 0xE0) {
        e0_prefix = 1;
        return;
    }

    /* Check if it's a key release (break code) */
    if (scancode & 0x80) {
        e0_prefix = 0; // Reset prefix on release
        uint8_t released_scancode = scancode & 0x7F;
        if (released_scancode == 0x2A || released_scancode == 0x36) {
            shift_pressed = 0;
        }
        return;
    }

    /* Key press (make code) */
    if (scancode == 0x2A || scancode == 0x36) {
        shift_pressed = 1;
        return;
    }

    if (scancode == 0x3A) {
        caps_lock = !caps_lock;
        e0_prefix = 0;
        return;
    }

    /* Only type characters if it is NOT an E0 extended key (like arrows) */
    if (!e0_prefix && scancode < sizeof(scancode_map)) {
        char ascii = 0;
        if (shift_pressed) {
            if (scancode < sizeof(scancode_shift_map)) {
                ascii = scancode_shift_map[scancode];
            } else {
                ascii = scancode_map[scancode];
            }
        } else {
            ascii = scancode_map[scancode];
        }

        /* Apply Caps Lock state to letters only */
        if (ascii >= 'a' && ascii <= 'z') {
            if (caps_lock ^ shift_pressed) {
                ascii = ascii - 'a' + 'A';
            }
        } else if (ascii >= 'A' && ascii <= 'Z') {
            if (caps_lock ^ shift_pressed) {
                ascii = ascii - 'A' + 'a';
            }
        }

        if (ascii != 0) {
            extern void gui_handle_keyboard(char c);
            gui_handle_keyboard(ascii);
        }
    }
    
    e0_prefix = 0;
}

void init_keyboard(void) {
    register_interrupt_handler(33, keyboard_callback); // Keyboard is IRQ1 -> Int 33
}
