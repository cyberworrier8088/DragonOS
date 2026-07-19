#include "gui.h"
#include "../drivers/graphics.h"
#include "../drivers/mouse.h"
#include "../drivers/serial.h"
#include "../cpu/ports.h"
#include "../libc/string.h"
#include "shell.h"
#include "../mm/pmm.h"
#include "../mm/kheap.h"
#include "../drivers/pci.h"

#include "../libc/stdlib.h"

gui_window_t* windows;
int active_win_id = -1;

uint32_t* doom_window_buffer = 0;
int doom_running = 0;
jmp_buf doom_exit_jmp;

/* Start Menu State */
int start_menu_open = 0;

/* Clock / Time counter */
static uint32_t ticks_counter = 0;
static int hours = 12;
static int minutes = 0;
static int seconds = 0;
static char time_str[12] = "12:00 PM";
static char date_str[16] = "Jul 19";

/* Terminal Window Output Buffer */
static char term_lines[18][80];
static int term_line_count = 0;
static char command_buffer[128] = "";
static int cmd_buf_idx = 0;

/* Calculator Window State */
static char calc_buf[32] = "";
static int op1 = 0;
static int op2 = 0;
static char calc_op = 0;
static int calc_new_number = 1;

/* System Monitor State (scrolling line graph) */
static int sysmon_values[60];
static int val_timer = 0;

/* ============================================================
 * Windows 11 Style Mouse Cursor (16x22)
 * X = black outline, . = white fill, S = shadow (gray)
 * ============================================================ */
static const char cursor_bitmap[22][16] = {
    "X               ",
    "XX              ",
    "X.X             ",
    "X..X            ",
    "X...X           ",
    "X....X          ",
    "X.....X         ",
    "X......X        ",
    "X.......X       ",
    "X........X      ",
    "X.........X     ",
    "X..........X    ",
    "X.......XXXX    ",
    "X...X...X       ",
    "X..X X...X      ",
    "X.X   X...X     ",
    "XX     X...X    ",
    "X       X..X    ",
    "         X.X    ",
    "          XX    ",
    "                ",
    "                "
};

/* ============================================================
 * Windows 11 Color Palette
 * ============================================================ */
#define WIN11_ACCENT         0x0078D4  /* Windows 11 blue accent */
#define WIN11_ACCENT_LIGHT   0x429CE3
#define WIN11_ACCENT_DARK    0x005A9E
#define WIN11_TITLEBAR       0xF3F3F3  /* Light mica-style titlebar */
#define WIN11_TITLEBAR_INACTIVE 0xFBFBFB
#define WIN11_WINDOW_BG      0xFAFAFA  /* Window content background */
#define WIN11_BORDER         0xE0E0E0  /* Subtle window border */
#define WIN11_TEXT_PRIMARY    0x1A1A1A  /* Primary text */
#define WIN11_TEXT_SECONDARY  0x6B6B6B  /* Secondary/muted text */
#define WIN11_CLOSE_HOVER    0xC42B1C  /* Close button red */
#define WIN11_TASKBAR_BG     0x1C1C1C  /* Dark taskbar */
#define WIN11_CARD_BG        0xFFFFFF  /* Card surface */
#define WIN11_CARD_BORDER    0xEBEBEB
#define WIN11_SURFACE_DARK   0x2D2D2D  /* Dark surfaces (terminal, etc) */
#define WIN11_TERMINAL_BG    0x0C0C0C  /* Terminal black */
#define WIN11_GREEN          0x0F7B0F  /* Status green */
#define WIN11_SEARCH_BG      0xF5F5F5  /* Search bar background */

/* Custom print helper for Terminal GUI Window */
void gui_write_char(char c) {
    if (c == '\n') {
        term_line_count++;
        if (term_line_count >= 18) {
            for (int i = 0; i < 17; i++) {
                memcpy(term_lines[i], term_lines[i + 1], 80);
            }
            memset(term_lines[17], 0, 80);
            term_line_count = 17;
        }
    } else if (c == '\b') {
        int len = strlen(term_lines[term_line_count]);
        if (len > 0) {
            term_lines[term_line_count][len - 1] = '\0';
        }
    } else {
        int len = strlen(term_lines[term_line_count]);
        if (len < 79) {
            term_lines[term_line_count][len] = c;
            term_lines[term_line_count][len + 1] = '\0';
        }
    }
}

void gui_write_string(const char* str) {
    while (*str) {
        gui_write_char(*str);
        str++;
    }
}

/* Execute shell command in the GUI context */
static void gui_execute_command(const char* cmd) {
    if (strcmp(cmd, "help") == 0) {
        gui_write_string("Available commands:\n");
        gui_write_string("  help        Show this help menu\n");
        gui_write_string("  about       Display OS information\n");
        gui_write_string("  clear       Clear the screen\n");
        gui_write_string("  ticks       Show system timer ticks\n");
        gui_write_string("  ping        Test command response\n");
        gui_write_string("  echo <msg>  Echo input text back\n");
        gui_write_string("  reboot      Reboot the computer\n");
        gui_write_string("  halt        Halt the CPU safely\n");
    } else if (strcmp(cmd, "about") == 0) {
        gui_write_string("DragonOS x86_64 Kernel\n");
        gui_write_string("Build: July 2026\n");
        gui_write_string("Bootloader: Limine Protocol\n");
        gui_write_string("GUI: Windows 11 Fluent Design\n");
    } else if (strcmp(cmd, "clear") == 0) {
        memset(term_lines, 0, sizeof(term_lines));
        term_line_count = 0;
    } else if (strcmp(cmd, "ticks") == 0) {
        char tick_str[32];
        int_to_ascii(ticks_counter, tick_str);
        gui_write_string("Ticks: ");
        gui_write_string(tick_str);
        gui_write_string("\n");
    } else if (strcmp(cmd, "ping") == 0) {
        gui_write_string("pong!\n");
    } else if (strncmp(cmd, "echo ", 5) == 0) {
        gui_write_string(cmd + 5);
        gui_write_string("\n");
    } else if (strcmp(cmd, "reboot") == 0) {
        gui_write_string("Rebooting...\n");
        outb(0x64, 0xFE);
    } else if (strcmp(cmd, "halt") == 0) {
        gui_write_string("System halted.\n");
        __asm__ volatile("cli; hlt");
    } else if (strlen(cmd) > 0) {
        gui_write_string("Unknown command. Type 'help'.\n");
    }
}

void init_gui(void) {
    /* Dynamically allocate windows using KHeap */
    windows = (gui_window_t*)kmalloc(sizeof(gui_window_t) * MAX_WINDOWS);

    /* 0: Computer */
    windows[0].x = 80;
    windows[0].y = 60;
    windows[0].w = 360;
    windows[0].h = 280;
    strcpy(windows[0].title, "System Information");
    windows[0].active = 0;
    windows[0].closed = 1;
    windows[0].minimized = 0;
    windows[0].dragging = 0;
    windows[0].id = 0;

    /* 1: Terminal */
    windows[1].x = 350;
    windows[1].y = 80;
    windows[1].w = 440;
    windows[1].h = 340;
    strcpy(windows[1].title, "Terminal");
    windows[1].active = 0;
    windows[1].closed = 1;
    windows[1].minimized = 0;
    windows[1].dragging = 0;
    windows[1].id = 1;
    active_win_id = -1;

    /* 2: Calculator */
    windows[2].x = 120;
    windows[2].y = 220;
    windows[2].w = 220;
    windows[2].h = 280;
    strcpy(windows[2].title, "Calculator");
    windows[2].active = 0;
    windows[2].closed = 1;
    windows[2].minimized = 0;
    windows[2].dragging = 0;
    windows[2].id = 2;

    /* 3: System Monitor */
    windows[3].x = 420;
    windows[3].y = 280;
    windows[3].w = 360;
    windows[3].h = 220;
    strcpy(windows[3].title, "Task Manager");
    windows[3].active = 0;
    windows[3].closed = 1;
    windows[3].minimized = 0;
    windows[3].dragging = 0;
    windows[3].id = 3;

    /* 4: Doom */
    windows[4].x = 80;
    windows[4].y = 40;
    windows[4].w = 640;
    windows[4].h = 432;
    strcpy(windows[4].title, "DOOM");
    windows[4].active = 0;
    windows[4].closed = 1;
    windows[4].minimized = 0;
    windows[4].dragging = 0;
    windows[4].id = 4;

    doom_window_buffer = (uint32_t*)kmalloc(640 * 400 * 4);

    /* Initialize terminal buffer */
    memset(term_lines, 0, sizeof(term_lines));
    strcpy(term_lines[0], "DragonOS Terminal v2.0");
    strcpy(term_lines[1], "Type 'help' for commands.");
    strcpy(term_lines[2], "");
    term_line_count = 3;

    /* Initialize system monitor points */
    for (int i = 0; i < 60; i++) {
        sysmon_values[i] = 100;
    }

    strcpy(calc_buf, "0");
}

/* ============================================================
 * Draw Windows 11 Start Logo (4-pane grid)
 * ============================================================ */
static void draw_win11_logo(int cx, int cy, uint32_t color) {
    int s = 4; /* square size */
    int g = 2; /* gap */
    draw_rect(cx - s - g/2, cy - s - g/2, s, s, color);
    draw_rect(cx + g/2,     cy - s - g/2, s, s, color);
    draw_rect(cx - s - g/2, cy + g/2,     s, s, color);
    draw_rect(cx + g/2,     cy + g/2,     s, s, color);
}

/* ============================================================
 * Draw a Windows 11 style window
 * ============================================================ */
static void draw_window_chrome(gui_window_t* win) {
    int x = win->x, y = win->y, w = win->w, h = win->h;
    int is_active = (active_win_id == win->id);
    int titlebar_h = 32;
    int radius = 8;

    /* Drop shadow */
    draw_rounded_rect_translucent(x + 2, y + 2, w, h, radius, 0x000000, 30);
    draw_rounded_rect_translucent(x + 1, y + 1, w, h, radius, 0x000000, 15);

    /* Window body with rounded corners */
    draw_rounded_rect(x, y, w, h, radius, WIN11_WINDOW_BG);

    /* Titlebar */
    uint32_t tb_color = is_active ? WIN11_TITLEBAR : WIN11_TITLEBAR_INACTIVE;
    draw_rounded_rect(x, y, w, titlebar_h, radius, tb_color);
    /* Fill in the bottom of titlebar (flat, no bottom rounding) */
    draw_rect(x, y + radius, w, titlebar_h - radius, tb_color);

    /* Border */
    uint32_t border_c = is_active ? 0xCCCCCC : WIN11_BORDER;
    draw_rounded_rect_outline(x, y, w, h, radius, border_c);

    /* Title text */
    uint32_t title_c = is_active ? WIN11_TEXT_PRIMARY : WIN11_TEXT_SECONDARY;
    draw_string(x + 12, y + 8, win->title, title_c);

    /* Window control buttons (right side of titlebar) */
    int btn_w = 46;
    int btn_h = 28;
    int btn_y = y + 2;

    /* Close button hover check */
    int close_x = x + w - btn_w;
    int mouse_in_close = (mouse_x >= close_x && mouse_x < close_x + btn_w && mouse_y >= btn_y && mouse_y < btn_y + btn_h);
    uint32_t close_bg = mouse_in_close ? 0xE81123 : tb_color;
    uint32_t close_fg = mouse_in_close ? 0xFFFFFF : (is_active ? 0x808080 : 0x505050);

    /* Draw close button background if hovered */
    if (mouse_in_close) {
        draw_rect(close_x, btn_y, btn_w - radius, btn_h, close_bg);
        draw_rounded_rect(close_x, btn_y, btn_w, btn_h, radius, close_bg);
    }
    /* X symbol */
    int cx = close_x + btn_w / 2;
    int cy = btn_y + btn_h / 2;
    for (int d = -4; d <= 4; d++) {
        draw_pixel(cx + d, cy + d, close_fg);
        draw_pixel(cx + d, cy - d, close_fg);
    }

    /* Maximize button hover check */
    int max_x = close_x - btn_w;
    int mouse_in_max = (mouse_x >= max_x && mouse_x < max_x + btn_w && mouse_y >= btn_y && mouse_y < btn_y + btn_h);
    uint32_t max_bg = mouse_in_max ? 0x3D3D3D : tb_color;
    if (mouse_in_max) {
        draw_rect(max_x, btn_y, btn_w, btn_h, max_bg);
    }
    draw_rect_outline(max_x + btn_w/2 - 5, btn_y + btn_h/2 - 4, 10, 8, WIN11_TEXT_SECONDARY);

    /* Minimize button hover check */
    int min_x = max_x - btn_w;
    int mouse_in_min = (mouse_x >= min_x && mouse_x < min_x + btn_w && mouse_y >= btn_y && mouse_y < btn_y + btn_h);
    uint32_t min_bg = mouse_in_min ? 0x3D3D3D : tb_color;
    if (mouse_in_min) {
        draw_rect(min_x, btn_y, btn_w, btn_h, min_bg);
    }
    draw_hline(min_x + btn_w/2 - 5, btn_y + btn_h/2, 10, WIN11_TEXT_SECONDARY);
}

/* ============================================================
 * Draw Desktop Icons (Windows 11 flat style)
 * ============================================================ */
static void draw_desktop_icons(void) {
    /* Icon layout: vertical stack on left side */
    struct { const char* label; uint32_t bg; uint32_t fg; const char* symbol; } icons[5] = {
        {"System",   0x0078D4, 0xFFFFFF, "PC"},
        {"Terminal", 0x0C0C0C, 0x00FF00, ">_"},
        {"Calc",     0x202020, 0xFFFFFF, "+-"},
        {"Monitor",  0x1A1A1A, 0x00CC6A, "/\\"},
        {"DOOM",     0xC21807, 0xFFFFFF, "DM"},
    };

    for (int i = 0; i < 5; i++) {
        int ix = 30;
        int iy = 30 + i * 90;

        /* Hover selection card */
        int mouse_in_icon = (mouse_x >= ix - 6 && mouse_x < ix + 48 + 6 && mouse_y >= iy - 6 && mouse_y < iy + 76);
        if (mouse_in_icon) {
            draw_rounded_rect_translucent(ix - 6, iy - 6, 48 + 12, 70 + 12, 6, 0xFFFFFF, 35);
        }

        /* Icon tile background - rounded square */
        draw_rounded_rect(ix, iy, 48, 48, 8, icons[i].bg);
        /* Icon symbol */
        draw_string(ix + 10, iy + 16, icons[i].symbol, icons[i].fg);
        /* Label below icon */
        draw_string(ix - 2, iy + 54, icons[i].label, 0xFFFFFF);
    }
}

/* ============================================================
 * Draw Window Content
 * ============================================================ */
static void draw_window_content(gui_window_t* win) {
    int x = win->x, y = win->y, w = win->w, h = win->h;
    int content_y = y + 32;
    int content_h = h - 32;

    if (win->id == 0) {
        /* ---- System Information ---- */
        /* Info cards on light background */
        draw_rect(x + 1, content_y, w - 2, content_h - 1, WIN11_WINDOW_BG);

        /* CPU card */
        draw_rounded_rect(x + 12, content_y + 12, w - 24, 36, 6, WIN11_CARD_BG);
        draw_rounded_rect_outline(x + 12, content_y + 12, w - 24, 36, 6, WIN11_CARD_BORDER);
        draw_string(x + 22, content_y + 14, "CPU", WIN11_ACCENT);
        draw_string(x + 22, content_y + 30, "64-Bit Intel Emulator", WIN11_TEXT_PRIMARY);

        /* RAM card */
        draw_rounded_rect(x + 12, content_y + 56, w - 24, 36, 6, WIN11_CARD_BG);
        draw_rounded_rect_outline(x + 12, content_y + 56, w - 24, 36, 6, WIN11_CARD_BORDER);
        draw_string(x + 22, content_y + 58, "Memory", WIN11_ACCENT);
        
        char mem_str[64];
        char num_buf[32];
        strcpy(mem_str, "Used: ");
        int_to_ascii(pmm_used_memory / 1024 / 1024, num_buf);
        strcat(mem_str, num_buf);
        strcat(mem_str, " MB / Total: ");
        int_to_ascii(pmm_total_memory / 1024 / 1024, num_buf);
        strcat(mem_str, num_buf);
        strcat(mem_str, " MB");
        draw_string(x + 22, content_y + 74, mem_str, WIN11_TEXT_PRIMARY);

        /* OS card */
        draw_rounded_rect(x + 12, content_y + 100, w - 24, 36, 6, WIN11_CARD_BG);
        draw_rounded_rect_outline(x + 12, content_y + 100, w - 24, 36, 6, WIN11_CARD_BORDER);
        draw_string(x + 22, content_y + 102, "System", WIN11_ACCENT);
        draw_string(x + 22, content_y + 118, "DragonOS 64-bit Limine", WIN11_TEXT_PRIMARY);

        /* PCI Devices card */
        draw_rounded_rect(x + 12, content_y + 144, w - 24, 90, 6, WIN11_CARD_BG);
        draw_rounded_rect_outline(x + 12, content_y + 144, w - 24, 90, 6, WIN11_CARD_BORDER);
        draw_string(x + 22, content_y + 146, "PCI Hardware", WIN11_ACCENT);

        int line_offset = 0;
        pci_device_t* curr = pci_devices_head;
        while (curr && line_offset < 4) {
            char pci_str[64];
            strcpy(pci_str, curr->class_name);
            draw_string(x + 22, content_y + 162 + line_offset * 14, pci_str, WIN11_TEXT_PRIMARY);
            curr = curr->next;
            line_offset++;
        }
        if (line_offset == 0) {
            draw_string(x + 22, content_y + 162, "No devices scanned.", WIN11_TEXT_PRIMARY);
        }
    }
    else if (win->id == 1) {
        /* ---- Terminal ---- */
        int pad = 4;
        draw_rounded_rect(x + pad, content_y + pad, w - pad * 2, content_h - pad * 2 - 2, 4, WIN11_TERMINAL_BG);

        /* Terminal text output */
        for (int line = 0; line <= term_line_count; line++) {
            draw_string(x + pad + 8, content_y + pad + 6 + line * FONT_HEIGHT, term_lines[line], 0xCCCCCC);
        }
        /* Prompt line */
        char prompt_line[160];
        strcpy(prompt_line, "$ ");
        strcat(prompt_line, command_buffer);
        draw_string(x + pad + 8, content_y + pad + 6 + term_line_count * FONT_HEIGHT, prompt_line, 0x00CC6A);

        /* Blinking cursor */
        if ((ticks_counter / 30) % 2 == 0) {
            int text_len = strlen(prompt_line);
            int cursor_x = x + pad + 8 + text_len * FONT_WIDTH;
            int cursor_y = content_y + pad + 6 + term_line_count * FONT_HEIGHT;
            draw_rect(cursor_x, cursor_y, 2, FONT_HEIGHT - 2, 0x00CC6A);
        }
    }
    else if (win->id == 2) {
        /* ---- Calculator (Windows 11 style) ---- */
        draw_rect(x + 1, content_y, w - 2, content_h - 1, 0x202020);

        /* Display area */
        draw_string(x + w - (strlen(calc_buf) + 1) * FONT_WIDTH, content_y + 10, calc_buf, 0xFFFFFF);

        /* Separator */
        draw_hline(x + 10, content_y + 36, w - 20, 0x3D3D3D);

        /* Button grid */
        char* layout[4][4] = {
            {"7", "8", "9", "/"},
            {"4", "5", "6", "*"},
            {"1", "2", "3", "-"},
            {"0", "C", "=", "+"}
        };

        int pad = 6;
        int btn_gap = 4;
        int usable_w = w - pad * 2;
        int btn_w = (usable_w - btn_gap * 3) / 4;
        int btn_h = 36;
        int grid_y = content_y + 44;

        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 4; c++) {
                int bx = x + pad + c * (btn_w + btn_gap);
                int by = grid_y + r * (btn_h + btn_gap);
                char key = layout[r][c][0];

                uint32_t btn_bg;
                uint32_t btn_fg;
                if (key == '=') {
                    btn_bg = WIN11_ACCENT;
                    btn_fg = 0xFFFFFF;
                } else if (key >= '0' && key <= '9') {
                    btn_bg = 0x3B3B3B;
                    btn_fg = 0xFFFFFF;
                } else {
                    btn_bg = 0x323232;
                    btn_fg = 0xCCCCCC;
                }

                draw_rounded_rect(bx, by, btn_w, btn_h, 4, btn_bg);
                draw_char(bx + btn_w / 2 - FONT_WIDTH / 2, by + btn_h / 2 - FONT_HEIGHT / 2 + 1, key, btn_fg);
            }
        }
    }
    else if (win->id == 3) {
        /* ---- Task Manager / System Monitor ---- */
        draw_rect(x + 1, content_y, w - 2, content_h - 1, 0x1A1A1A);

        draw_string(x + 12, content_y + 8, "CPU Performance", 0x00CC6A);

        /* Graph area */
        int gx = x + 12;
        int gy = content_y + 30;
        int gw = w - 24;
        int gh = content_h - 50;

        draw_rounded_rect(gx, gy, gw, gh, 4, 0x111111);
        draw_rounded_rect_outline(gx, gy, gw, gh, 4, 0x333333);

        /* Grid lines */
        for (int gridx = 20; gridx < gw; gridx += 20) {
            for (int gridy = 10; gridy < gh; gridy += 10) {
                draw_pixel(gx + gridx, gy + gridy, 0x1A3A1A);
            }
        }

        /* Graph line with gradient fill */
        int prev_ly = -1;
        for (int gi = 0; gi < 60; gi++) {
            int val = sysmon_values[gi];
            int lx = gx + 5 + gi * (gw - 10) / 60;
            int ly = gy + gh - 8 - (val * (gh - 16) / 100);

            if (ly < gy + 4) ly = gy + 4;
            if (ly >= gy + gh - 4) ly = gy + gh - 5;

            /* Fill area under graph with translucent green */
            for (int fill_y = ly; fill_y < gy + gh - 4; fill_y++) {
                int dist = fill_y - ly;
                uint8_t alpha = (dist < 40) ? (uint8_t)(60 - dist) : 20;
                draw_pixel_alpha(lx, fill_y, 0x00CC6A, alpha);
            }

            /* Line connecting points */
            if (prev_ly != -1) {
                int prev_lx = gx + 5 + (gi - 1) * (gw - 10) / 60;
                draw_line(prev_lx, prev_ly, lx, ly, 0x00CC6A);
            }

            draw_pixel(lx, ly, 0x00FF7F);
            prev_ly = ly;
        }

        /* Current value label */
        char val_str[8];
        int_to_ascii(sysmon_values[59], val_str);
        strcat(val_str, "%");
        draw_string(gx + gw - 50, gy + 4, val_str, 0x00CC6A);
    }
    else if (win->id == 4) {
        /* ---- Doom window ---- */
        if (doom_window_buffer) {
            for (int dy = 0; dy < 400; dy++) {
                uint32_t* dest_row = &back_buffer[(content_y + dy) * screen_width + x];
                uint32_t* src_row = &doom_window_buffer[dy * 640];
                memcpy(dest_row, src_row, 640 * 4);
            }
        } else {
            draw_rect(x + 1, content_y, w - 2, content_h - 1, 0x000000);
            draw_string(x + w/2 - 40, content_y + h/2 - 8, "Loading DOOM...", 0xFFFFFF);
        }
    }
}

/* ============================================================
 * Main GUI Draw
 * ============================================================ */
void gui_draw(void) {
    /* 1. Desktop wallpaper (Windows 11 bloom) */
    draw_desktop_gradient();

    /* 2. Desktop icons */
    draw_desktop_icons();

    /* 3. Draw windows from bottom to top (focused last) */
    for (int priority = 0; priority < MAX_WINDOWS; priority++) {
        int i = priority;
        if (active_win_id >= 0 && active_win_id < MAX_WINDOWS) {
            if (priority == MAX_WINDOWS - 1) {
                i = active_win_id;
            } else if (priority >= active_win_id) {
                i = priority + 1;
                if (i >= MAX_WINDOWS) continue;
            }
        }

        gui_window_t* win = &windows[i];
        if (win->closed || win->minimized) continue;

        draw_window_chrome(win);
        draw_window_content(win);
    }

    /* 4. Start Menu (Windows 11 floating panel) */
    if (start_menu_open) {
        int sm_w = 340;
        int sm_h = 360;
        int sm_x = ((int)screen_width - sm_w) / 2;
        int sm_y = (int)screen_height - 48 - sm_h - 12;

        /* Shadow */
        draw_rounded_rect_translucent(sm_x + 3, sm_y + 3, sm_w, sm_h, 12, 0x000000, 40);

        /* Panel background */
        draw_rounded_rect(sm_x, sm_y, sm_w, sm_h, 12, 0x2D2D2D);
        draw_rounded_rect_outline(sm_x, sm_y, sm_w, sm_h, 12, 0x3D3D3D);

        /* Search bar at top */
        draw_rounded_rect(sm_x + 16, sm_y + 16, sm_w - 32, 32, 16, 0x3D3D3D);
        draw_string(sm_x + 40, sm_y + 23, "Search apps, settings...", 0x808080);
        /* Magnifying glass icon */
        draw_circle(sm_x + 30, sm_y + 30, 5, 0x808080);
        draw_line(sm_x + 34, sm_y + 34, sm_x + 37, sm_y + 37, 0x808080);

        /* "Pinned" section header */
        draw_string(sm_x + 20, sm_y + 60, "Pinned", 0xFFFFFF);
        draw_string(sm_x + sm_w - 80, sm_y + 60, "All apps >", 0x60CDFF);

        /* Pinned apps grid (2x2) */
        struct { const char* name; uint32_t color; const char* sym; } pinned[4] = {
            {"System",   0x0078D4, "PC"},
            {"Terminal", 0x0C0C0C, ">_"},
            {"Calc",     0x3B3B3B, "+-"},
            {"Monitor",  0x1A1A1A, "/\\"},
        };

        int grid_x = sm_x + 20;
        int grid_y = sm_y + 84;
        int tile_w = (sm_w - 60) / 2;
        int tile_h = 54;
        int gap = 8;

        for (int i = 0; i < 4; i++) {
            int col = i % 2;
            int row = i / 2;
            int tx = grid_x + col * (tile_w + gap);
            int ty = grid_y + row * (tile_h + gap);

            /* Tile background with hover check */
            int mouse_in_tile = (mouse_x >= tx && mouse_x < tx + tile_w && mouse_y >= ty && mouse_y < ty + tile_h);
            draw_rounded_rect(tx, ty, tile_w, tile_h, 6, mouse_in_tile ? 0x4D4D4D : 0x3B3B3B);

            /* Icon square */
            draw_rounded_rect(tx + 8, ty + 8, 28, 28, 4, pinned[i].color);
            draw_string(tx + 13, ty + 14, pinned[i].sym, 0xFFFFFF);

            /* App name */
            draw_string(tx + 42, ty + 20, pinned[i].name, 0xFFFFFF);
        }

        /* Separator line */
        draw_hline(sm_x + 20, sm_y + sm_h - 80, sm_w - 40, 0x3D3D3D);

        /* User profile and power */
        /* User avatar circle */
        draw_circle_filled(sm_x + 36, sm_y + sm_h - 50, 14, 0x0078D4);
        draw_string(sm_x + 30, sm_y + sm_h - 56, "U", 0xFFFFFF);

        draw_string(sm_x + 56, sm_y + sm_h - 56, "User", 0xFFFFFF);

        /* Power button with hover check */
        int pw_x = sm_x + sm_w - 44;
        int pw_y = sm_y + sm_h - 64;
        int mouse_in_pw = (mouse_x >= pw_x && mouse_x < pw_x + 28 && mouse_y >= pw_y && mouse_y < pw_y + 28);
        draw_rounded_rect(pw_x, pw_y, 28, 28, 4, mouse_in_pw ? 0x4D4D4D : 0x3B3B3B);
        draw_circle(pw_x + 14, pw_y + 16, 6, 0xFFFFFF);
        draw_vline(pw_x + 14, pw_y + 8, 6, 0xFFFFFF);
    }

    /* 5. Taskbar (Windows 11 centered dark frosted glass) */
    int tb_h = 48;
    int tby = (int)screen_height - tb_h;

    /* Taskbar background with subtle transparency */
    draw_rounded_rect_translucent(4, tby + 2, screen_width - 8, tb_h - 4, 8, WIN11_TASKBAR_BG, 230);
    /* Top highlight line */
    draw_hline(12, tby + 2, screen_width - 24, 0x333333);

    /* Centered app icons */
    int icon_count = 6; /* Start + 5 apps */
    int icon_size = 32;
    int icon_gap = 6;
    int total_icons_w = icon_count * icon_size + (icon_count - 1) * icon_gap;
    int icons_start_x = ((int)screen_width - total_icons_w) / 2;

    /* Start button (Windows 11 4-pane logo) */
    int start_x = icons_start_x;
    int start_y = tby + (tb_h - icon_size) / 2;
    int mouse_in_start = (mouse_x >= start_x && mouse_x < start_x + icon_size && mouse_y >= start_y && mouse_y < start_y + icon_size);
    draw_rounded_rect(start_x, start_y, icon_size, icon_size, 4, mouse_in_start ? 0x444444 : 0x333333);
    draw_win11_logo(start_x + icon_size / 2, start_y + icon_size / 2, WIN11_ACCENT_LIGHT);

    /* App taskbar icons */
    uint32_t app_icon_colors[5] = {0x0078D4, 0x0C0C0C, 0x3B3B3B, 0x1A1A1A, 0xC21807};
    char* app_icon_syms[5] = {"PC", ">_", "+-", "/\\", "DM"};

    for (int i = 0; i < MAX_WINDOWS; i++) {
        gui_window_t* w = &windows[i];

        int ix = icons_start_x + (i + 1) * (icon_size + icon_gap);
        int iy = tby + (tb_h - icon_size) / 2;

        /* Icon background with hover */
        int mouse_in_app = (mouse_x >= ix && mouse_x < ix + icon_size && mouse_y >= iy && mouse_y < iy + icon_size);
        uint32_t bg;
        if (active_win_id == w->id) {
            bg = mouse_in_app ? 0x4D4D4D : 0x3D3D3D;
        } else {
            bg = mouse_in_app ? 0x3D3D3D : 0x2D2D2D;
        }
        draw_rounded_rect(ix, iy, icon_size, icon_size, 4, bg);

        /* Small colored icon */
        draw_rounded_rect(ix + 6, iy + 6, 20, 20, 3, app_icon_colors[i]);
        draw_string(ix + 9, iy + 9, app_icon_syms[i], 0xFFFFFF);

        /* Active indicator pill */
        if (active_win_id == w->id) {
            int pill_w = 16;
            int pill_x = ix + (icon_size - pill_w) / 2;
            int pill_y = iy + icon_size + 1;
            draw_rounded_rect(pill_x, pill_y, pill_w, 3, 1, WIN11_ACCENT);
        } else if (!w->minimized) {
            /* Small dot for open-but-unfocused */
            draw_rect(ix + icon_size / 2 - 2, iy + icon_size + 1, 4, 2, 0x606060);
        }
    }

    /* System tray (right side) */
    /* Update clock */
    ticks_counter++;
    if (ticks_counter % 100 == 0) {
        seconds++;
        if (seconds >= 60) {
            seconds = 0;
            minutes++;
            if (minutes >= 60) {
                minutes = 0;
                hours++;
                if (hours > 12) hours = 1;
            }
        }
        time_str[0] = '0' + (hours / 10);
        time_str[1] = '0' + (hours % 10);
        time_str[2] = ':';
        time_str[3] = '0' + (minutes / 10);
        time_str[4] = '0' + (minutes % 10);
        time_str[5] = ' ';
        time_str[6] = 'P';
        time_str[7] = 'M';
        time_str[8] = '\0';
    }

    int tray_x = (int)screen_width - 140;

    /* Wi-Fi icon */
    for (int arc = 0; arc < 3; arc++) {
        int r = 3 + arc * 3;
        draw_circle(tray_x, tby + 30, r, 0xCCCCCC);
    }
    draw_rect(tray_x - 1, tby + 31, 3, 8, WIN11_TASKBAR_BG); /* clear bottom half */

    /* Volume icon */
    draw_rect(tray_x + 24, tby + 22, 4, 8, 0xCCCCCC);
    draw_rect(tray_x + 28, tby + 20, 2, 12, 0xCCCCCC);
    draw_circle(tray_x + 34, tby + 26, 5, 0xCCCCCC);
    draw_rect(tray_x + 29, tby + 20, 6, 12, WIN11_TASKBAR_BG); /* mask inner circle */

    /* Battery indicator */
    draw_rect_outline(tray_x + 48, tby + 20, 16, 10, 0xCCCCCC);
    draw_rect(tray_x + 64, tby + 23, 2, 4, 0xCCCCCC);
    draw_rect(tray_x + 50, tby + 22, 12, 6, WIN11_GREEN); /* Fill */

    /* Clock text */
    draw_string(tray_x + 74, tby + 12, time_str, 0xCCCCCC);
    draw_string(tray_x + 74, tby + 28, date_str, 0x999999);

    /* Update system monitor data */
    val_timer++;
    if (val_timer >= 20) {
        val_timer = 0;
        for (int i = 0; i < 59; i++) {
            sysmon_values[i] = sysmon_values[i + 1];
        }
        int load = 10 + (ticks_counter % 7) * 10 + (mouse_x % 5) * 2;
        if (load > 100) load = 100;
        if (load < 0) load = 0;
        sysmon_values[59] = load;
    }

    /* 6. Mouse cursor (Windows 11 style) */
    int mx = mouse_x;
    int my = mouse_y;
    /* Shadow */
    for (int r = 0; r < 22; r++) {
        for (int c = 0; c < 16; c++) {
            char p = cursor_bitmap[r][c];
            if (p == 'X' || p == '.') {
                draw_pixel_alpha(mx + c + 1, my + r + 1, 0x000000, 40);
            }
        }
    }
    /* Main cursor */
    for (int r = 0; r < 22; r++) {
        for (int c = 0; c < 16; c++) {
            char p = cursor_bitmap[r][c];
            if (p == 'X') {
                draw_pixel(mx + c, my + r, 0x000000);
            } else if (p == '.') {
                draw_pixel(mx + c, my + r, 0xFFFFFF);
            }
        }
    }

    /* 7. Blit */
    blit_buffer();
}

/* ============================================================
 * Mouse click interaction logic
 * ============================================================ */
void gui_handle_mouse(int mx, int my, int click, int r_click) {
    (void)r_click;
    static int was_clicked = 0;

    /* Dragging active window */
    if (active_win_id >= 0 && active_win_id < MAX_WINDOWS) {
        gui_window_t* win = &windows[active_win_id];
        if (win->dragging) {
            if (click) {
                win->x = mx - win->drag_off_x;
                win->y = my - win->drag_off_y;
                if (win->y < 0) win->y = 0;
                if (win->y > (int)screen_height - 80) win->y = (int)screen_height - 80;
                if (win->x < -win->w + 40) win->x = -win->w + 40;
                if (win->x > (int)screen_width - 40) win->x = (int)screen_width - 40;
                return;
            } else {
                win->dragging = 0;
            }
        }
    }

    if (click && !was_clicked) {
        was_clicked = 1;

        int tb_h = 48;
        int tby = (int)screen_height - tb_h;

        /* Taskbar centered icon clicks */
        int icon_count = 5;
        int icon_size = 32;
        int icon_gap = 6;
        int total_icons_w = icon_count * icon_size + (icon_count - 1) * icon_gap;
        int icons_start_x = ((int)screen_width - total_icons_w) / 2;

        /* Start button click */
        int start_x = icons_start_x;
        int start_y = tby + (tb_h - icon_size) / 2;
        if (mx >= start_x && mx < start_x + icon_size && my >= start_y && my < start_y + icon_size) {
            start_menu_open = !start_menu_open;
            return;
        }

        /* App icon clicks */
        for (int i = 0; i < MAX_WINDOWS; i++) {
            gui_window_t* w = &windows[i];

            int ix = icons_start_x + (i + 1) * (icon_size + icon_gap);
            int iy = tby + (tb_h - icon_size) / 2;

            if (mx >= ix && mx < ix + icon_size && my >= iy && my < iy + icon_size) {
                if (w->closed || w->minimized) {
                    w->closed = 0;
                    w->minimized = 0;
                    active_win_id = w->id;
                } else if (active_win_id == w->id) {
                    w->minimized = 1;
                    active_win_id = -1;
                } else {
                    active_win_id = w->id;
                }
                start_menu_open = 0;
                return;
            }
        }

        /* Start Menu item clicks */
        if (start_menu_open) {
            int sm_w = 340;
            int sm_h = 360;
            int sm_x = ((int)screen_width - sm_w) / 2;
            int sm_y = (int)screen_height - 48 - sm_h - 12;

            /* Power button click */
            int pw_x = sm_x + sm_w - 44;
            int pw_y = sm_y + sm_h - 64;
            if (mx >= pw_x && mx < pw_x + 28 && my >= pw_y && my < pw_y + 28) {
                gui_write_string("Shutting down...\n");
                outw(0x604, 0x2000);
                __asm__ volatile("cli; hlt");
            }

            /* Pinned apps grid clicks */
            int grid_x = sm_x + 20;
            int grid_y = sm_y + 84;
            int tile_w = (sm_w - 60) / 2;
            int tile_h = 54;
            int gap = 8;

            for (int i = 0; i < 4; i++) {
                int col = i % 2;
                int row = i / 2;
                int tx = grid_x + col * (tile_w + gap);
                int ty = grid_y + row * (tile_h + gap);

                if (mx >= tx && mx < tx + tile_w && my >= ty && my < ty + tile_h) {
                    windows[i].closed = 0;
                    windows[i].minimized = 0;
                    active_win_id = i;
                    start_menu_open = 0;
                    return;
                }
            }

            /* Click outside start menu closes it */
            if (mx < sm_x || mx > sm_x + sm_w || my < sm_y || my > sm_y + sm_h) {
                start_menu_open = 0;
            }
        }

        /* Desktop icon clicks */
        for (int i = 0; i < 5; i++) {
            int ix = 30;
            int iy = 30 + i * 90;
            if (mx >= ix && mx < ix + 48 && my >= iy && my < iy + 70) {
                windows[i].closed = 0;
                windows[i].minimized = 0;
                active_win_id = i;
                start_menu_open = 0;

                if (i == 4 && !doom_running) {
                    extern void doomgeneric_Create(int argc, char** argv);
                    char* doom_argv[] = {"doomgeneric", "-iwad", "/boot/doom1.wad"};
                    print_serial("[Doom] Launching game loop via setjmp...\n");
                    if (setjmp(doom_exit_jmp) == 0) {
                        doom_running = 1;
                        doomgeneric_Create(3, doom_argv);
                    } else {
                        print_serial("[Doom] Gracefully returned to desktop.\n");
                    }
                }
                return;
            }
        }

        /* Window clicks (from top priority down) */
        int win_order[MAX_WINDOWS];
        int idx = 0;
        if (active_win_id >= 0) {
            win_order[idx++] = active_win_id;
        }
        for (int i = MAX_WINDOWS - 1; i >= 0; i--) {
            if (i != active_win_id) {
                win_order[idx++] = i;
            }
        }

        for (int order = 0; order < MAX_WINDOWS; order++) {
            int i = win_order[order];
            gui_window_t* win = &windows[i];
            if (win->closed || win->minimized) continue;

            if (mx >= win->x && mx < win->x + win->w && my >= win->y && my < win->y + win->h) {
                active_win_id = win->id;

                int titlebar_h = 32;
                int btn_w = 46;

                /* Close button */
                int close_x = win->x + win->w - btn_w;
                if (mx >= close_x && mx < close_x + btn_w && my >= win->y && my < win->y + 30) {
                    win->closed = 1;
                    if (active_win_id == win->id) active_win_id = -1;
                    return;
                }

                /* Minimize button */
                int min_x = close_x - btn_w * 2;
                if (mx >= min_x && mx < min_x + btn_w && my >= win->y && my < win->y + 30) {
                    win->minimized = 1;
                    if (active_win_id == win->id) active_win_id = -1;
                    return;
                }

                /* Titlebar drag */
                if (my >= win->y && my < win->y + titlebar_h) {
                    win->dragging = 1;
                    win->drag_off_x = mx - win->x;
                    win->drag_off_y = my - win->y;
                    return;
                }

                /* Calculator button clicks */
                if (win->id == 2) {
                    int content_y = win->y + 32;
                    int pad = 6;
                    int btn_gap = 4;
                    int usable_w = win->w - pad * 2;
                    int calc_btn_w = (usable_w - btn_gap * 3) / 4;
                    int calc_btn_h = 36;
                    int grid_y = content_y + 44;

                    char* layout[4][4] = {
                        {"7", "8", "9", "/"},
                        {"4", "5", "6", "*"},
                        {"1", "2", "3", "-"},
                        {"0", "C", "=", "+"}
                    };

                    for (int r = 0; r < 4; r++) {
                        for (int c = 0; c < 4; c++) {
                            int bx = win->x + pad + c * (calc_btn_w + btn_gap);
                            int by = grid_y + r * (calc_btn_h + btn_gap);
                            if (mx >= bx && mx < bx + calc_btn_w && my >= by && my < by + calc_btn_h) {
                                char key = layout[r][c][0];
                                if (key >= '0' && key <= '9') {
                                    if (calc_new_number || strcmp(calc_buf, "0") == 0) {
                                        calc_buf[0] = key;
                                        calc_buf[1] = '\0';
                                        calc_new_number = 0;
                                    } else {
                                        int len = strlen(calc_buf);
                                        if (len < 15) {
                                            calc_buf[len] = key;
                                            calc_buf[len + 1] = '\0';
                                        }
                                    }
                                } else if (key == 'C') {
                                    strcpy(calc_buf, "0");
                                    op1 = 0; op2 = 0; calc_op = 0;
                                    calc_new_number = 1;
                                } else if (key == '=') {
                                    int temp_val = 0;
                                    for (int s = 0; calc_buf[s] != '\0'; s++)
                                        temp_val = temp_val * 10 + (calc_buf[s] - '0');
                                    op2 = temp_val;
                                    int res = 0;
                                    if (calc_op == '+') res = op1 + op2;
                                    else if (calc_op == '-') res = op1 - op2;
                                    else if (calc_op == '*') res = op1 * op2;
                                    else if (calc_op == '/') res = (op2 != 0) ? op1 / op2 : 0;
                                    else res = op2;
                                    int_to_ascii(res, calc_buf);
                                    calc_new_number = 1;
                                    calc_op = 0;
                                } else {
                                    int temp_val = 0;
                                    for (int s = 0; calc_buf[s] != '\0'; s++)
                                        temp_val = temp_val * 10 + (calc_buf[s] - '0');
                                    op1 = temp_val;
                                    calc_op = key;
                                    calc_new_number = 1;
                                }
                                return;
                            }
                        }
                    }
                }
                return;
            }
        }
    }

    if (!click) {
        was_clicked = 0;
    }
}

/* ============================================================
 * Keyboard character routing to focused Terminal window
 * ============================================================ */
void gui_handle_keyboard(char c) {
    if (active_win_id != 1) return;

    if (c == '\n') {
        gui_write_char('\n');
        gui_execute_command(command_buffer);
        memset(command_buffer, 0, sizeof(command_buffer));
        cmd_buf_idx = 0;
    } else if (c == '\b') {
        if (cmd_buf_idx > 0) {
            cmd_buf_idx--;
            command_buffer[cmd_buf_idx] = '\0';
        }
    } else {
        if (cmd_buf_idx < 120) {
            command_buffer[cmd_buf_idx++] = c;
            command_buffer[cmd_buf_idx] = '\0';
        }
    }
}
