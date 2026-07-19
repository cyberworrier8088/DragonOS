#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <stdint.h>

extern uint32_t* framebuffer;
extern uint32_t* back_buffer;
extern uint32_t  screen_width;
extern uint32_t  screen_height;
extern uint32_t  screen_pitch;

/* Font dimensions for the modern 10x18 Segoe-style font */
#define FONT_WIDTH  10
#define FONT_HEIGHT 18

void graphics_init(uint64_t phys_addr, uint32_t width, uint32_t height, uint32_t pitch);

/* Core drawing primitives */
void draw_pixel(uint32_t x, uint32_t y, uint32_t color);
void draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void draw_rect_outline(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void draw_rect_translucent(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color, uint8_t alpha);

/* Modern drawing primitives for Fluent Design */
void draw_rounded_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t radius, uint32_t color);
void draw_rounded_rect_outline(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t radius, uint32_t color);
void draw_rounded_rect_translucent(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t radius, uint32_t color, uint8_t alpha);
void draw_hline(uint32_t x, uint32_t y, uint32_t length, uint32_t color);
void draw_vline(uint32_t x, uint32_t y, uint32_t length, uint32_t color);
void draw_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color);
void draw_gradient_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color_top, uint32_t color_bottom);
void draw_circle(int32_t x0, int32_t y0, int32_t radius, uint32_t color);
void draw_circle_filled(int32_t x0, int32_t y0, int32_t radius, uint32_t color);

/* Text rendering with modern Segoe-style font */
void draw_char(uint32_t x, uint32_t y, char c, uint32_t color);
void draw_string(uint32_t x, uint32_t y, const char* str, uint32_t color);

/* Desktop wallpaper */
void draw_desktop_gradient(void);

/* Buffer management */
void blit_buffer(void);

/* Alpha-blended pixel (used for shadows and translucent effects) */
void draw_pixel_alpha(uint32_t x, uint32_t y, uint32_t color, uint8_t alpha);

#endif
