#include "graphics.h"
#include "../libc/font.h"
#include "../libc/string.h"

uint32_t* framebuffer = 0;
uint32_t  screen_width = 800;
uint32_t  screen_height = 600;
uint32_t  screen_pitch = 800 * 4;

/* 800x600 Double buffer memory allocation */
static uint32_t back_buffer_storage[800 * 600];
uint32_t* back_buffer = back_buffer_storage;

void graphics_init(uint64_t phys_addr, uint32_t width, uint32_t height, uint32_t pitch) {
    framebuffer = (uint32_t*)phys_addr;
    screen_width = width;
    screen_height = height;
    screen_pitch = pitch;
    memset(back_buffer, 0, screen_width * screen_height * 4);
}

void draw_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x >= screen_width || y >= screen_height) return;
    back_buffer[y * screen_width + x] = color;
}

void draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    for (uint32_t i = 0; i < h; i++) {
        uint32_t py = y + i;
        if (py >= screen_height) break;
        for (uint32_t j = 0; j < w; j++) {
            uint32_t px = x + j;
            if (px >= screen_width) break;
            back_buffer[py * screen_width + px] = color;
        }
    }
}

void draw_rect_outline(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    for (uint32_t px = x; px < x + w; px++) {
        draw_pixel(px, y, color);
        draw_pixel(px, y + h - 1, color);
    }
    for (uint32_t py = y; py < y + h; py++) {
        draw_pixel(x, py, color);
        draw_pixel(x + w - 1, py, color);
    }
}

void draw_rect_translucent(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color, uint8_t alpha) {
    uint32_t r_src = (color >> 16) & 0xFF;
    uint32_t g_src = (color >> 8) & 0xFF;
    uint32_t b_src = color & 0xFF;

    for (uint32_t i = 0; i < h; i++) {
        uint32_t py = y + i;
        if (py >= screen_height) break;
        for (uint32_t j = 0; j < w; j++) {
            uint32_t px = x + j;
            if (px >= screen_width) break;

            uint32_t dest_color = back_buffer[py * screen_width + px];
            uint32_t r_dst = (dest_color >> 16) & 0xFF;
            uint32_t g_dst = (dest_color >> 8) & 0xFF;
            uint32_t b_dst = dest_color & 0xFF;

            uint32_t r_out = (r_src * alpha + r_dst * (255 - alpha)) / 255;
            uint32_t g_out = (g_src * alpha + g_dst * (255 - alpha)) / 255;
            uint32_t b_out = (b_src * alpha + b_dst * (255 - alpha)) / 255;

            back_buffer[py * screen_width + px] = (r_out << 16) | (g_out << 8) | b_out;
        }
    }
}

void draw_char(uint32_t x, uint32_t y, char c, uint32_t color) {
    if ((unsigned char)c >= 128) return;
    for (uint32_t i = 0; i < 16; i++) {
        unsigned char row = font_8x16[(unsigned char)c][i];
        for (uint32_t j = 0; j < 8; j++) {
            if (row & (0x80 >> j)) {
                draw_pixel(x + j, y + i, color);
            }
        }
    }
}

void draw_string(uint32_t x, uint32_t y, const char* str, uint32_t color) {
    uint32_t cx = x;
    while (*str) {
        if (*str == '\n') {
            y += 16;
            cx = x;
        } else {
            draw_char(cx, y, *str, color);
            cx += 8;
        }
        str++;
    }
}

void draw_circle(int32_t x0, int32_t y0, int32_t radius, uint32_t color) {
    int x = radius;
    int y = 0;
    int err = 0;
    while (x >= y) {
        for (int i = x0 - x; i <= x0 + x; i++) {
            draw_pixel(i, y0 + y, color);
            draw_pixel(i, y0 - y, color);
        }
        for (int i = x0 - y; i <= x0 + y; i++) {
            draw_pixel(i, y0 + x, color);
            draw_pixel(i, y0 - x, color);
        }
        y += 1;
        if (err <= 0) {
            err += 2 * y + 1;
        }
        if (err > 0) {
            x -= 1;
            err -= 2 * x + 1;
        }
    }
}

void draw_desktop_gradient(void) {
    for (uint32_t y = 0; y < screen_height; y++) {
        for (uint32_t x = 0; x < screen_width; x++) {
            // 1. Base blue gradient
            uint32_t factor_num = (x * 255 / screen_width + y * 255 / screen_height) / 2;
            uint8_t r = 10 + (factor_num * 30 / 255);
            uint8_t g = 90 - (factor_num * 60 / 255);
            uint8_t b = 180 - (factor_num * 100 / 255);
            
            // 2. Add Windows 7 curved light streams (Aero streams)
            int dx1 = (int)x - 200;
            int dy1 = (int)y - 700;
            int dist1 = dx1 * dx1 + dy1 * dy1;
            
            int dx2 = (int)x - 600;
            int dy2 = (int)y + 200;
            int dist2 = dx2 * dx2 + dy2 * dy2;
            
            int glow = 0;
            
            // Check proximity to radius 450
            int diff1 = dist1 - 450 * 450;
            if (diff1 < 0) diff1 = -diff1;
            if (diff1 < 30000) {
                glow += (30000 - diff1) / 500;
            }
            
            // Check proximity to radius 720
            int diff2 = dist2 - 720 * 720;
            if (diff2 < 0) diff2 = -diff2;
            if (diff2 < 40000) {
                glow += (40000 - diff2) / 600;
            }
            
            // Blend glow
            if (glow > 0) {
                if (glow > 80) glow = 80;
                g = (g * (255 - glow) + 180 * glow) / 255;
                b = (b * (255 - glow) + 240 * glow) / 255;
                r = (r * (255 - glow) + 120 * glow) / 255;
            }
            
            back_buffer[y * screen_width + x] = (r << 16) | (g << 8) | b;
        }
    }
}

void blit_buffer(void) {
    if (framebuffer) {
        memcpy(framebuffer, back_buffer, screen_width * screen_height * 4);
    }
}
