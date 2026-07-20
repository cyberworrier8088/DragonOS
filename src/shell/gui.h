#ifndef GUI_H
#define GUI_H

#include <stdint.h>

#define MAX_WINDOWS 8

typedef struct {
    int x, y, w, h;
    char title[64];
    int active;
    int minimized;
    int closed;
    int dragging;
    int drag_off_x, drag_off_y;
    int id; // 0 = Computer, 1 = Terminal, 2 = Calculator, 3 = System Monitor
    int maximized;
    int old_x, old_y, old_w, old_h;
} gui_window_t;

void init_gui(void);
void gui_draw(void);
void gui_handle_mouse(int mx, int my, int click, int r_click);
void gui_handle_keyboard(char c);
void gui_write_char(char c);
void gui_write_string(const char* str);

#endif
