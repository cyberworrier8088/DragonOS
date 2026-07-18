#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <stdint.h>

extern uint32_t* framebuffer;
extern uint32_t* back_buffer;
extern uint32_t  screen_width;
extern uint32_t  screen_height;
extern uint32_t  screen_pitch;

void graphics_init(uint64_t phys_addr, uint32_t width, uint32_t height, uint32_t pitch);
void draw_pixel(uint32_t x, uint32_t y, uint32_t color);
void draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void draw_rect_outline(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void draw_rect_translucent(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color, uint8_t alpha);
void draw_char(uint32_t x, uint32_t y, char c, uint32_t color);
void draw_string(uint32_t x, uint32_t y, const char* str, uint32_t color);
void draw_circle(int32_t x0, int32_t y0, int32_t radius, uint32_t color);
void draw_desktop_gradient(void);
void blit_buffer(void);

#endif
