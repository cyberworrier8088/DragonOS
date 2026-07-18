#include "gui.h"
#include "../drivers/graphics.h"
#include "../drivers/mouse.h"
#include "../drivers/serial.h"
#include "../cpu/ports.h"
#include "../libc/string.h"
#include "../libc/font.h"
#include "shell.h"

gui_window_t windows[MAX_WINDOWS];
int active_win_id = -1;

/* Start Menu State */
int start_menu_open = 0;

/* Clock / Time counter */
static uint32_t ticks_counter = 0;
static int hours = 12;
static int minutes = 0;
static int seconds = 0;
static char time_str[12] = "12:00 PM";

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

/* High-Fidelity 12x20 Windows 7 Mouse Pointer Bitmap */
static const char mouse_cursor_bitmap[20][12] = {
    "X           ",
    "XX          ",
    "X.X         ",
    "X..X        ",
    "X...X       ",
    "X....X      ",
    "X.....X     ",
    "X......X    ",
    "X.......X   ",
    "X........X  ",
    "X.........X ",
    "X....XXXXXX ",
    "X..X.X      ",
    "X.X  X.X    ",
    "XX    X.X   ",
    "X      X.X  ",
    "        XX  ",
    "            ",
    "            ",
    "            "
};

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
        gui_write_string("  help        - Show this help menu\n");
        gui_write_string("  about       - Display OS information\n");
        gui_write_string("  clear       - Clear the screen\n");
        gui_write_string("  ticks       - Show system timer ticks elapsed\n");
        gui_write_string("  ping        - Test command response\n");
        gui_write_string("  echo <msg>  - Echo input text back\n");
        gui_write_string("  reboot      - Reboot the computer\n");
        gui_write_string("  halt        - Halt the CPU safely\n");
    } else if (strcmp(cmd, "about") == 0) {
        gui_write_string("DragonOS x86_64 Kernel\n");
        gui_write_string("Build: July 2026\n");
        gui_write_string("Bootloader: Limine VBE Graphics\n");
        gui_write_string("Design: Monolithic 64-bit Aero GUI\n");
    } else if (strcmp(cmd, "clear") == 0) {
        memset(term_lines, 0, sizeof(term_lines));
        term_line_count = 0;
    } else if (strcmp(cmd, "ticks") == 0) {
        char tick_str[32];
        int_to_ascii(ticks_counter, tick_str);
        gui_write_string("Ticks elapsed: ");
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
        gui_write_string("Unknown command. Type 'help' for commands.\n");
    }
}

void init_gui(void) {
    /* Initialize windows */
    // 0: Computer
    windows[0].x = 50;
    windows[0].y = 50;
    windows[0].w = 260;
    windows[0].h = 160;
    strcpy(windows[0].title, "Computer Information");
    windows[0].active = 0;
    windows[0].closed = 0;
    windows[0].minimized = 0;
    windows[0].dragging = 0;
    windows[0].id = 0;

    // 1: Terminal
    windows[1].x = 350;
    windows[1].y = 80;
    windows[1].w = 400;
    windows[1].h = 320;
    strcpy(windows[1].title, "Command Terminal");
    windows[1].active = 1;
    windows[1].closed = 0;
    windows[1].minimized = 0;
    windows[1].dragging = 0;
    windows[1].id = 1;
    active_win_id = 1;

    // 2: Calculator
    windows[2].x = 100;
    windows[2].y = 250;
    windows[2].w = 180;
    windows[2].h = 220;
    strcpy(windows[2].title, "Calculator");
    windows[2].active = 0;
    windows[2].closed = 0;
    windows[2].minimized = 0;
    windows[2].dragging = 0;
    windows[2].id = 2;

    // 3: System Monitor
    windows[3].x = 400;
    windows[3].y = 280;
    windows[3].w = 320;
    windows[3].h = 180;
    strcpy(windows[3].title, "System Monitor");
    windows[3].active = 0;
    windows[3].closed = 0;
    windows[3].minimized = 0;
    windows[3].dragging = 0;
    windows[3].id = 3;

    /* Initialize terminal buffer */
    memset(term_lines, 0, sizeof(term_lines));
    strcpy(term_lines[0], "DragonOS Interactive Terminal");
    strcpy(term_lines[1], "Type 'help' to see list of commands.");
    strcpy(term_lines[2], "");
    term_line_count = 3;

    /* Initialize system monitor points */
    for (int i = 0; i < 60; i++) {
        sysmon_values[i] = 100; // bottom of grid
    }

    strcpy(calc_buf, "0");
}

/* Helper to render Start Orb Flag Logo */
static void draw_start_logo(int cx, int cy) {
    draw_rect(cx - 5, cy - 5, 4, 4, 0xFF4B4B); // Red
    draw_rect(cx + 1, cy - 5, 4, 4, 0x4BFF4B); // Green
    draw_rect(cx - 5, cy + 1, 4, 4, 0x4B4BFF); // Blue
    draw_rect(cx + 1, cy + 1, 4, 4, 0xFFFF4B); // Yellow
}

/* Redraw GUI state on the back buffer */
void gui_draw(void) {
    // 1. Draw Windows 7 gradient background with Aero light streams
    draw_desktop_gradient();

    // 2. Draw Desktop Icons
    // Icon 0: Computer
    draw_rect(20, 20, 32, 24, 0xCCCCCC); // Screen
    draw_rect(32, 44, 8, 4, 0x888888);   // Stand
    draw_rect(24, 48, 24, 2, 0x666666);  // Base
    draw_string(14, 54, "Computer", 0xFFFFFF);

    // Icon 1: Terminal
    draw_rect(20, 100, 32, 28, 0x111111); // Box
    draw_rect_outline(20, 100, 32, 28, 0x888888);
    draw_string(24, 106, ">_", 0x00FF00);
    draw_string(14, 132, "Terminal", 0xFFFFFF);

    // Icon 2: Calculator
    draw_rect(20, 180, 32, 28, 0x4A6FA5);
    draw_rect_outline(20, 180, 32, 28, 0xFFFFFF);
    draw_string(24, 186, "+-", 0xFFFFFF);
    draw_string(24, 196, "x=", 0xFFFFFF);
    draw_string(14, 212, "Calc", 0xFFFFFF);

    // Icon 3: System Monitor
    draw_rect(20, 260, 32, 28, 0x1E1E1E);
    draw_rect_outline(20, 260, 32, 28, 0x00AA00);
    draw_rect(24, 274, 24, 2, 0x00FF00); // simulated graph line
    draw_string(14, 292, "SysMon", 0xFFFFFF);

    // 3. Draw Windows from bottom to top (focused window drawn last)
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

        // Draw Aero window translucent shadow border
        draw_rect_translucent(win->x - 4, win->y - 4, win->w + 8, win->h + 8, 0x228BE6, 40);

        // Title bar with rounded top corners
        uint32_t title_color = (active_win_id == win->id) ? 0x1C7ED6 : 0x74C0FC;
        draw_rect_translucent(win->x + 2, win->y, win->w - 4, 1, title_color, 200);
        draw_rect_translucent(win->x + 1, win->y + 1, win->w - 2, 1, title_color, 200);
        draw_rect_translucent(win->x, win->y + 2, win->w, 22, title_color, 200);
        draw_rect_outline(win->x, win->y, win->w, win->h, 0x1971C2);

        // Specular gloss reflection line
        draw_rect_translucent(win->x + 1, win->y + 2, win->w - 2, 8, 0xFFFFFF, 50);

        // Window Title text
        draw_string(win->x + 8, win->y + 5, win->title, 0xFFFFFF);

        // Buttons: Minimize, Maximize, Close
        int bx_close = win->x + win->w - 18;
        int bx_max   = win->x + win->w - 34;
        int bx_min   = win->x + win->w - 50;

        // Minimize
        draw_rect(bx_min, win->y + 4, 12, 12, 0x4DABF7);
        draw_rect_outline(bx_min, win->y + 4, 12, 12, 0x1971C2);
        draw_rect(bx_min + 3, win->y + 10, 6, 2, 0xFFFFFF);

        // Maximize
        draw_rect(bx_max, win->y + 4, 12, 12, 0x4DABF7);
        draw_rect_outline(bx_max, win->y + 4, 12, 12, 0x1971C2);
        draw_rect_outline(bx_max + 3, win->y + 7, 6, 6, 0xFFFFFF);

        // Close (Red Box)
        draw_rect(bx_close, win->y + 4, 12, 12, 0xFA5252);
        draw_rect_outline(bx_close, win->y + 4, 12, 12, 0xC92A2A);
        draw_char(bx_close + 3, win->y + 2, 'x', 0xFFFFFF);

        // Window content background
        draw_rect(win->x + 1, win->y + 24, win->w - 2, win->h - 25, 0xFFFFFF);

        // Draw specific window contents
        if (win->id == 0) {
            // Computer Info
            draw_string(win->x + 10, win->y + 35, "DragonOS x86_64 Kernel", 0x1971C2);
            draw_string(win->x + 10, win->y + 55, "=====================", 0xADB5BD);
            draw_string(win->x + 10, win->y + 75, "CPU: 64-Bit Intel Core Emulator", 0x212529);
            draw_string(win->x + 10, win->y + 91, "RAM: 4096 MB Physical", 0x212529);
            draw_string(win->x + 10, win->y + 107, "Mode: 64-Bit Protected Long Mode", 0x212529);
            draw_string(win->x + 10, win->y + 123, "Boot: Limine Multiboot v1.0", 0x212529);
        }
        else if (win->id == 1) {
            // Command Terminal
            draw_rect(win->x + 4, win->y + 28, win->w - 8, win->h - 32, 0x111111);
            for (int line = 0; line <= term_line_count; line++) {
                draw_string(win->x + 8, win->y + 32 + line * 14, term_lines[line], 0x00FF00);
            }
            // Draw prompt and input line
            char prompt_line[160];
            strcpy(prompt_line, "dragonos> ");
            strcat(prompt_line, command_buffer);
            draw_string(win->x + 8, win->y + 32 + term_line_count * 14, prompt_line, 0xFFFFFF);

            // Draw flashing console underline cursor
            if ((ticks_counter / 25) % 2 == 0) {
                int text_len = strlen(prompt_line);
                int cursor_x = win->x + 8 + text_len * 8;
                int cursor_y = win->y + 32 + term_line_count * 14;
                draw_rect(cursor_x, cursor_y + 12, 8, 2, 0xFFFFFF);
            }
        }
        else if (win->id == 2) {
            // Calculator (styled to match Windows 7 calculator)
            draw_rect(win->x + 1, win->y + 24, win->w - 2, win->h - 25, 0xF1F3F5); // Silver background
            
            // Display box
            draw_rect(win->x + 10, win->y + 30, win->w - 20, 22, 0xFFFFFF);
            draw_rect_outline(win->x + 10, win->y + 30, win->w - 20, 22, 0xADB5BD);
            draw_string(win->x + 15, win->y + 34, calc_buf, 0x212529);

            char* layout[4][4] = {
                {"7", "8", "9", "/"},
                {"4", "5", "6", "*"},
                {"1", "2", "3", "-"},
                {"0", "C", "=", "+"}
            };

            int start_bx = win->x + 12;
            int start_by = win->y + 60;
            for (int r = 0; r < 4; r++) {
                for (int c = 0; c < 4; c++) {
                    int bx = start_bx + c * 40;
                    int by = start_by + r * 35;
                    draw_rect(bx, by, 34, 28, 0xFFFFFF);
                    draw_rect_outline(bx, by, 34, 28, 0xCED4DA);
                    draw_char(bx + 13, by + 6, layout[r][c][0], 0x1C7ED6); // Aero blue text
                }
            }
        }
        else if (win->id == 3) {
            // System Monitor
            draw_string(win->x + 10, win->y + 30, "CPU Usage History", 0x12B886);
            int grid_x = win->x + 10;
            int grid_y = win->y + 50;
            int grid_w = win->w - 20;
            int grid_h = win->h - 60;
            draw_rect(grid_x, grid_y, grid_w, grid_h, 0x111111);
            draw_rect_outline(grid_x, grid_y, grid_w, grid_h, 0x12B886);

            for (int gx = 20; gx < grid_w; gx += 20) {
                for (int gy = 10; gy < grid_h; gy += 10) {
                    draw_pixel(grid_x + gx, grid_y + gy, 0x002E00);
                }
            }

            int last_gy = -1;
            for (int gi = 0; gi < 60; gi++) {
                int val = sysmon_values[gi];
                int lx = grid_x + 5 + gi * 5;
                int ly = grid_y + grid_h - 10 - (val * (grid_h - 20) / 100);

                if (ly < grid_y) ly = grid_y;
                if (ly >= grid_y + grid_h) ly = grid_y + grid_h - 1;

                if (last_gy != -1) {
                    int prev_lx = lx - 5;
                    if (ly == last_gy) {
                        for (int x_fill = prev_lx; x_fill <= lx; x_fill++) {
                            draw_pixel(x_fill, ly, 0x38D9A9);
                        }
                    } else {
                        int min_y = (ly < last_gy) ? ly : last_gy;
                        int max_y = (ly > last_gy) ? ly : last_gy;
                        for (int y_fill = min_y; y_fill <= max_y; y_fill++) {
                            draw_pixel(lx, y_fill, 0x38D9A9);
                        }
                    }
                }
                draw_pixel(lx, ly, 0x12B886);
                last_gy = ly;
            }
        }
    }

    // 4. Draw Start Menu (pop-up over desktop/windows if open)
    if (start_menu_open) {
        draw_rect_translucent(6, 196, 288, 368, 0x1971C2, 60);

        // Glass blue frame
        draw_rect_translucent(10, 200, 280, 360, 0x1A365D, 220);
        draw_rect_outline(10, 200, 280, 360, 0x1971C2);

        // Left Pane (White Apps Panel)
        draw_rect(16, 206, 150, 348, 0xFFFFFF);

        // Apps list
        draw_string(24, 220, "Computer Info", 0x212529);
        draw_string(24, 250, "Command Term", 0x212529);
        draw_string(24, 280, "Calculator", 0x212529);
        draw_string(24, 310, "System Monitor", 0x212529);

        // Search Programs box at the bottom left
        draw_rect(20, 522, 142, 22, 0xFAFAFA);
        draw_rect_outline(20, 522, 142, 22, 0xCED4DA);
        draw_string(26, 526, "Search programs", 0x868E96);

        // Right Pane (shortcuts list)
        draw_string(176, 220, "Documents", 0xFFFFFF);
        draw_string(176, 250, "Pictures", 0xFFFFFF);
        draw_string(176, 280, "Games", 0xFFFFFF);
        draw_string(176, 310, "Control Panel", 0xFFFFFF);

        // User profile smiley avatar box
        int pic_x = 230;
        int pic_y = 210;
        int pic_w = 40;
        int pic_h = 40;
        draw_rect(pic_x, pic_y, pic_w, pic_h, 0x74C0FC);
        draw_rect_outline(pic_x, pic_y, pic_w, pic_h, 0xFFFFFF);
        draw_rect_outline(pic_x - 1, pic_y - 1, pic_w + 2, pic_h + 2, 0xADB5BD);
        // Smiley Face profile picture
        draw_circle(pic_x + 20, pic_y + 20, 10, 0xFFD43B);
        draw_pixel(pic_x + 17, pic_y + 17, 0x000000);
        draw_pixel(pic_x + 23, pic_y + 17, 0x000000);
        draw_pixel(pic_x + 16, pic_y + 23, 0x000000);
        draw_pixel(pic_x + 17, pic_y + 24, 0x000000);
        draw_pixel(pic_x + 18, pic_y + 25, 0x000000);
        draw_pixel(pic_x + 19, pic_y + 25, 0x000000);
        draw_pixel(pic_x + 20, pic_y + 25, 0x000000);
        draw_pixel(pic_x + 21, pic_y + 25, 0x000000);
        draw_pixel(pic_x + 22, pic_y + 25, 0x000000);
        draw_pixel(pic_x + 23, pic_y + 24, 0x000000);
        draw_pixel(pic_x + 24, pic_y + 23, 0x000000);

        // Shutdown Button at the bottom right
        draw_rect(180, 520, 100, 28, 0xFA5252);
        draw_rect_outline(180, 520, 100, 28, 0xFFFFFF);
        draw_string(195, 526, "Shutdown", 0xFFFFFF);
    }

    // 5. Draw Taskbar (translucent glassmorphism bottom row)
    draw_rect_translucent(0, 560, 800, 40, 0x1A365D, 180);
    draw_rect(0, 560, 800, 1, 0x4DABF7); // Top light highlight line

    // Start Orb Button (circular blue glass orb)
    draw_circle(24, 580, 16, 0x1971C2);
    draw_circle(24, 580, 14, 0x228BE6);
    draw_start_logo(24, 580);

    // Draw taskbar glassy buttons for open applications
    char* app_names[4] = {"Comp", "Term", "Calc", "Mon"};
    uint32_t app_colors[4] = {0xCCCCCC, 0x111111, 0x4A6FA5, 0x1E1E1E};
    for (int i = 0; i < MAX_WINDOWS; i++) {
        gui_window_t* w = &windows[i];
        if (w->closed) continue;

        int bx = 55 + i * 70;
        int by = 565;
        int bw = 64;
        int bh = 30;

        if (active_win_id == w->id) {
            draw_rect_translucent(bx, by, bw, bh, 0x4DABF7, 180);
            draw_rect_outline(bx, by, bw, bh, 0xFFFFFF);
            draw_rect(bx, by + bh - 3, bw, 3, 0x228BE6); // active bottom bar
        } else {
            draw_rect_translucent(bx, by, bw, bh, 0x495057, 100);
            draw_rect_outline(bx, by, bw, bh, 0xCED4DA);
        }

        // Draw small color block representing app icon
        draw_rect(bx + 6, by + 8, 12, 12, app_colors[i]);
        draw_rect_outline(bx + 6, by + 8, 12, 12, 0xFFFFFF);

        draw_string(bx + 22, by + 10, app_names[i], 0xFFFFFF);
    }

    // System Tray clock and indicators
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
        time_str[5] = ':';
        time_str[6] = '0' + (seconds / 10);
        time_str[7] = '0' + (seconds % 10);
        time_str[8] = ' ';
        time_str[9] = 'P';
        time_str[10] = 'M';
        time_str[11] = '\0';
    }

    draw_string(725, 572, time_str, 0xFFFFFF);

    // Network Signal strength indicator bars
    int net_x = 695;
    int net_y = 588;
    for (int bar = 0; bar < 5; bar++) {
        int bar_h = 3 + bar * 3;
        draw_rect(net_x + bar * 4, net_y - bar_h, 3, bar_h, 0x00FF00);
    }

    // Volume speaker tray icon
    int vol_x = 675;
    int vol_y = 576;
    draw_rect(vol_x, vol_y + 3, 3, 4, 0xFFFFFF);
    draw_pixel(vol_x + 3, vol_y + 3, 0xFFFFFF);
    draw_pixel(vol_x + 4, vol_y + 2, 0xFFFFFF);
    draw_pixel(vol_x + 5, vol_y + 1, 0xFFFFFF);
    draw_pixel(vol_x + 3, vol_y + 4, 0xFFFFFF);
    draw_pixel(vol_x + 4, vol_y + 4, 0xFFFFFF);
    draw_pixel(vol_x + 5, vol_y + 4, 0xFFFFFF);
    draw_pixel(vol_x + 3, vol_y + 5, 0xFFFFFF);
    draw_pixel(vol_x + 4, vol_y + 5, 0xFFFFFF);
    draw_pixel(vol_x + 5, vol_y + 5, 0xFFFFFF);
    draw_pixel(vol_x + 3, vol_y + 6, 0xFFFFFF);
    draw_pixel(vol_x + 4, vol_y + 7, 0xFFFFFF);
    draw_pixel(vol_x + 5, vol_y + 8, 0xFFFFFF);

    // Show Desktop glassy sliver on the very right
    draw_rect_translucent(790, 560, 10, 40, 0xADB5BD, 100);
    draw_rect(790, 560, 1, 40, 0xCED4DA);

    // Update System Monitor graphs
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

    // 6. Draw High-Fidelity White Mouse Pointer with drop shadow outline
    int mx = mouse_x;
    int my = mouse_y;
    for (int r = 0; r < 20; r++) {
        for (int c = 0; c < 12; c++) {
            char pixel = mouse_cursor_bitmap[r][c];
            if (pixel == 'X') {
                draw_pixel(mx + c, my + r, 0x000000); // Black outline
            } else if (pixel == '.') {
                draw_pixel(mx + c, my + r, 0xFFFFFF); // White fill
            }
        }
    }

    // 7. Blit buffer to screen
    blit_buffer();
}

/* Mouse click interaction logic */
void gui_handle_mouse(int mx, int my, int click, int r_click) {
    (void)r_click;
    static int was_clicked = 0;

    // Dragging active window
    if (active_win_id >= 0 && active_win_id < MAX_WINDOWS) {
        gui_window_t* win = &windows[active_win_id];
        if (win->dragging) {
            if (click) {
                win->x = mx - win->drag_off_x;
                win->y = my - win->drag_off_y;
                if (win->y < 0) win->y = 0;
                if (win->y > 520) win->y = 520;
                if (win->x < -win->w + 40) win->x = -win->w + 40;
                if (win->x > 760) win->x = 760;
                return;
            } else {
                win->dragging = 0;
            }
        }
    }

    if (click && !was_clicked) {
        was_clicked = 1;

        // Check Show Desktop sliver click
        if (mx >= 790 && mx < 800 && my >= 560 && my < 600) {
            for (int w = 0; w < MAX_WINDOWS; w++) {
                windows[w].minimized = 1;
            }
            active_win_id = -1;
            start_menu_open = 0;
            return;
        }

        // Check taskbar buttons click
        for (int i = 0; i < MAX_WINDOWS; i++) {
            gui_window_t* w = &windows[i];
            if (w->closed) continue;

            int bx = 55 + i * 70;
            int by = 565;
            int bw = 64;
            int bh = 30;
            if (mx >= bx && mx < bx + bw && my >= by && my < by + bh) {
                if (w->minimized) {
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

        // Check Start Menu items if open
        if (start_menu_open) {
            // Clicked Shutdown button
            if (mx >= 180 && mx < 280 && my >= 520 && my < 548) {
                gui_write_string("Shutting down...\n");
                outw(0x604, 0x2000); // Standard QEMU shutdown port
                __asm__ volatile("cli; hlt");
            }
            // Clicked App shortcuts inside start menu
            if (mx >= 16 && mx < 166) {
                if (my >= 210 && my < 240) {
                    windows[0].closed = 0;
                    windows[0].minimized = 0;
                    active_win_id = 0;
                    start_menu_open = 0;
                    return;
                }
                if (my >= 240 && my < 270) {
                    windows[1].closed = 0;
                    windows[1].minimized = 0;
                    active_win_id = 1;
                    start_menu_open = 0;
                    return;
                }
                if (my >= 270 && my < 300) {
                    windows[2].closed = 0;
                    windows[2].minimized = 0;
                    active_win_id = 2;
                    start_menu_open = 0;
                    return;
                }
                if (my >= 300 && my < 330) {
                    windows[3].closed = 0;
                    windows[3].minimized = 0;
                    active_win_id = 3;
                    start_menu_open = 0;
                    return;
                }
            }

            // Click outside start menu closes it
            if (mx > 290 || my < 200 || my > 560) {
                start_menu_open = 0;
            }
        }

        // Check Taskbar Start Orb
        if (mx >= 8 && mx < 40 && my >= 564 && my < 596) {
            start_menu_open = !start_menu_open;
            return;
        }

        // Check Desktop Icons
        if (mx >= 14 && mx < 70 && my >= 20 && my < 70) {
            windows[0].closed = 0;
            windows[0].minimized = 0;
            active_win_id = 0;
            return;
        }
        if (mx >= 14 && mx < 70 && my >= 100 && my < 150) {
            windows[1].closed = 0;
            windows[1].minimized = 0;
            active_win_id = 1;
            return;
        }
        if (mx >= 14 && mx < 70 && my >= 180 && my < 230) {
            windows[2].closed = 0;
            windows[2].minimized = 0;
            active_win_id = 2;
            return;
        }
        if (mx >= 14 && mx < 70 && my >= 260 && my < 310) {
            windows[3].closed = 0;
            windows[3].minimized = 0;
            active_win_id = 3;
            return;
        }

        // Check Window Clicks (from top priority down)
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

            // Clicked inside window frame
            if (mx >= win->x && mx < win->x + win->w && my >= win->y && my < win->y + win->h) {
                active_win_id = win->id; // Focus this window!

                // Check Close Button
                int bx_close = win->x + win->w - 18;
                if (mx >= bx_close && mx < bx_close + 12 && my >= win->y + 4 && my < win->y + 16) {
                    win->closed = 1;
                    if (active_win_id == win->id) active_win_id = -1;
                    return;
                }

                // Check Minimize Button
                int bx_min = win->x + win->w - 50;
                if (mx >= bx_min && mx < bx_min + 12 && my >= win->y + 4 && my < win->y + 16) {
                    win->minimized = 1;
                    if (active_win_id == win->id) active_win_id = -1;
                    return;
                }

                // Check Title Bar (dragging)
                if (my >= win->y && my < win->y + 24) {
                    win->dragging = 1;
                    win->drag_off_x = mx - win->x;
                    win->drag_off_y = my - win->y;
                    return;
                }

                // Check Calculator Inputs if clicked inside Calculator body
                if (win->id == 2) {
                    int start_bx = win->x + 12;
                    int start_by = win->y + 60;
                    char* layout[4][4] = {
                        {"7", "8", "9", "/"},
                        {"4", "5", "6", "*"},
                        {"1", "2", "3", "-"},
                        {"0", "C", "=", "+"}
                    };
                    for (int r = 0; r < 4; r++) {
                        for (int c = 0; c < 4; c++) {
                            int bx = start_bx + c * 40;
                            int by = start_by + r * 35;
                            if (mx >= bx && mx < bx + 34 && my >= by && my < by + 28) {
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
                                    op1 = 0;
                                    op2 = 0;
                                    calc_op = 0;
                                    calc_new_number = 1;
                                } else if (key == '=') {
                                    int temp_val = 0;
                                    for (int s = 0; calc_buf[s] != '\0'; s++) {
                                        temp_val = temp_val * 10 + (calc_buf[s] - '0');
                                    }
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
                                    for (int s = 0; calc_buf[s] != '\0'; s++) {
                                        temp_val = temp_val * 10 + (calc_buf[s] - '0');
                                    }
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

/* Keyboard character routing to focused Terminal window */
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
