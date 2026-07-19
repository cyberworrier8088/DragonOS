#include "graphics.h"
#include "../libc/font_segoe.h"
#include "../libc/string.h"

#define MAX_WIDTH 1280
#define MAX_HEIGHT 1024

uint32_t* framebuffer = 0;
uint32_t  screen_width = 800;
uint32_t  screen_height = 600;
uint32_t  screen_pitch = 800 * 4;

/* Dynamic resolution-supporting double buffer allocation */
static uint32_t back_buffer_storage[MAX_WIDTH * MAX_HEIGHT];
uint32_t* back_buffer = back_buffer_storage;

void graphics_init(uint64_t phys_addr, uint32_t width, uint32_t height, uint32_t pitch) {
    framebuffer = (uint32_t*)phys_addr;

    /* Clip resolution to prevent buffer overflow on high-res displays */
    screen_width = (width > MAX_WIDTH) ? MAX_WIDTH : width;
    screen_height = (height > MAX_HEIGHT) ? MAX_HEIGHT : height;
    screen_pitch = pitch;

    memset(back_buffer, 0, screen_width * screen_height * 4);
}

/* ========== Core Drawing Primitives ========== */

void draw_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x >= screen_width || y >= screen_height) return;
    back_buffer[y * screen_width + x] = color;
}

void draw_pixel_alpha(uint32_t x, uint32_t y, uint32_t color, uint8_t alpha) {
    if (x >= screen_width || y >= screen_height) return;
    uint32_t r_src = (color >> 16) & 0xFF;
    uint32_t g_src = (color >> 8) & 0xFF;
    uint32_t b_src = color & 0xFF;
    uint32_t dest = back_buffer[y * screen_width + x];
    uint32_t r_dst = (dest >> 16) & 0xFF;
    uint32_t g_dst = (dest >> 8) & 0xFF;
    uint32_t b_dst = dest & 0xFF;
    uint32_t r = (r_src * alpha + r_dst * (255 - alpha)) / 255;
    uint32_t g = (g_src * alpha + g_dst * (255 - alpha)) / 255;
    uint32_t b = (b_src * alpha + b_dst * (255 - alpha)) / 255;
    back_buffer[y * screen_width + x] = (r << 16) | (g << 8) | b;
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

/* ========== Modern Fluent Design Primitives ========== */

void draw_hline(uint32_t x, uint32_t y, uint32_t length, uint32_t color) {
    if (y >= screen_height) return;
    for (uint32_t i = 0; i < length; i++) {
        uint32_t px = x + i;
        if (px >= screen_width) break;
        back_buffer[y * screen_width + px] = color;
    }
}

void draw_vline(uint32_t x, uint32_t y, uint32_t length, uint32_t color) {
    if (x >= screen_width) return;
    for (uint32_t i = 0; i < length; i++) {
        uint32_t py = y + i;
        if (py >= screen_height) break;
        back_buffer[py * screen_width + x] = color;
    }
}

void draw_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color) {
    int dx = x1 - x0;
    int dy = y1 - y0;
    int sx = (dx > 0) ? 1 : -1;
    int sy = (dy > 0) ? 1 : -1;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;

    int err = dx - dy;

    while (1) {
        draw_pixel((uint32_t)x0, (uint32_t)y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

/* Helper: check if point (px,py) is inside a rounded rect region */
static int point_in_rounded_rect(int px, int py, int rx, int ry, int rw, int rh, int rad) {
    /* Inner rectangle (no rounding needed) */
    if (px >= rx + rad && px < rx + rw - rad && py >= ry && py < ry + rh) return 1;
    if (px >= rx && px < rx + rw && py >= ry + rad && py < ry + rh - rad) return 1;

    /* Check four corners with circle distance */
    int corners[4][2] = {
        {rx + rad, ry + rad},               /* top-left */
        {rx + rw - rad - 1, ry + rad},       /* top-right */
        {rx + rad, ry + rh - rad - 1},       /* bottom-left */
        {rx + rw - rad - 1, ry + rh - rad - 1} /* bottom-right */
    };
    for (int c = 0; c < 4; c++) {
        int dx = px - corners[c][0];
        int dy = py - corners[c][1];
        if (dx * dx + dy * dy <= rad * rad) return 1;
    }
    return 0;
}

void draw_rounded_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t radius, uint32_t color) {
    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;

    for (uint32_t py = 0; py < h; py++) {
        uint32_t sy = y + py;
        if (sy >= screen_height) break;
        for (uint32_t px = 0; px < w; px++) {
            uint32_t sx = x + px;
            if (sx >= screen_width) break;
            if (point_in_rounded_rect(sx, sy, x, y, w, h, radius)) {
                back_buffer[sy * screen_width + sx] = color;
            }
        }
    }
}

void draw_rounded_rect_outline(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t radius, uint32_t color) {
    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;

    for (uint32_t py = 0; py < h; py++) {
        uint32_t sy = y + py;
        if (sy >= screen_height) break;
        for (uint32_t px = 0; px < w; px++) {
            uint32_t sx = x + px;
            if (sx >= screen_width) break;
            int inside = point_in_rounded_rect(sx, sy, x, y, w, h, radius);
            int inside_shrunk = point_in_rounded_rect(sx, sy, x + 1, y + 1, w - 2, h - 2, radius > 0 ? radius - 1 : 0);
            if (inside && !inside_shrunk) {
                back_buffer[sy * screen_width + sx] = color;
            }
        }
    }
}

void draw_rounded_rect_translucent(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t radius, uint32_t color, uint8_t alpha) {
    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;
    uint32_t r_src = (color >> 16) & 0xFF;
    uint32_t g_src = (color >> 8) & 0xFF;
    uint32_t b_src = color & 0xFF;

    for (uint32_t py = 0; py < h; py++) {
        uint32_t sy = y + py;
        if (sy >= screen_height) break;
        for (uint32_t px = 0; px < w; px++) {
            uint32_t sx = x + px;
            if (sx >= screen_width) break;
            if (point_in_rounded_rect(sx, sy, x, y, w, h, radius)) {
                uint32_t dest = back_buffer[sy * screen_width + sx];
                uint32_t r_dst = (dest >> 16) & 0xFF;
                uint32_t g_dst = (dest >> 8) & 0xFF;
                uint32_t b_dst = dest & 0xFF;
                uint32_t r = (r_src * alpha + r_dst * (255 - alpha)) / 255;
                uint32_t g = (g_src * alpha + g_dst * (255 - alpha)) / 255;
                uint32_t b = (b_src * alpha + b_dst * (255 - alpha)) / 255;
                back_buffer[sy * screen_width + sx] = (r << 16) | (g << 8) | b;
            }
        }
    }
}

void draw_gradient_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color_top, uint32_t color_bottom) {
    uint32_t rt = (color_top >> 16) & 0xFF, gt = (color_top >> 8) & 0xFF, bt = color_top & 0xFF;
    uint32_t rb = (color_bottom >> 16) & 0xFF, gb = (color_bottom >> 8) & 0xFF, bb = color_bottom & 0xFF;

    for (uint32_t i = 0; i < h; i++) {
        uint32_t py = y + i;
        if (py >= screen_height) break;
        uint32_t r = rt + (rb - rt) * i / h;
        uint32_t g = gt + (gb - gt) * i / h;
        uint32_t b = bt + (bb - bt) * i / h;
        uint32_t c = (r << 16) | (g << 8) | b;
        for (uint32_t j = 0; j < w; j++) {
            uint32_t px = x + j;
            if (px >= screen_width) break;
            back_buffer[py * screen_width + px] = c;
        }
    }
}

void draw_circle(int32_t x0, int32_t y0, int32_t radius, uint32_t color) {
    int x = radius, y = 0, err = 0;
    while (x >= y) {
        draw_pixel(x0 + x, y0 + y, color);
        draw_pixel(x0 - x, y0 + y, color);
        draw_pixel(x0 + x, y0 - y, color);
        draw_pixel(x0 - x, y0 - y, color);
        draw_pixel(x0 + y, y0 + x, color);
        draw_pixel(x0 - y, y0 + x, color);
        draw_pixel(x0 + y, y0 - x, color);
        draw_pixel(x0 - y, y0 - x, color);
        y++;
        if (err <= 0) err += 2 * y + 1;
        if (err > 0) { x--; err -= 2 * x + 1; }
    }
}

void draw_circle_filled(int32_t x0, int32_t y0, int32_t radius, uint32_t color) {
    int x = radius, y = 0, err = 0;
    while (x >= y) {
        for (int i = x0 - x; i <= x0 + x; i++) {
            draw_pixel(i, y0 + y, color);
            draw_pixel(i, y0 - y, color);
        }
        for (int i = x0 - y; i <= x0 + y; i++) {
            draw_pixel(i, y0 + x, color);
            draw_pixel(i, y0 - x, color);
        }
        y++;
        if (err <= 0) err += 2 * y + 1;
        if (err > 0) { x--; err -= 2 * x + 1; }
    }
}

/* ========== Text Rendering with Modern 10x18 Segoe Font ========== */

void draw_char(uint32_t x, uint32_t y, char c, uint32_t color) {
    if ((unsigned char)c >= 128) return;
    for (uint32_t row = 0; row < 18; row++) {
        unsigned short bits = font_segoe[(unsigned char)c][row];
        for (uint32_t col = 0; col < 10; col++) {
            if (bits & (0x8000 >> col)) {
                draw_pixel(x + col, y + row, color);
            }
        }
    }
}

void draw_string(uint32_t x, uint32_t y, const char* str, uint32_t color) {
    uint32_t cx = x;
    while (*str) {
        if (*str == '\n') {
            y += FONT_HEIGHT;
            cx = x;
        } else {
            draw_char(cx, y, *str, color);
            cx += FONT_WIDTH;
        }
        str++;
    }
}

/* ========== Windows 11 Desktop Wallpaper ========== */

void draw_desktop_gradient(void) {
    /* Windows 11 "Bloom" wallpaper: dark navy edges, centered bright blue bloom */
    int cx = (int)screen_width / 2;
    int cy = (int)screen_height / 2;
    int max_dist_sq = cx * cx + cy * cy;

    for (uint32_t y = 0; y < screen_height; y++) {
        for (uint32_t x = 0; x < screen_width; x++) {
            int dx = (int)x - cx;
            int dy = (int)y - cy;
            int dist_sq = dx * dx + dy * dy;

            /* Normalized distance from center (0 at center, 255 at corners) */
            int norm = dist_sq * 255 / max_dist_sq;
            if (norm > 255) norm = 255;
            int inv = 255 - norm; /* 255 at center, 0 at corners */

            /* Edge color: deep dark navy #0a0e27 */
            /* Center bloom: rich blue #2563eb with hints of purple/teal */
            int r = 10 + (inv * 30) / 255;
            int g = 14 + (inv * 85) / 255;
            int b = 39 + (inv * 196) / 255;

            /* Add secondary bloom layers for a multi-color effect */
            /* Teal bloom offset above center */
            int tdx = (int)x - cx - 80;
            int tdy = (int)y - cy + 60;
            int tdist = tdx * tdx + tdy * tdy;
            int tmax = (screen_width / 3) * (screen_width / 3);
            if (tdist < tmax) {
                int tinv = 255 - (tdist * 255 / tmax);
                g = g + (tinv * 40) / 255;
                b = b + (tinv * 20) / 255;
                if (g > 255) g = 255;
                if (b > 255) b = 255;
            }

            /* Purple bloom offset below center-right */
            int pdx = (int)x - cx + 100;
            int pdy = (int)y - cy - 80;
            int pdist = pdx * pdx + pdy * pdy;
            int pmax = (screen_width / 4) * (screen_width / 4);
            if (pdist < pmax) {
                int pinv = 255 - (pdist * 255 / pmax);
                r = r + (pinv * 50) / 255;
                b = b + (pinv * 30) / 255;
                if (r > 255) r = 255;
                if (b > 255) b = 255;
            }

            back_buffer[y * screen_width + x] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }
}

/* ========== Buffer Blit ========== */

void blit_buffer(void) {
    if (framebuffer) {
        /* Row-by-row copy respecting pitch differences */
        uint32_t row_bytes = screen_width * 4;
        uint32_t pitch_pixels = screen_pitch / 4;
        for (uint32_t y = 0; y < screen_height; y++) {
            memcpy(&framebuffer[y * pitch_pixels], &back_buffer[y * screen_width], row_bytes);
        }
    }
}
