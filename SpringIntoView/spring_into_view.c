#include "spring_into_view.h"
#include <stddef.h>
#include "../pmm.h" // For pmm_alloc
#include "../string.h" // For memcpy, memset, strlen
#include "../libs/stb_truetype.h"

// Embedded font data
#include "font.h"

static uint32_t* fb = 0;
static uint32_t fb_width = 0, fb_height = 0, fb_pitch = 0, fb_bpp = 0;
static stbtt_fontinfo font_info;
static bool font_initialized = false;

static inline int siv_abs(int x) { return x < 0 ? -x : x; }

// === NEW: helpers for RGB565 format ===
static inline uint16_t rgb888_to_565(uint32_t color) {
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8)  & 0xFF;
    uint8_t b =  color        & 0xFF;
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static inline uint32_t rgb565_to_888(uint16_t c) {
    uint8_t r = (c >> 11) & 0x1F;
    uint8_t g = (c >> 5)  & 0x3F;
    uint8_t b =  c        & 0x1F;
    return ((uint32_t)(r << 3) << 16) | ((uint32_t)(g << 2) << 8) | (uint32_t)(b << 3);
}

void siv_init(uint32_t width, uint32_t height, uint32_t pitch, uint32_t bpp, void* framebuffer) {
    fb = (uint32_t*)framebuffer;
    fb_width = width;
    fb_height = height;
    fb_pitch = pitch;
    fb_bpp = bpp;
}

bool siv_init_font(void) {
    if (stbtt_InitFont(&font_info, RobotoMono_Regular_ttf, stbtt_GetFontOffsetForIndex(RobotoMono_Regular_ttf, 0))) {
        font_initialized = true;
        return true;
    }
    return false;
}

void siv_put_pixel(int x, int y, uint32_t color) {
    if (x < 0 || y < 0 || x >= (int)fb_width || y >= (int)fb_height) return;

    uint8_t* fb_byte_ptr = (uint8_t*)fb;
    uint32_t offset = y * fb_pitch + x * (fb_bpp / 8);

    if (fb_bpp == 32) {
        *((uint32_t*)(fb_byte_ptr + offset)) = color;
    } else if (fb_bpp == 24) {
        // 24-bpp: store as BGR
        fb_byte_ptr[offset + 0] = color & 0xFF;        // B
        fb_byte_ptr[offset + 1] = (color >> 8) & 0xFF; // G
        fb_byte_ptr[offset + 2] = (color >> 16) & 0xFF; // R
    } else if (fb_bpp == 16) {
        *((uint16_t*)(fb_byte_ptr + offset)) = rgb888_to_565(color);
    }
}

uint32_t siv_get_pixel(int x, int y) {
    if (x < 0 || y < 0 || x >= (int)fb_width || y >= (int)fb_height) return 0;

    uint8_t* fb_byte_ptr = (uint8_t*)fb;
    uint32_t offset = y * fb_pitch + x * (fb_bpp / 8);

    if (fb_bpp == 32) {
        return *((uint32_t*)(fb_byte_ptr + offset));
    } else if (fb_bpp == 24) {
        uint8_t b = fb_byte_ptr[offset + 0];
        uint8_t g = fb_byte_ptr[offset + 1];
        uint8_t r = fb_byte_ptr[offset + 2];
        return (r << 16) | (g << 8) | b;
    } else if (fb_bpp == 16) {
        uint16_t c = *((uint16_t*)(fb_byte_ptr + offset));
        return rgb565_to_888(c);
    }
    return 0;
}

// Alpha blend a pixel
void siv_put_pixel_alpha(int x, int y, uint32_t color, uint8_t alpha) {
    if (x < 0 || y < 0 || x >= (int)fb_width || y >= (int)fb_height) return;
    if (alpha == 255) {
        siv_put_pixel(x, y, color);
        return;
    }
    if (alpha == 0) return;

    uint8_t* fb_byte_ptr = (uint8_t*)fb;
    uint32_t offset = y * fb_pitch + x * (fb_bpp / 8);

    uint32_t dst;
    if (fb_bpp == 32) {
        dst = *((uint32_t*)(fb_byte_ptr + offset));
    } else if (fb_bpp == 24) {
        uint8_t b = fb_byte_ptr[offset + 0];
        uint8_t g = fb_byte_ptr[offset + 1];
        uint8_t r = fb_byte_ptr[offset + 2];
        dst = (r << 16) | (g << 8) | b;
    } else { // 16-bpp
        uint16_t c = *((uint16_t*)(fb_byte_ptr + offset));
        dst = rgb565_to_888(c);
    }

    uint8_t dr = (dst >> 16) & 0xFF;
    uint8_t dg = (dst >> 8) & 0xFF;
    uint8_t db = dst & 0xFF;

    uint8_t sr = (color >> 16) & 0xFF;
    uint8_t sg = (color >> 8) & 0xFF;
    uint8_t sb = color & 0xFF;

    uint8_t r = ((sr * alpha) + (dr * (255 - alpha))) / 255;
    uint8_t g = ((sg * alpha) + (dg * (255 - alpha))) / 255;
    uint8_t b = ((sb * alpha) + (db * (255 - alpha))) / 255;

    uint32_t blended = (r << 16) | (g << 8) | b;
    if (fb_bpp == 32) {
        *((uint32_t*)(fb_byte_ptr + offset)) = blended;
    } else if (fb_bpp == 24) {
        fb_byte_ptr[offset + 0] = blended & 0xFF;
        fb_byte_ptr[offset + 1] = (blended >> 8) & 0xFF;
        fb_byte_ptr[offset + 2] = (blended >> 16) & 0xFF;
    } else {
        *((uint16_t*)(fb_byte_ptr + offset)) = rgb888_to_565(blended);
    }
}

void siv_get_screen_size(uint32_t* width, uint32_t* height) {
    *width = fb_width;
    *height = fb_height;
}

void siv_clear(uint32_t color) {
    if (fb_bpp == 32 && fb_pitch == fb_width * 4) {
        /* Fill using 32-bit writes so each pixel gets the intended colour.
           Using memset with a multi-byte value only repeats the lowest byte
           (0xXXXXXX**YY** → YYYY...). That produced the random artefacts you saw. */
        uint32_t* ptr = (uint32_t*)fb;
        size_t total = (size_t)fb_width * (size_t)fb_height;
        for (size_t i = 0; i < total; ++i) {
            ptr[i] = color;
        }
    } else {
        siv_draw_rect(0, 0, fb_width, fb_height, color, true);
    }
}

void siv_draw_cursor(int x, int y, uint32_t color) {
    // A simple cursor shape
    const char* cursor_map[] = {
        "X           ",
        "XX          ",
        "XXX         ",
        "XXXX        ",
        "XXXXX       ",
        "XXXXXX      ",
        "XXX.XX      ",
        "XX..XX      ",
        "X...XX      ",
        " ...XX      ",
        "  ..X       ",
        "   .        ",
    };
    
    uint32_t outline_color = 0x000000;

    for (int i = 0; i < 12; ++i) {
        for (int j = 0; j < 12; ++j) {
            if (cursor_map[i][j] == 'X') {
                siv_put_pixel(x + j, y + i, color);
            } else if (cursor_map[i][j] == '.') {
                 siv_put_pixel(x + j, y + i, outline_color);
            }
        }
    }
}

void siv_draw_line(int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = siv_abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -siv_abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;
    while (1) {
        siv_put_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void siv_draw_rect(int x, int y, int w, int h, uint32_t color, bool filled) {
    // Clip rectangle to screen bounds
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)fb_width) { w = fb_width - x; }
    if (y + h > (int)fb_height) { h = fb_height - y; }
    if (w <= 0 || h <= 0) return;

    if (filled) {
        uint8_t* row_start = (uint8_t*)fb + y * fb_pitch + x * (fb_bpp / 8);
        if (fb_bpp == 32) {
            // Optimized for 32bpp
            uint32_t* row_ptr = (uint32_t*)row_start;
            for (int i = 0; i < h; ++i) {
                for (int j = 0; j < w; ++j) {
                    row_ptr[j] = color;
                }
                row_ptr = (uint32_t*)((uint8_t*)row_ptr + fb_pitch);
            }
        } else {
             for (int i = 0; i < h; ++i) {
                for (int j = 0; j < w; ++j) {
                    siv_put_pixel(x + j, y + i, color);
                }
            }
        }
    } else {
        siv_draw_line(x, y, x + w - 1, y, color);
        siv_draw_line(x, y, x, y + h - 1, color);
        siv_draw_line(x + w - 1, y, x + w - 1, y + h - 1, color);
        siv_draw_line(x, y + h - 1, x + w - 1, y + h - 1, color);
    }
}

void siv_draw_circle(int xc, int yc, int r, uint32_t color, bool filled) {
    if (r <= 0) return;

    if (filled) {
        int x0 = 0;
        int y0 = r;
        int d = 3 - 2 * r;
        while (y0 >= x0) {
            siv_draw_line(xc - x0, yc - y0, xc + x0, yc - y0, color);
            siv_draw_line(xc - y0, yc - x0, xc + y0, yc - x0, color);
            siv_draw_line(xc - x0, yc + y0, xc + x0, yc + y0, color);
            siv_draw_line(xc - y0, yc + x0, xc + y0, yc + x0, color);
            if (d < 0) {
                d = d + 4 * x0 + 6;
            } else {
                d = d + 4 * (x0 - y0) + 10;
                y0--;
            }
            x0++;
        }
    } else {
        int x = r, y = 0;
        int err = 0;

        while (x >= y) {
            siv_put_pixel(xc + x, yc + y, color);
            siv_put_pixel(xc + y, yc + x, color);
            siv_put_pixel(xc - y, yc + x, color);
            siv_put_pixel(xc - x, yc + y, color);
            siv_put_pixel(xc - x, yc - y, color);
            siv_put_pixel(xc - y, yc - x, color);
            siv_put_pixel(xc + y, yc - x, color);
            siv_put_pixel(xc + x, yc - y, color);

            if (err <= 0) {
                y += 1;
                err += 2 * y + 1;
            }
            if (err > 0) {
                x -= 1;
                err -= 2 * x + 1;
            }
        }
    }
}

void siv_draw_char(int x, int y, char c, float scale, uint32_t color) {
    if (!font_initialized) return;

    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&font_info, &ascent, &descent, &lineGap);
    float font_scale = stbtt_ScaleForPixelHeight(&font_info, 16.0f * scale);
    ascent = (int)(ascent * font_scale);

    int bitmap_w, bitmap_h, xoff, yoff;
    unsigned char* bitmap = siv_get_char_bitmap(c, scale, &bitmap_w, &bitmap_h, &xoff, &yoff);

    if (bitmap) {
        for (int row = 0; row < bitmap_h; ++row) {
            for (int col = 0; col < bitmap_w; ++col) {
                uint8_t alpha = bitmap[row * bitmap_w + col];
                siv_put_pixel_alpha(x + xoff + col, y + ascent + yoff + row, color, alpha);
            }
        }
        siv_free_char_bitmap(bitmap);
    }
}

void siv_draw_text(int x, int y, const char* text, float scale, uint32_t color) {
    if (!font_initialized) return;

    float font_scale = stbtt_ScaleForPixelHeight(&font_info, 16.0f * scale);
    int current_x = x;

    while (*text) {
        siv_draw_char(current_x, y, *text, scale, color);
        
        int advanceWidth, leftSideBearing;
        stbtt_GetCodepointHMetrics(&font_info, *text, &advanceWidth, &leftSideBearing);
        current_x += (int)(advanceWidth * font_scale);

        if (*(text + 1)) {
            current_x += (int)(stbtt_GetCodepointKernAdvance(&font_info, *text, *(text + 1)) * font_scale);
        }
        text++;
    }
}

void siv_get_text_size(const char* text, float scale, int* width, int* height) {
    if (!font_initialized) {
        if (width) *width = 0;
        if (height) *height = 0;
        return;
    }

    float font_scale = stbtt_ScaleForPixelHeight(&font_info, 16.0f * scale);
    int w = 0;
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&font_info, &ascent, &descent, &lineGap);

    while (*text) {
        int advanceWidth, leftSideBearing;
        stbtt_GetCodepointHMetrics(&font_info, *text, &advanceWidth, &leftSideBearing);
        w += (int)(advanceWidth * font_scale);

        if (*(text + 1)) {
            w += (int)(stbtt_GetCodepointKernAdvance(&font_info, *text, *(text + 1)) * font_scale);
        }
        text++;
    }

    if (width) *width = w;
    if (height) *height = (int)((ascent - descent) * font_scale);
}

int siv_font_height(float scale) {
    if (!font_initialized) return 0;

    float font_scale = stbtt_ScaleForPixelHeight(&font_info, 16.0f * scale);
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&font_info, &ascent, &descent, &lineGap);
    return (int)((ascent - descent) * font_scale);
}

unsigned char* siv_get_char_bitmap(char c, float scale, int* width, int* height, int* xoff, int* yoff) {
    if (!font_initialized) return NULL;

    float font_scale = stbtt_ScaleForPixelHeight(&font_info, 16.0f * scale);
    return stbtt_GetCodepointBitmap(&font_info, font_scale, font_scale, c, width, height, xoff, yoff);
}

void siv_free_char_bitmap(unsigned char* bitmap) {
    stbtt_FreeBitmap(bitmap, NULL);
} 