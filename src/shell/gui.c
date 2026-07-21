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
#include "fs/vfs.h"

#include "../libc/stdlib.h"
#include "../cpu/idt.h"
// No setjmp.h needed

gui_window_t* windows;
int active_win_id = -1;

extern int doom_running;
extern uint32_t* doom_window_buffer;
extern int quake_running;
extern uint32_t* quake_window_buffer;
extern jmp_buf quake_exit_jmp;

uint32_t* doom_window_buffer = 0;
int doom_running = 0;
jmp_buf doom_exit_jmp;
int gui_was_clicked = 0;

/* Start Menu State */
int start_menu_open = 0;

/* Launch or resume Quake. The engine initializes exactly once and then
 * persists across window closes: re-running Host_Init on a live engine
 * corrupts its zone allocator (cvar strings from the previous zone are
 * freed into the new one), so "relaunch" resumes the existing instance. */
static void gui_launch_quake(void) {
    extern void QG_Create(int argc, char** argv);
    extern void QG_Init(void);
    extern int quake_initialized;
    static char* quake_argv[] = {"quake"};

    print_serial("[Quake] Launching game loop via setjmp...\n");
    /* Paint the placeholder frame before the blocking engine init so the
     * desktop doesn't appear frozen, and drop any stale input state. */
    gui_draw();
    QG_Init();

    if (setjmp(quake_exit_jmp) == 0) {
        quake_running = 1;
        if (!quake_initialized) {
            /* Mark before init: if Host_Init faults partway, re-running it
             * on the half-built engine would corrupt the allocators. */
            quake_initialized = 1;
            QG_Create(1, quake_argv);
        }
        /* Already initialized: the desktop loop resumes QG_Tick. */
    } else {
        print_serial("[Quake] Gracefully returned to desktop.\n");
        windows[7].closed = 1;
        if (active_win_id == 7) active_win_id = -1;
    }
}

/* Clock / Time counter */
static uint32_t ticks_counter = 0;
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
        gui_write_string("  ls          List mounted VFS files\n");
        gui_write_string("  cat <file>  Print file content\n");
        gui_write_string("  touch <f>   Create an empty file\n");
        gui_write_string("  write <f> <t> Write/Append text to file\n");
        gui_write_string("  rm <file>   Remove file from VFS\n");
        gui_write_string("  stat <file> Print file metadata\n");
        gui_write_string("  lua [file]  Run Lua script or hello snippet\n");
        gui_write_string("  tcc [file]  Compile/run C code or test suite\n");
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
    } else if (strcmp(cmd, "ls") == 0) {
        extern int vfs_get_count(void);
        extern vfs_node_t* vfs_get_node(int index);
        int count = vfs_get_count();
        gui_write_string("VFS Mounted Nodes:\n");
        for (int i = 0; i < count; i++) {
            vfs_node_t* node = vfs_get_node(i);
            if (node) {
                gui_write_string("  ");
                gui_write_string(node->name);
                gui_write_string(" (");
                char size_str[32];
                int_to_ascii(node->size, size_str);
                gui_write_string(size_str);
                gui_write_string(" B)\n");
            }
        }
    } else if (strncmp(cmd, "cat ", 4) == 0) {
        const char* filepath = cmd + 4;
        while (*filepath == ' ') filepath++;
        int fd = open(filepath, 0);
        if (fd >= 0) {
            char read_buf[256];
            int bytes;
            while ((bytes = read(fd, read_buf, 255)) > 0) {
                read_buf[bytes] = '\0';
                gui_write_string(read_buf);
            }
            close(fd);
            gui_write_string("\n");
        } else {
            gui_write_string("cat: error opening file: ");
            gui_write_string(filepath);
            gui_write_string("\n");
        }
    } else if (strncmp(cmd, "touch ", 6) == 0) {
        const char* filepath = cmd + 6;
        while (*filepath == ' ') filepath++;
        int fd = open(filepath, O_CREAT | O_WRONLY);
        if (fd >= 0) {
            close(fd);
            gui_write_string("Created file: ");
            gui_write_string(filepath);
            gui_write_string("\n");
        } else {
            gui_write_string("touch: failed to create: ");
            gui_write_string(filepath);
            gui_write_string("\n");
        }
    } else if (strncmp(cmd, "write ", 6) == 0) {
        const char* p = cmd + 6;
        while (*p == ' ') p++;
        const char* filepath = p;
        while (*p && *p != ' ') p++;
        int path_len = p - filepath;
        if (path_len > 0 && *p == ' ') {
            char path[64];
            if (path_len >= 64) path_len = 63;
            memcpy(path, filepath, path_len);
            path[path_len] = '\0';
            
            const char* text = p + 1;
            int fd = open(path, O_CREAT | O_WRONLY | O_APPEND);
            if (fd >= 0) {
                write(fd, text, strlen(text));
                write(fd, "\n", 1);
                close(fd);
                gui_write_string("Wrote to file ");
                gui_write_string(path);
                gui_write_string("\n");
            } else {
                gui_write_string("write: failed to open file\n");
            }
        } else {
            gui_write_string("Usage: write <file> <text>\n");
        }
    } else if (strncmp(cmd, "rm ", 3) == 0) {
        const char* filepath = cmd + 3;
        while (*filepath == ' ') filepath++;
        extern int unlink(const char* pathname);
        if (unlink(filepath) == 0) {
            gui_write_string("Removed file: ");
            gui_write_string(filepath);
            gui_write_string("\n");
        } else {
            gui_write_string("rm: cannot remove: ");
            gui_write_string(filepath);
            gui_write_string("\n");
        }
    } else if (strncmp(cmd, "stat ", 5) == 0) {
        const char* filepath = cmd + 5;
        while (*filepath == ' ') filepath++;
        struct stat st;
        if (stat(filepath, &st) == 0) {
            gui_write_string("File: ");
            gui_write_string(filepath);
            gui_write_string("\nSize: ");
            char size_str[32];
            int_to_ascii((int)st.st_size, size_str);
            gui_write_string(size_str);
            gui_write_string(" Bytes\nMode: S_IFREG\n");
        } else {
            gui_write_string("stat: cannot find file: ");
            gui_write_string(filepath);
            gui_write_string("\n");
        }
    } else if (strncmp(cmd, "lua ", 4) == 0 || strcmp(cmd, "lua") == 0) {
        const char* filepath = (strcmp(cmd, "lua") == 0) ? "" : cmd + 4;
        while (*filepath == ' ') filepath++;
        
        if (strlen(filepath) == 0) {
            gui_write_string("Running built-in Lua snippet:\n");
            extern int lua_main_string(const char* code);
            lua_main_string("print('Hello from Lua 5.1 on DragonOS!')\nprint('Memory usage: ' .. collectgarbage('count') .. ' KB')\n");
        } else {
            // Check if file exists
            int fd = open(filepath, 0);
            if (fd >= 0) {
                close(fd);
                char* argv[] = {"lua", (char*)filepath};
                extern int lua_main(int argc, char** argv);
                lua_main(2, argv);
            } else {
                gui_write_string("lua: cannot open script file: ");
                gui_write_string(filepath);
                gui_write_string("\n");
            }
        }
    } else if (strncmp(cmd, "tcc ", 4) == 0 || strcmp(cmd, "tcc") == 0) {
        const char* filepath = (strcmp(cmd, "tcc") == 0) ? "" : cmd + 4;
        while (*filepath == ' ') filepath++;
        
        if (strlen(filepath) == 0) {
            gui_write_string("Running built-in TCC demo:\n");
            extern int tcc_main_string(const char* code);
            const char* test_code = 
                "int main() {\n"
                "    printf(\"Hello from TCC built-in test!\\n\");\n"
                "    int sum = 0;\n"
                "    for (int i = 1; i <= 10; i++) sum = sum + i;\n"
                "    printf(\"Sum of 1..10 = %d\\n\", sum);\n"
                "    return 0;\n"
                "}\n";
            tcc_main_string(test_code);
        } else {
            // Check if file exists
            int fd = open(filepath, 0);
            if (fd >= 0) {
                close(fd);
                char* argv[] = {"tcc", (char*)filepath};
                extern int tcc_main(int argc, char** argv);
                tcc_main(2, argv);
            } else {
                gui_write_string("tcc: cannot open source file: ");
                gui_write_string(filepath);
                gui_write_string("\n");
            }
        }
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
    windows[4].x = 100;
    windows[4].y = 50;
    windows[4].w = 640;
    windows[4].h = 400;
    strcpy(windows[4].title, "Doom");
    windows[4].active = 0;
    windows[4].closed = 1;
    windows[4].minimized = 0;
    windows[4].dragging = 0;
    windows[4].id = 4;

    windows[5].x = 160;
    windows[5].y = 120;
    windows[5].w = 460;
    windows[5].h = 300;
    strcpy(windows[5].title, "File Explorer");
    windows[5].active = 0;
    windows[5].closed = 1;
    windows[5].minimized = 0;
    windows[5].dragging = 0;
    windows[5].id = 5;

    /* 6: 2048 */
    windows[6].x = 200;
    windows[6].y = 100;
    windows[6].w = 340;
    windows[6].h = 380;
    strcpy(windows[6].title, "2048");
    windows[6].active = 0;
    windows[6].closed = 1;
    windows[6].minimized = 0;
    windows[6].dragging = 0;
    windows[6].id = 6;

    /* 7: Quake */
    windows[7].x = 100;
    windows[7].y = 50;
    windows[7].w = 640;
    windows[7].h = 512;
    strcpy(windows[7].title, "Quake 1");
    windows[7].active = 0;
    windows[7].closed = 1;
    windows[7].minimized = 0;
    windows[7].dragging = 0;
    windows[7].id = 7;

    for (int i = 0; i < MAX_WINDOWS; i++) {
        windows[i].maximized = 0;
        windows[i].old_x = windows[i].x;
        windows[i].old_y = windows[i].y;
        windows[i].old_w = windows[i].w;
        windows[i].old_h = windows[i].h;
    }

    doom_window_buffer = (uint32_t*)kmalloc(640 * 400 * 4);
    
    extern uint32_t* quake_window_buffer;
    quake_window_buffer = (uint32_t*)kmalloc(640 * 480 * 4);

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
 * Draw a Windows 11 style window
 * ============================================================ */
static void draw_window_chrome(gui_window_t* win) {
    int x = win->x, y = win->y, w = win->w, h = win->h;
    int is_active = (active_win_id == win->id);
    int titlebar_h = 32;
    int radius = 8;

    /* Drop shadow (top-rounded) */
    draw_top_rounded_rect_translucent(x + 2, y + 2, w, h, radius, 0x000000, 30);
    draw_top_rounded_rect_translucent(x + 1, y + 1, w, h, radius, 0x000000, 15);

    /* Window body with top-rounded corners, flat bottom */
    draw_top_rounded_rect(x, y, w, h, radius, WIN11_WINDOW_BG);

    /* Titlebar */
    uint32_t tb_color = is_active ? WIN11_TITLEBAR : WIN11_TITLEBAR_INACTIVE;
    draw_top_rounded_rect(x, y, w, titlebar_h, radius, tb_color);
    /* Fill in the bottom of titlebar (flat, no bottom rounding) */
    draw_rect(x, y + radius, w, titlebar_h - radius, tb_color);

    /* Border */
    uint32_t border_c = is_active ? WIN11_ACCENT : WIN11_BORDER;
    draw_top_rounded_rect_outline(x, y, w, h, radius, border_c);

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
        draw_top_rounded_rect(close_x, btn_y, btn_w, btn_h, radius - 2, close_bg);
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
    struct { const char* label; uint32_t bg; uint32_t fg; const char* symbol; } icons[7] = {
        {"System",   0x0078D4, 0xFFFFFF, "PC"},
        {"Terminal", 0x0C0C0C, 0x00FF00, ">_"},
        {"Calc",     0x202020, 0xFFFFFF, "+-"},
        {"Monitor",  0x1A1A1A, 0x00CC6A, "/\\"},
        {"DOOM",     0xC21807, 0xFFFFFF, "DM"},
        {"Explorer", 0xDF8A10, 0xFFFFFF, "FE"},
        {"Quake 1",  0x8B4513, 0xFFFFFF, "Q1"},
    };

    for (int i = 0; i < 7; i++) {
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
static void gui_draw_2048(int x, int y, int w, int h);

static void draw_window_content(gui_window_t* win) {
    int x = win->x, y = win->y, w = win->w, h = win->h;
    int content_y = y + 32;
    int content_h = h - 32;

    graphics_set_clip(x + 1, content_y, w - 2, content_h - 1);

    if (win->id == 0) {
        /* ---- System Information ---- */
        /* Info cards on modern Windows 11 dark theme */
        draw_rect(x + 1, content_y, w - 2, content_h - 1, WIN11_WINDOW_BG);

        /* CPU card */
        draw_rounded_rect(x + 12, content_y + 12, w - 24, 36, 6, WIN11_CARD_BG);
        draw_rounded_rect_outline(x + 12, content_y + 12, w - 24, 36, 6, WIN11_CARD_BORDER);
        draw_string(x + 22, content_y + 14, "CPU", WIN11_ACCENT);
        draw_string(x + 22, content_y + 30, "Intel(R) Core(TM) i9 CPU @ 3.80GHz", WIN11_TEXT_PRIMARY);

        /* RAM card with Visual Progress Bar */
        draw_rounded_rect(x + 12, content_y + 54, w - 24, 52, 6, WIN11_CARD_BG);
        draw_rounded_rect_outline(x + 12, content_y + 54, w - 24, 52, 6, WIN11_CARD_BORDER);
        draw_string(x + 22, content_y + 56, "Memory", WIN11_ACCENT);
        
        char mem_str[64];
        char num_buf[32];
        strcpy(mem_str, "Used: ");
        int_to_ascii(pmm_used_memory / 1024 / 1024, num_buf);
        strcat(mem_str, num_buf);
        strcat(mem_str, " MB / Total: ");
        int_to_ascii(pmm_total_memory / 1024 / 1024, num_buf);
        strcat(mem_str, num_buf);
        strcat(mem_str, " MB");
        draw_string(x + 22, content_y + 72, mem_str, WIN11_TEXT_PRIMARY);

        /* RAM fill bar rendering */
        int bar_w = w - 48;
        int bar_x = x + 24;
        int bar_y = content_y + 90;
        int fill_w = 0;
        if (pmm_total_memory > 0) {
            fill_w = (int)((pmm_used_memory * bar_w) / pmm_total_memory);
            if (fill_w > bar_w) fill_w = bar_w;
        }
        draw_rounded_rect(bar_x, bar_y, bar_w, 6, 3, 0x333333); // bar track
        draw_rounded_rect(bar_x, bar_y, fill_w, 6, 3, WIN11_ACCENT); // bar fill

        /* OS card */
        draw_rounded_rect(x + 12, content_y + 112, w - 24, 36, 6, WIN11_CARD_BG);
        draw_rounded_rect_outline(x + 12, content_y + 112, w - 24, 36, 6, WIN11_CARD_BORDER);
        draw_string(x + 22, content_y + 114, "System", WIN11_ACCENT);
        draw_string(x + 22, content_y + 130, "DragonOS 64-bit Core Edition", WIN11_TEXT_PRIMARY);

        /* PCI Devices card */
        draw_rounded_rect(x + 12, content_y + 154, w - 24, 90, 6, WIN11_CARD_BG);
        draw_rounded_rect_outline(x + 12, content_y + 154, w - 24, 90, 6, WIN11_CARD_BORDER);
        draw_string(x + 22, content_y + 156, "PCI Hardware", WIN11_ACCENT);

        int line_offset = 0;
        pci_device_t* curr = pci_devices_head;
        while (curr && line_offset < 4) {
            char pci_str[64];
            strcpy(pci_str, curr->class_name);
            draw_string(x + 22, content_y + 172 + line_offset * 14, pci_str, WIN11_TEXT_PRIMARY);
            curr = curr->next;
            line_offset++;
        }
        if (line_offset == 0) {
            draw_string(x + 22, content_y + 172, "No devices scanned.", WIN11_TEXT_PRIMARY);
        }
    }
    else if (win->id == 1) {
        /* ---- Terminal ---- */
        int pad = 4;
        draw_rect(x + 1, content_y, w - 2, content_h - 1, 0x0C0C0C);

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

                /* Hover highlight check */
                int mouse_in_btn = (mouse_x >= bx && mouse_x < bx + btn_w && mouse_y >= by && mouse_y < by + btn_h);

                uint32_t btn_bg;
                uint32_t btn_fg;
                if (key == '=') {
                    btn_bg = mouse_in_btn ? 0x0078D4 : WIN11_ACCENT;
                    btn_fg = 0xFFFFFF;
                } else if (key >= '0' && key <= '9') {
                    btn_bg = mouse_in_btn ? 0x4D4D4D : 0x3B3B3B;
                    btn_fg = 0xFFFFFF;
                } else {
                    btn_bg = mouse_in_btn ? 0x404040 : 0x323232;
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
        draw_rect(x + 1, content_y, w - 2, content_h - 1, 0x000000);
        if (doom_window_buffer) {
            int dx = (w - 640) / 2;
            int dy = (content_h - 400) / 2;
            if (dx < 0) dx = 0;
            if (dy < 0) dy = 0;
            
            int px = x + dx;
            int py = content_y + dy;
            int src_x = 0;
            int copy_w = 640;
            
            if (px < 0) {
                src_x = -px;
                copy_w -= src_x;
                px = 0;
            }
            if (px + copy_w > (int)screen_width) {
                copy_w = (int)screen_width - px;
            }
            
            if (copy_w > 0) {
                for (int r = 0; r < 400; r++) {
                    int screen_y = py + r;
                    if (screen_y < 0) continue;
                    if (screen_y >= content_y + content_h) break;
                    if (screen_y >= (int)screen_height) break;
                    
                    uint32_t* dest_row = &back_buffer[screen_y * screen_width + px];
                    uint32_t* src_row = &doom_window_buffer[r * 640 + src_x];
                    memcpy(dest_row, src_row, copy_w * 4);
                }
            }
        } else {
            draw_string(x + w/2 - 40, content_y + h/2 - 8, "Loading DOOM...", 0xFFFFFF);
        }
    }
    else if (win->id == 7) {
        /* ---- Quake window ---- */
        draw_rect(x + 1, content_y, w - 2, content_h - 1, 0x000000);
        extern uint32_t* quake_window_buffer;
        if (quake_window_buffer) {
            int dx = (w - 640) / 2;
            int dy = (content_h - 480) / 2;
            if (dx < 0) dx = 0;
            if (dy < 0) dy = 0;
            
            int px = x + dx;
            int py = content_y + dy;
            int src_x = 0;
            int copy_w = 640;
            
            if (px < 0) {
                src_x = -px;
                copy_w -= src_x;
                px = 0;
            }
            if (px + copy_w > (int)screen_width) {
                copy_w = (int)screen_width - px;
            }
            
            if (copy_w > 0) {
                for (int r = 0; r < 480; r++) {
                    int screen_y = py + r;
                    if (screen_y < 0) continue;
                    if (screen_y >= content_y + content_h) break;
                    if (screen_y >= (int)screen_height) break;
                    
                    uint32_t* dest_row = &back_buffer[screen_y * screen_width + px];
                    uint32_t* src_row = &quake_window_buffer[r * 640 + src_x];
                    memcpy(dest_row, src_row, copy_w * 4);
                }
            }
        } else {
            draw_string(x + w/2 - 40, content_y + h/2 - 8, "Loading QUAKE...", 0xFFFFFF);
        }
    }
    else if (win->id == 5) {
        /* ---- File Explorer (Windows 11 Fluent style) ---- */
        draw_rect(x + 1, content_y, w - 2, content_h - 1, 0x1E1E1E);

        /* Left Sidebar (Frosted-like dark grey) */
        draw_rect(x + 1, content_y, 110, content_h - 1, 0x252525);
        draw_vline(x + 111, content_y, content_h - 1, 0x2D2D2D);

        /* Sidebar shortcuts */
        draw_string(x + 12, content_y + 16, " Quick Access", 0x888888);
        
        draw_rounded_rect(x + 8, content_y + 36, 96, 22, 4, 0x333333);
        draw_string(x + 12, content_y + 40, " Home", 0x60CDFF);
        
        draw_string(x + 12, content_y + 68, " Desktop", 0xCCCCCC);
        draw_string(x + 12, content_y + 96, " Documents", 0xCCCCCC);
        draw_string(x + 12, content_y + 124, " Downloads", 0xCCCCCC);
        draw_string(x + 12, content_y + 152, " Local Disk (C:)", 0xCCCCCC);

        /* New File Button */
        draw_rounded_rect(x + 8, content_y + 180, 96, 22, 4, 0x005FB8); // Windows 11 Blue
        draw_string(x + 24, content_y + 184, "New File", 0xFFFFFF);

        /* Address Bar */
        draw_rounded_rect(x + 120, content_y + 8, w - 132, 22, 4, 0x2B2B2B);
        draw_rounded_rect_outline(x + 120, content_y + 8, w - 132, 22, 4, 0x3D3D3D);
        draw_string(x + 128, content_y + 11, "This PC > Local Disk (C:)", 0xCCCCCC);

        /* Main View Header */
        draw_string(x + 124, content_y + 38, "Name", 0x888888);
        draw_string(x + 350, content_y + 38, "Size", 0x888888);
        draw_hline(x + 120, content_y + 54, w - 132, 0x2D2D2D);

        int count = vfs_get_count();
        if (count > 7) count = 7; // Cap layout to fit window bounds
        
        for (int i = 0; i < count; i++) {
            vfs_node_t* node = vfs_get_node(i);
            if (!node) continue;
            
            int ry = content_y + 60 + i * 26;

            /* File Icon (Fluent Orange folder) */
            draw_rounded_rect(x + 124, ry, 18, 16, 3, 0xDF8A10);
            draw_rect(x + 126, ry - 2, 8, 4, 0xDF8A10); // folder tab
            
            /* File Name */
            draw_string(x + 148, ry + 1, node->name, 0xFFFFFF);

            /* File Size */
            char size_str[32];
            char num_buf[16];
            int_to_ascii(node->size, num_buf);
            strcpy(size_str, num_buf);
            strcat(size_str, " B");
            draw_string(x + 350, ry + 1, size_str, 0x888888);
        }
    } else if (win->id == 6) {
        gui_draw_2048(x, content_y, w, content_h);
    }
    
    graphics_clear_clip();
}

extern int board_2048[4][4];
extern int score_2048;
extern int game_over_2048;

static int score_2048_best = 0;

static void gui_draw_2048(int x, int y, int w, int h) {
    if (score_2048 > score_2048_best) {
        score_2048_best = score_2048;
    }

    draw_rect(x, y, w, h, 0xFAF8EF); // Light beige background

    /* Logo Panel */
    draw_rounded_rect(x + 20, y + 10, 60, 30, 4, 0xEDC22E);
    draw_string(x + 28, y + 16, "2048", 0xFFFFFF);

    /* Score Panel */
    draw_rounded_rect(x + 95, y + 10, 100, 30, 4, 0xBBADA0);
    draw_string(x + 103, y + 12, "SCORE", 0xEEE4DA);
    char score_str[16];
    int_to_ascii(score_2048, score_str);
    draw_string(x + 103, y + 22, score_str, 0xFFFFFF);

    /* Best Score Panel */
    draw_rounded_rect(x + 205, y + 10, 110, 30, 4, 0xBBADA0);
    draw_string(x + 213, y + 12, "BEST SCORE", 0xEEE4DA);
    char best_str[16];
    int_to_ascii(score_2048_best, best_str);
    draw_string(x + 213, y + 22, best_str, 0xFFFFFF);

    int cell_size = 60;
    int padding = 6;
    int grid_w = (cell_size + padding) * 4 + padding; // 270 px width
    int grid_x = x + (w - grid_w) / 2;
    int grid_y = y + 48;
    
    /* Draw board background */
    draw_rounded_rect(grid_x, grid_y, grid_w, grid_w, 6, 0xBBADA0);

    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            int cx = grid_x + padding + col * (cell_size + padding);
            int cy = grid_y + padding + row * (cell_size + padding);
            
            int val = board_2048[row][col];
            uint32_t color = 0xCDC1B4; // empty
            if (val == 2) color = 0xEEE4DA;
            else if (val == 4) color = 0xEDE0C8;
            else if (val == 8) color = 0xF2B179;
            else if (val == 16) color = 0xF59563;
            else if (val == 32) color = 0xF67C5F;
            else if (val == 64) color = 0xF65E3B;
            else if (val == 128) color = 0xEDCF72;
            else if (val == 256) color = 0xEDCC61;
            else if (val == 512) color = 0xEDC850;
            else if (val == 1024) color = 0xEDC53F;
            else if (val == 2048) color = 0xEDC22E;
            else if (val > 2048) color = 0x3C3A32;

            draw_rounded_rect(cx, cy, cell_size, cell_size, 4, color);
            
            if (val > 0) {
                char v_str[16];
                int_to_ascii(val, v_str);
                uint32_t t_color = (val <= 4) ? 0x776E65 : 0xF9F6F2;
                int text_x = cx + cell_size / 2 - (strlen(v_str) * 8) / 2;
                int text_y = cy + cell_size / 2 - 8;
                draw_string(text_x, text_y, v_str, t_color);
            }
        }
    }
    
    if (game_over_2048) {
        draw_rounded_rect_translucent(grid_x, grid_y, grid_w, grid_w, 6, 0xFAF8EF, 200);
        draw_string(grid_x + grid_w / 2 - 40, grid_y + grid_w / 2 - 8, "GAME OVER", 0x776E65);
    }

    draw_string(x + 16, y + h - 22, "WASD: Slide tiles | R: Reset Game", 0x776E65);
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

    /* 4. Start Menu (Ubuntu Applications Dropdown) */
    if (start_menu_open) {
        int sm_w = 340;
        int sm_h = 360;
        int sm_x = 0;
        int sm_y = 32;

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

        /* Pinned apps grid (3x2) */
        struct { const char* name; uint32_t color; const char* sym; int win_id; } pinned[6] = {
            {"System",   0x0078D4, "PC",  0},
            {"Terminal", 0x0C0C0C, ">_",  1},
            {"Calc",     0x3B3B3B, "+-",  2},
            {"Explorer", 0xDF8A10, "FE",  5},
            {"Doom",     0xC21807, "DM",  4},
            {"2048",     0xEDC22E, "2K",  6},
        };

        int grid_x = sm_x + 20;
        int grid_y = sm_y + 84;
        int tile_w = (sm_w - 60) / 2;
        int tile_h = 54;
        int gap = 8;

        for (int i = 0; i < 6; i++) {
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

    /* 5. Top Bar (Ubuntu GNOME style) */
    int tb_h = 32;
    int tby = 0;

    /* Top Bar background */
    draw_rect(0, tby, screen_width, tb_h, 0x1A1A1A);
    draw_hline(0, tby + tb_h, screen_width, 0x2D2D2D);

    /* "Activities" / "Applications" button at Top Left */
    int start_x = 10;
    int start_y = 4;
    int mouse_in_start = (mouse_x >= start_x && mouse_x < start_x + 100 && mouse_y >= start_y && mouse_y < start_y + 24);
    draw_rounded_rect(start_x, start_y, 100, 24, 4, mouse_in_start ? 0x333333 : 0x1A1A1A);
    draw_string(start_x + 10, start_y + 4, "Applications", 0xFFFFFF);

    /* Active App Window list in top bar */
    int icons_start_x = 130;
    int icon_size = 24;
    int icon_gap = 8;
    
    for (int i = 0; i < MAX_WINDOWS; i++) {
        gui_window_t* w = &windows[i];
        if (w->closed) continue;

        int ix = icons_start_x + i * (icon_size + icon_gap);
        int iy = 4;

        /* Icon background with hover */
        int mouse_in_app = (mouse_x >= ix && mouse_x < ix + icon_size && mouse_y >= iy && mouse_y < iy + icon_size);
        uint32_t bg = 0x1A1A1A;
        if (active_win_id == w->id) {
            bg = mouse_in_app ? 0x4D4D4D : 0x333333;
        } else {
            bg = mouse_in_app ? 0x3D3D3D : 0x1A1A1A;
        }
        draw_rounded_rect(ix, iy, icon_size, icon_size, 4, bg);
        
        /* Tiny colored square based on window ID */
        uint32_t c = (w->id == 0) ? 0x0078D4 : (w->id == 1) ? 0x0C0C0C : (w->id == 2) ? 0x3B3B3B : (w->id == 4) ? 0xC21807 : (w->id == 6) ? 0xEDC22E : 0xDF8A10;
        draw_rect(ix + 6, iy + 6, 12, 12, c);
        
        /* Active indicator line at bottom */
        if (active_win_id == w->id) {
            draw_hline(ix + 2, iy + icon_size + 2, icon_size - 4, 0xDF8A10); /* Ubuntu orange */
        }
    }

    /* System tray (right side) */
    /* Update clock using CMOS RTC */
    ticks_counter++;
    if (ticks_counter % 100 == 0) {
        uint8_t rtc_hour = 0, rtc_min = 0, rtc_sec = 0;
        extern void rtc_read_time(uint8_t* hours, uint8_t* minutes, uint8_t* seconds);
        rtc_read_time(&rtc_hour, &rtc_min, &rtc_sec);
        
        int pm = 0;
        int disp_hour = rtc_hour;
        if (disp_hour >= 12) {
            pm = 1;
            if (disp_hour > 12) disp_hour -= 12;
        } else if (disp_hour == 0) {
            disp_hour = 12;
        }
        
        time_str[0] = '0' + (disp_hour / 10);
        time_str[1] = '0' + (disp_hour % 10);
        time_str[2] = ':';
        time_str[3] = '0' + (rtc_min / 10);
        time_str[4] = '0' + (rtc_min % 10);
        time_str[5] = ':';
        time_str[6] = '0' + (rtc_sec / 10);
        time_str[7] = '0' + (rtc_sec % 10);
        time_str[8] = ' ';
        time_str[9] = pm ? 'P' : 'A';
        time_str[10] = 'M';
        time_str[11] = '\0';
    }

    /* System tray (right side) */
    int tray_x = (int)screen_width - 160;

    /* Wi-Fi icon */
    for (int arc = 0; arc < 3; arc++) {
        int r = 3 + arc * 3;
        draw_circle(tray_x, tby + 16, r, 0xCCCCCC);
    }
    draw_rect(tray_x - 1, tby + 17, 3, 8, 0x1A1A1A); /* clear bottom half */

    /* Volume icon */
    draw_rect(tray_x + 24, tby + 12, 4, 8, 0xCCCCCC);
    draw_rect(tray_x + 28, tby + 10, 2, 12, 0xCCCCCC);
    draw_circle(tray_x + 34, tby + 16, 5, 0xCCCCCC);
    draw_rect(tray_x + 29, tby + 10, 6, 12, 0x1A1A1A); /* mask inner circle */

    /* Battery indicator */
    draw_rect_outline(tray_x + 48, tby + 11, 16, 10, 0xCCCCCC);
    draw_rect(tray_x + 64, tby + 14, 2, 4, 0xCCCCCC);
    draw_rect(tray_x + 50, tby + 13, 12, 6, 0x00FF00); /* Fill */

    /* Clock text */
    draw_string(tray_x + 74, tby + 8, time_str, 0xFFFFFF);

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

    if (click && !gui_was_clicked) {
        gui_was_clicked = 1;

        /* Applications button click */
        int start_x = 10;
        int start_y = 4;
        if (mx >= start_x && mx < start_x + 100 && my >= start_y && my < start_y + 24) {
            start_menu_open = !start_menu_open;
            return;
        }

        /* App icon clicks in Top Bar */
        int icons_start_x = 130;
        int icon_size = 24;
        int icon_gap = 8;
        for (int i = 0; i < MAX_WINDOWS; i++) {
            gui_window_t* w = &windows[i];
            if (w->closed) continue;

            int ix = icons_start_x + i * (icon_size + icon_gap);
            int iy = 4;

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
            int sm_x = 0;
            int sm_y = 32;

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

            struct { const char* name; uint32_t color; const char* sym; int win_id; } pinned[7] = {
                {"System",   0x0078D4, "PC",  0},
                {"Terminal", 0x0C0C0C, ">_",  1},
                {"Calc",     0x3B3B3B, "+-",  2},
                {"Explorer", 0xDF8A10, "FE",  5},
                {"Doom",     0xC21807, "DM",  4},
                {"2048",     0xEDC22E, "2K",  6},
                {"Quake 1",  0x8B4513, "Q1",  7},
            };

            for (int i = 0; i < 7; i++) {
                int col = i % 2;
                int row = i / 2;
                int tx = grid_x + col * (tile_w + gap);
                int ty = grid_y + row * (tile_h + gap);

                if (mx >= tx && mx < tx + tile_w && my >= ty && my < ty + tile_h) {
                    int win_id = pinned[i].win_id;
                    if (win_id >= 0) {
                        windows[win_id].closed = 0;
                        windows[win_id].minimized = 0;
                        active_win_id = win_id;
                        
                        if (win_id == 4 && !doom_running) {
                            extern void doomgeneric_Create(int argc, char** argv);
                            char* doom_argv[] = {"doomgeneric", "-iwad", "/boot/doom1.wad"};
                            print_serial("[Doom] Launching game loop via setjmp...\n");
                            /* Paint the "Loading..." placeholder before the blocking
                             * engine init so the desktop doesn't appear frozen. */
                            gui_draw();
                            if (setjmp(doom_exit_jmp) == 0) {
                                doom_running = 1;
                                doomgeneric_Create(3, doom_argv);
                            } else {
                                print_serial("[Doom] Gracefully returned to desktop.\n");
                            }
                        }
                        else if (win_id == 7 && !quake_running) {
                            gui_launch_quake();
                        }
                    }
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
        for (int i = 0; i < 7; i++) {
            int ix = 30;
            int iy = 30 + i * 90;
            if (mx >= ix && mx < ix + 48 && my >= iy && my < iy + 70) {
                int win_id = i;
                if (i == 6) win_id = 7; // Quake
                if (i == 5) win_id = 5; // Explorer
                windows[win_id].closed = 0;
                windows[win_id].minimized = 0;
                active_win_id = win_id;
                start_menu_open = 0;

                if (win_id == 4 && !doom_running) {
                    extern void doomgeneric_Create(int argc, char** argv);
                    char* doom_argv[] = {"doomgeneric", "-iwad", "/boot/doom1.wad"};
                    print_serial("[Doom] Launching game loop via setjmp...\n");
                    gui_draw();
                    if (setjmp(doom_exit_jmp) == 0) {
                        doom_running = 1;
                        doomgeneric_Create(3, doom_argv);
                    } else {
                        print_serial("[Doom] Gracefully returned to desktop.\n");
                    }
                }
                else if (win_id == 7 && !quake_running) {
                    gui_launch_quake();
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

                /* Maximize button */
                int max_x = close_x - btn_w;
                if (mx >= max_x && mx < max_x + btn_w && my >= win->y && my < win->y + 30) {
                    if (win->maximized) {
                        win->maximized = 0;
                        win->x = win->old_x;
                        win->y = win->old_y;
                        win->w = win->old_w;
                        win->h = win->old_h;
                    } else {
                        win->maximized = 1;
                        win->old_x = win->x;
                        win->old_y = win->y;
                        win->old_w = win->w;
                        win->old_h = win->h;
                        win->x = 0;
                        win->y = 0;
                        win->w = screen_width;
                        win->h = screen_height - 48;
                    }
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
                    if (!win->maximized) {
                        win->dragging = 1;
                        win->drag_off_x = mx - win->x;
                        win->drag_off_y = my - win->y;
                    }
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
                
                /* File Explorer clicks */
                if (win->id == 5) {
                    int content_y = win->y + 32;
                    
                    // Check if New File button clicked
                    if (mx >= win->x + 8 && mx < win->x + 104 && my >= content_y + 180 && my < content_y + 202) {
                        extern void vfs_create_file(const char* name, uint32_t size);
                        vfs_create_file("test.lua", 4096);
                        
                        // Populate with a default hello world script
                        int new_fd = open("test.lua", 0);
                        if (new_fd >= 0) {
                            const char* default_lua = "print('Hello from dynamic DragonOS RAM Disk!')\n";
                            write(new_fd, default_lua, strlen(default_lua));
                            close(new_fd);
                        }
                    }

                    if (mx >= win->x + 120 && mx < win->x + win->w && my >= content_y + 60 && my < content_y + 60 + 7 * 26) {
                        int clicked_row = (my - (content_y + 60)) / 26;
                        int count = vfs_get_count();
                        if (clicked_row >= 0 && clicked_row < count) {
                            vfs_node_t* node = vfs_get_node(clicked_row);
                            if (node) {
                                // If clicked doom1.wad, launch Doom!
                                if (strcmp(node->name, "doom1.wad") == 0 || strcmp(node->name, "/boot/doom1.wad") == 0) {
                                    win->closed = 1;
                                    active_win_id = 4;
                                    windows[4].closed = 0;
                                    windows[4].minimized = 0;
                                    
                                    if (!doom_running) {
                                        extern void doomgeneric_Create(int argc, char** argv);
                                        char* doom_argv[] = {"doomgeneric", "-iwad", "/boot/doom1.wad"};
                                        print_serial("[Doom] Launching game loop via setjmp...\n");
                                        if (setjmp(doom_exit_jmp) == 0) {
                                            doom_running = 1;
                                            doomgeneric_Create(3, doom_argv);
                                        } else {
                                            print_serial("[Doom] Exited successfully back to GUI loop.\n");
                                        }
                                    }
                                }
                                // If clicked a .lua file, run it in the Lua VM
                                else {
                                    int len = strlen(node->name);
                                    if (len > 4 && strcmp(node->name + len - 4, ".lua") == 0) {
                                        int fd = open(node->name, 0);
                                        if (fd >= 0) {
                                            char* script_buf = kmalloc(node->size + 1);
                                            if (script_buf) {
                                                int bytes = read(fd, script_buf, node->size);
                                                if (bytes > 0) {
                                                    script_buf[bytes] = '\0';
                                                    extern int lua_main_string(const char* code);
                                                    gui_write_string("Executing ");
                                                    gui_write_string(node->name);
                                                    gui_write_string("...\n");
                                                    lua_main_string(script_buf);
                                                }
                                                kfree(script_buf);
                                            }
                                            close(fd);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                return;
            }
        }
    }

    if (!click) {
        gui_was_clicked = 0;
    }
}

/* ============================================================
 * Keyboard character routing to focused Terminal window
 * ============================================================ */
void gui_handle_keyboard(char c) {
    if (active_win_id == 6) {
        extern void move_2048(int dir);
        extern void init_2048(void);
        if (c == 'w' || c == 'W') move_2048(0);
        else if (c == 's' || c == 'S') move_2048(1);
        else if (c == 'a' || c == 'A') move_2048(2);
        else if (c == 'd' || c == 'D') move_2048(3);
        else if (c == 'r' || c == 'R') init_2048();
        return;
    }

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
            gui_write_char('\b');
        }
    } else {
        if (cmd_buf_idx < 120) {
            command_buffer[cmd_buf_idx++] = c;
            command_buffer[cmd_buf_idx] = '\0';
        }
    }
}

void gui_close_quake(void) {
    if (windows) windows[7].closed = 1;
    if (active_win_id == 7) active_win_id = -1;
}

void gui_close_doom(void) {
    if (windows) windows[4].closed = 1;
    if (active_win_id == 4) active_win_id = -1;
    gui_was_clicked = 0;
}

static void gui_bsod_hex(uint32_t x, uint32_t y, uint64_t val, uint32_t color) {
    char buf[20];
    char* hex = "0123456789ABCDEF";
    buf[19] = '\0';
    int i = 18;
    if (val == 0) {
        buf[i--] = '0';
    } else {
        while (val > 0 && i >= 0) {
            buf[i--] = hex[val % 16];
            val /= 16;
        }
    }
    buf[i--] = 'x';
    buf[i] = '0';
    
    char* p = &buf[i];
    while (*p) {
        draw_char(x, y, *p, color);
        x += 8;
        p++;
    }
}

static void gui_bsod_str(uint32_t x, uint32_t y, const char* str, uint32_t color) {
    while (*str) {
        draw_char(x, y, *str, color);
        x += 8;
        str++;
    }
}

void gui_bsod(void* r_ptr, uint64_t cr2) {
    registers_t* r = (registers_t*)r_ptr;
    
    // Disable clipping just in case
    graphics_clear_clip();
    
    // Blue background
    draw_rect(0, 0, 1024, 768, 0x0000AA);
    
    // Draw text
    uint32_t white = 0xFFFFFF;
    uint32_t start_y = 50;
    
    gui_bsod_str(50, start_y, "A fatal exception has occurred in DragonOS.", white);
    start_y += 30;
    gui_bsod_str(50, start_y, "The system has been halted to prevent damage to your computer.", white);
    start_y += 30;
    
    gui_bsod_str(50, start_y, "VECTOR: ", white);
    gui_bsod_hex(150, start_y, r->int_no, white);
    start_y += 20;
    
    gui_bsod_str(50, start_y, "ERROR CODE: ", white);
    gui_bsod_hex(150, start_y, r->err_code, white);
    start_y += 20;
    
    gui_bsod_str(50, start_y, "RIP: ", white);
    gui_bsod_hex(150, start_y, r->rip, white);
    start_y += 20;
    
    gui_bsod_str(50, start_y, "RSP: ", white);
    gui_bsod_hex(150, start_y, r->rsp, white);
    start_y += 20;
    
    gui_bsod_str(50, start_y, "CR2: ", white);
    gui_bsod_hex(150, start_y, cr2, white);
    start_y += 30;
    
    gui_bsod_str(50, start_y, "RDI: ", white); gui_bsod_hex(100, start_y, r->rdi, white);
    gui_bsod_str(250, start_y, "RSI: ", white); gui_bsod_hex(300, start_y, r->rsi, white);
    start_y += 20;
    gui_bsod_str(50, start_y, "RAX: ", white); gui_bsod_hex(100, start_y, r->rax, white);
    gui_bsod_str(250, start_y, "RBX: ", white); gui_bsod_hex(300, start_y, r->rbx, white);
    start_y += 20;
    gui_bsod_str(50, start_y, "RCX: ", white); gui_bsod_hex(100, start_y, r->rcx, white);
    gui_bsod_str(250, start_y, "RDX: ", white); gui_bsod_hex(300, start_y, r->rdx, white);
    start_y += 40;
    
    gui_bsod_str(50, start_y, "Please restart your computer.", white);
    
    // Swap buffer to screen so it's visible
    blit_buffer();
}
