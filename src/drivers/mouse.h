#ifndef MOUSE_H
#define MOUSE_H

#include "../cpu/idt.h"

extern int mouse_x;
extern int mouse_y;
extern int mouse_l_click;
extern int mouse_r_click;
extern int mouse_dx;
extern int mouse_dy;

void init_mouse(void);
void mouse_handler(registers_t* r);

#endif
