#include "graphics.h"
#include "../libc/font_segoe.h"
#include "../libc/string.h"
#include "../mm/kheap.h"
#include "serial.h"
#include "fs/vfs.h"

#define MAX_WIDTH 1280
#define MAX_HEIGHT 1024

uint32_t* framebuffer = 0;
uint32_t  screen_width = 800;
uint32_t  screen_height = 600;
uint32_t  screen_pitch = 800 * 4;

/* Dynamic resolution-supporting double buffer allocation */
static uint32_t back_buffer_storage[MAX_WIDTH * MAX_HEIGHT];
uint32_t* back_buffer = back_buffer_storage;

static uint8_t hdr_lut[256];

void graphics_init(uint64_t phys_addr, uint32_t width, uint32_t height, uint32_t pitch) {
    framebuffer = (uint32_t*)phys_addr;

    /* Clip resolution to prevent buffer overflow on high-res displays */
    screen_width = (width > MAX_WIDTH) ? MAX_WIDTH : width;
    screen_height = (height > MAX_HEIGHT) ? MAX_HEIGHT : height;
    screen_pitch = pitch;

    /* Initialize HDR ACES Filmic approximation LUT */
    for (int i = 0; i < 256; i++) {
        int val = (i * 12) / 10; // Boost exposure by 1.2x
        if (val > 255) val = 255;
        // Cubic sigmoid S-curve for high dynamic contrast
        hdr_lut[i] = (val * val * (765 - 2 * val)) / 65025;
    }

    memset(back_buffer, 0, screen_width * screen_height * 4);
}

static int clip_enabled = 0;
static uint32_t clip_x1 = 0;
static uint32_t clip_y1 = 0;
static uint32_t clip_x2 = 0;
static uint32_t clip_y2 = 0;

void graphics_set_clip(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    clip_enabled = 1;
    clip_x1 = x;
    clip_y1 = y;
    clip_x2 = x + w;
    clip_y2 = y + h;
}

void graphics_clear_clip(void) {
    clip_enabled = 0;
}

/* ========== Core Drawing Primitives ========== */

void draw_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (clip_enabled) {
        if (x < clip_x1 || x >= clip_x2 || y < clip_y1 || y >= clip_y2) return;
    }
    if (x >= screen_width || y >= screen_height) return;
    back_buffer[y * screen_width + x] = color;
}

void draw_pixel_alpha(uint32_t x, uint32_t y, uint32_t color, uint8_t alpha) {
    if (clip_enabled) {
        if (x < clip_x1 || x >= clip_x2 || y < clip_y1 || y >= clip_y2) return;
    }
    if (x >= screen_width || y >= screen_height) return;
    uint32_t r_src = (color >> 16) & 0xFF;
    uint32_t g_src = (color >> 8) & 0xFF;
    uint32_t b_src = color & 0xFF;
    uint32_t dest = back_buffer[y * screen_width + x];
    uint32_t r_dst = (dest >> 16) & 0xFF;
    uint32_t g_dst = (dest >> 8) & 0xFF;
    uint32_t b_dst = dest & 0xFF;
    
    // Exact divide-by-255 without a real division: (x + 1 + (x >> 8)) >> 8
    // equals round(x / 255) for all x in [0, 255*255]. A plain >> 8 (divide by
    // 256) is *not* equivalent -- it under-darkens every blend and, at
    // alpha=255, fails to reproduce the source color at all (255 -> 254),
    // which showed up as translucent overlays reading slightly too dark.
    uint32_t inv_alpha = 255 - alpha;
    uint32_t r_mix = r_src * alpha + r_dst * inv_alpha;
    uint32_t g_mix = g_src * alpha + g_dst * inv_alpha;
    uint32_t b_mix = b_src * alpha + b_dst * inv_alpha;
    uint32_t r = (r_mix + 1 + (r_mix >> 8)) >> 8;
    uint32_t g = (g_mix + 1 + (g_mix >> 8)) >> 8;
    uint32_t b = (b_mix + 1 + (b_mix >> 8)) >> 8;
    
    back_buffer[y * screen_width + x] = (r << 16) | (g << 8) | b;
}

void draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (clip_enabled) {
        if (x >= clip_x2 || y >= clip_y2) return;
        if (x < clip_x1) {
            if (w <= clip_x1 - x) return;
            w -= (clip_x1 - x);
            x = clip_x1;
        }
        if (y < clip_y1) {
            if (h <= clip_y1 - y) return;
            h -= (clip_y1 - y);
            y = clip_y1;
        }
        if (x + w > clip_x2) w = clip_x2 - x;
        if (y + h > clip_y2) h = clip_y2 - y;
    }
    if (x >= screen_width || y >= screen_height) return;
    if (x + w > screen_width) w = screen_width - x;
    if (y + h > screen_height) h = screen_height - y;
    
    for (uint32_t i = 0; i < h; i++) {
        uint32_t* dest = &back_buffer[(y + i) * screen_width + x];
        uint32_t count = w;
        __asm__ volatile (
            "rep stosl"
            : "+D"(dest), "+c"(count)
            : "a"(color)
            : "memory"
        );
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

/* Helper: check if point (px,py) is inside a top-rounded rect region */
static int point_in_top_rounded_rect(int px, int py, int rx, int ry, int rw, int rh, int rad) {
    /* If it's in the bottom part, it's always inside the rect (square corners) */
    if (py >= ry + rh - rad) {
        return (px >= rx && px < rx + rw && py < ry + rh);
    }
    /* Inner rectangle (no rounding needed for top part) */
    if (px >= rx + rad && px < rx + rw - rad && py >= ry && py < ry + rh) return 1;
    if (px >= rx && px < rx + rw && py >= ry + rad && py < ry + rh) return 1;

    /* Check two top corners with circle distance */
    int corners[2][2] = {
        {rx + rad, ry + rad},               /* top-left */
        {rx + rw - rad - 1, ry + rad}        /* top-right */
    };
    for (int c = 0; c < 2; c++) {
        int dx = px - corners[c][0];
        int dy = py - corners[c][1];
        if (dx * dx + dy * dy <= rad * rad) return 1;
    }
    return 0;
}

void draw_rounded_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t radius, uint32_t color) {
    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;

    uint32_t start_y = y;
    uint32_t end_y = y + h;
    uint32_t start_x = x;
    uint32_t end_x = x + w;

    if (clip_enabled) {
        if (start_y < clip_y1) start_y = clip_y1;
        if (end_y > clip_y2) end_y = clip_y2;
        if (start_x < clip_x1) start_x = clip_x1;
        if (end_x > clip_x2) end_x = clip_x2;
    }

    for (uint32_t sy = start_y; sy < end_y; sy++) {
        if (sy >= screen_height) break;
        for (uint32_t sx = start_x; sx < end_x; sx++) {
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

    uint32_t start_y = y;
    uint32_t end_y = y + h;
    uint32_t start_x = x;
    uint32_t end_x = x + w;

    if (clip_enabled) {
        if (start_y < clip_y1) start_y = clip_y1;
        if (end_y > clip_y2) end_y = clip_y2;
        if (start_x < clip_x1) start_x = clip_x1;
        if (end_x > clip_x2) end_x = clip_x2;
    }

    for (uint32_t sy = start_y; sy < end_y; sy++) {
        if (sy >= screen_height) break;
        for (uint32_t sx = start_x; sx < end_x; sx++) {
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

    uint32_t start_y = y;
    uint32_t end_y = y + h;
    uint32_t start_x = x;
    uint32_t end_x = x + w;

    if (clip_enabled) {
        if (start_y < clip_y1) start_y = clip_y1;
        if (end_y > clip_y2) end_y = clip_y2;
        if (start_x < clip_x1) start_x = clip_x1;
        if (end_x > clip_x2) end_x = clip_x2;
    }

    for (uint32_t sy = start_y; sy < end_y; sy++) {
        if (sy >= screen_height) break;
        for (uint32_t sx = start_x; sx < end_x; sx++) {
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

void draw_top_rounded_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t radius, uint32_t color) {
    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;

    uint32_t start_y = y;
    uint32_t end_y = y + h;
    uint32_t start_x = x;
    uint32_t end_x = x + w;

    if (clip_enabled) {
        if (start_y < clip_y1) start_y = clip_y1;
        if (end_y > clip_y2) end_y = clip_y2;
        if (start_x < clip_x1) start_x = clip_x1;
        if (end_x > clip_x2) end_x = clip_x2;
    }

    for (uint32_t sy = start_y; sy < end_y; sy++) {
        if (sy >= screen_height) break;
        for (uint32_t sx = start_x; sx < end_x; sx++) {
            if (sx >= screen_width) break;
            if (point_in_top_rounded_rect(sx, sy, x, y, w, h, radius)) {
                back_buffer[sy * screen_width + sx] = color;
            }
        }
    }
}

void draw_top_rounded_rect_outline(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t radius, uint32_t color) {
    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;

    uint32_t start_y = y;
    uint32_t end_y = y + h;
    uint32_t start_x = x;
    uint32_t end_x = x + w;

    if (clip_enabled) {
        if (start_y < clip_y1) start_y = clip_y1;
        if (end_y > clip_y2) end_y = clip_y2;
        if (start_x < clip_x1) start_x = clip_x1;
        if (end_x > clip_x2) end_x = clip_x2;
    }

    for (uint32_t sy = start_y; sy < end_y; sy++) {
        if (sy >= screen_height) break;
        for (uint32_t sx = start_x; sx < end_x; sx++) {
            if (sx >= screen_width) break;
            int inside = point_in_top_rounded_rect(sx, sy, x, y, w, h, radius);
            int inside_shrunk = point_in_top_rounded_rect(sx, sy, x + 1, y + 1, w - 2, h - 2, radius > 0 ? radius - 1 : 0);
            if (inside && !inside_shrunk) {
                back_buffer[sy * screen_width + sx] = color;
            }
        }
    }
}

void draw_top_rounded_rect_translucent(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t radius, uint32_t color, uint8_t alpha) {
    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;
    uint32_t r_src = (color >> 16) & 0xFF;
    uint32_t g_src = (color >> 8) & 0xFF;
    uint32_t b_src = color & 0xFF;

    uint32_t start_y = y;
    uint32_t end_y = y + h;
    uint32_t start_x = x;
    uint32_t end_x = x + w;

    if (clip_enabled) {
        if (start_y < clip_y1) start_y = clip_y1;
        if (end_y > clip_y2) end_y = clip_y2;
        if (start_x < clip_x1) start_x = clip_x1;
        if (end_x > clip_x2) end_x = clip_x2;
    }

    for (uint32_t sy = start_y; sy < end_y; sy++) {
        if (sy >= screen_height) break;
        for (uint32_t sx = start_x; sx < end_x; sx++) {
            if (sx >= screen_width) break;
            if (point_in_top_rounded_rect(sx, sy, x, y, w, h, radius)) {
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

static uint32_t wallpaper_buffer[MAX_WIDTH * MAX_HEIGHT];
static int wallpaper_rendered = 0;

void draw_desktop_gradient(void) {
    if (wallpaper_rendered) {
        memcpy(back_buffer, wallpaper_buffer, screen_width * screen_height * 4);
        return;
    }

    int loaded_wallpaper = 0;
    
    // Try to load the raw BMP wallpaper if the resolution matches
    if (screen_width == 1024 && screen_height == 768) {
        vfs_node_t* bmp_node = 0;
        int count = vfs_get_count();
        for (int i = 0; i < count; i++) {
            vfs_node_t* node = vfs_get_node(i);
            if (node && (strcmp(node->name, "/boot/wallpaper.bmp") == 0 ||
                         strcmp(node->name, "wallpaper.bmp") == 0 ||
                         strcmp(node->name, "boot/wallpaper.bmp") == 0)) {
                bmp_node = node;
                break;
            }
        }
        
        if (bmp_node && bmp_node->private_data && bmp_node->size >= 54) {
            uint8_t* file_data = (uint8_t*)bmp_node->private_data;
            if (file_data[0] == 'B' && file_data[1] == 'M') {
                loaded_wallpaper = 1;
                uint8_t* pixel_data = file_data + 54;
                for (int y = 767; y >= 0; y--) {
                    uint8_t* row = pixel_data + (767 - y) * 1024 * 3;
                    for (int x = 0; x < 1024; x++) {
                        uint8_t b = row[x * 3];
                        uint8_t g = row[x * 3 + 1];
                        uint8_t r = row[x * 3 + 2];
                        wallpaper_buffer[y * 1024 + x] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
                    }
                }
            }
        }
    }

    if (loaded_wallpaper) {
        print_serial("[graphics] Wallpaper loaded successfully from VFS!\n");
        wallpaper_rendered = 1;
        memcpy(back_buffer, wallpaper_buffer, screen_width * screen_height * 4);
        return;
    }

    print_serial("[graphics] BMP wallpaper skipped or failed. Drawing fallback gradient.\n");
    char res_buf[64];
    char num_buf[16];
    strcpy(res_buf, "Resolution: ");
    int_to_ascii(screen_width, num_buf);
    strcat(res_buf, num_buf);
    strcat(res_buf, "x");
    int_to_ascii(screen_height, num_buf);
    strcat(res_buf, num_buf);
    strcat(res_buf, "\n");
    print_serial(res_buf);

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

            /* Draw stylized glowing abstract Dragon emblem in the center */
            int logo_x = dx;
            int logo_y = dy;
            
            // Central diamond: |x| + |y| < 24
            int abs_x = (logo_x < 0) ? -logo_x : logo_x;
            int abs_y = (logo_y < 0) ? -logo_y : logo_y;
            if (abs_x + abs_y < 24) {
                int intensity = (24 - (abs_x + abs_y)) * 255 / 24;
                r = r + intensity * 150 / 255;
                g = g + intensity * 40 / 255;
                b = b + intensity * 240 / 255;
                if (r > 255) r = 255;
                if (g > 255) g = 255;
                if (b > 255) b = 255;
            }
            
            // Wing curves: sweeping parabolic wings extending left and right
            if (abs_x > 15 && abs_x < 140) {
                int wing_center_y = (abs_x * abs_x) * 15 / 1000 - 15;
                int dy_wing = logo_y - wing_center_y;
                int abs_dy_wing = (dy_wing < 0) ? -dy_wing : dy_wing;
                if (abs_dy_wing < 8) {
                    int intensity = (8 - abs_dy_wing) * 255 / 8;
                    // Fade out towards tips
                    intensity = intensity * (140 - abs_x) / 125;
                    r = r + intensity * 40 / 255;
                    g = g + intensity * 160 / 255;
                    b = b + intensity * 255 / 255;
                    if (r > 255) r = 255;
                    if (g > 255) g = 255;
                    if (b > 255) b = 255;
                }
            }
            
            // Upper head/horns crest
            if (abs_x < 45) {
                int horn_center_y = -(abs_x * abs_x) * 35 / 1000 - 30;
                int dy_horn = logo_y - horn_center_y;
                int abs_dy_horn = (dy_horn < 0) ? -dy_horn : dy_horn;
                if (abs_dy_horn < 5) {
                    int intensity = (5 - abs_dy_horn) * 255 / 5;
                    intensity = intensity * (45 - abs_x) / 45;
                    r = r + intensity * 180 / 255;
                    g = g + intensity * 30 / 255;
                    b = b + intensity * 255 / 255;
                    if (r > 255) r = 255;
                    if (g > 255) g = 255;
                    if (b > 255) b = 255;
                }
            }
            
            // Tail hook below
            if (logo_x > -35 && logo_x < 35) {
                int tail_center_y = (logo_x * logo_x) * 45 / 1000 + 35;
                int dy_tail = logo_y - tail_center_y;
                int abs_dy_tail = (dy_tail < 0) ? -dy_tail : dy_tail;
                if (abs_dy_tail < 5) {
                    int intensity = (5 - abs_dy_tail) * 255 / 5;
                    r = r + intensity * 20 / 255;
                    g = g + intensity * 200 / 255;
                    b = b + intensity * 220 / 255;
                    if (r > 255) r = 255;
                    if (g > 255) g = 255;
                    if (b > 255) b = 255;
                }
            }

            wallpaper_buffer[y * screen_width + x] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }
    
    wallpaper_rendered = 1;
    memcpy(back_buffer, wallpaper_buffer, screen_width * screen_height * 4);
}

/* ========== Buffer Blit ========== */

void blit_buffer(void) {
    if (framebuffer) {
        uint32_t pitch_pixels = screen_pitch / 4;
        if (pitch_pixels == screen_width) {
            memcpy(framebuffer, back_buffer, screen_width * screen_height * 4);
        } else {
            for (uint32_t y = 0; y < screen_height; y++) {
                memcpy(&framebuffer[y * pitch_pixels], &back_buffer[y * screen_width], screen_width * 4);
            }
        }
    }
}
