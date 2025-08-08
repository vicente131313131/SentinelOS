#ifndef SPRING_INTO_VIEW_H
#define SPRING_INTO_VIEW_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Initialize the graphics library with framebuffer info
void siv_init(uint32_t width, uint32_t height, uint32_t pitch, uint32_t bpp, void* framebuffer);

// Initialize the font from embedded TTF data
bool siv_init_font(void);

// Draw a pixel at (x, y) with the given color
void siv_put_pixel(int x, int y, uint32_t color);

// Draw a pixel at (x, y) with the given color and alpha
void siv_put_pixel_alpha(int x, int y, uint32_t color, uint8_t alpha);

// Draw a line from (x0, y0) to (x1, y1) with the given color
void siv_draw_line(int x0, int y0, int x1, int y1, uint32_t color);

// Draw a rectangle at (x, y) with width w and height h, color, and fill option
void siv_draw_rect(int x, int y, int w, int h, uint32_t color, bool filled);

// Draw a circle at (xc, yc) with radius r, color, and fill option
void siv_draw_circle(int xc, int yc, int r, uint32_t color, bool filled);

// Draw text at (x, y) with a given size and color
void siv_draw_text(int x, int y, const char* text, float size, uint32_t color);

// Get the size of a string in pixels
void siv_get_text_size(const char* text, float scale, int* width, int* height);

// Get the height of the font in pixels
int siv_font_height(float scale);

// Get the bitmap for a character
unsigned char* siv_get_char_bitmap(char c, float scale, int* width, int* height, int* xoff, int* yoff);

// Free the bitmap returned by siv_get_char_bitmap
void siv_free_char_bitmap(unsigned char* bitmap);

void siv_get_screen_size(uint32_t* width, uint32_t* height);
void siv_clear(uint32_t color);
uint32_t siv_get_pixel(int x, int y);

#endif // SPRING_INTO_VIEW_H 