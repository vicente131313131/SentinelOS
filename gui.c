#include "gui.h"
#include "SpringIntoView/spring_into_view.h"
#include "mouse.h"
#include "serial.h"
 
// center cursor when GUI starts
extern void mouse_set_position(int32_t x, int32_t y);

typedef struct {
	int x;
	int y;
	int w;
	int h;
	bool dragging;
	int drag_off_x;
	int drag_off_y;
} gui_window_t;

static bool g_gui_active = false;
static uint32_t g_screen_w = 0;
static uint32_t g_screen_h = 0;

static gui_window_t g_demo;

static void draw_taskbar(void)
{
	const int tb_h = 32;
	// Taskbar background
	siv_draw_rect(0, (int)g_screen_h - tb_h, (int)g_screen_w, tb_h, 0x00222A33, true);
	// Separator line
	siv_draw_rect(0, (int)g_screen_h - tb_h - 1, (int)g_screen_w, 1, 0x00333C45, true);
	// Title
	siv_draw_text(10, (int)g_screen_h - tb_h + 8, "SentinelOS", 1.0f, 0xFFFFFFFF);
}

static void draw_window(gui_window_t* win)
{
	// Window shadow
	siv_draw_rect(win->x + 4, win->y + 4, win->w, win->h, 0x00000000, true);
	// Window body
	siv_draw_rect(win->x, win->y, win->w, win->h, 0x00E3E8EE, true);
	// Title bar
	siv_draw_rect(win->x, win->y, win->w, 24, win->dragging ? 0x004A90E2 : 0x003A7BD5, true);
	// Title text
	siv_draw_text(win->x + 8, win->y + 6, "Demo Window", 1.0f, 0xFFFFFFFF);
	// Border
	siv_draw_rect(win->x, win->y, win->w, 1, 0x00222A33, true);
	siv_draw_rect(win->x, win->y + win->h - 1, win->w, 1, 0x00222A33, true);
	siv_draw_rect(win->x, win->y, 1, win->h, 0x00222A33, true);
	siv_draw_rect(win->x + win->w - 1, win->y, 1, win->h, 0x00222A33, true);

	// Some content
	siv_draw_text(win->x + 12, win->y + 36, "Hello from GUI!", 1.0f, 0x00000000);
}

static void draw_cursor(int x, int y)
{
    // 13x21 arrow with black border and white fill to reduce blending glitches
    static const uint16_t mask[21] = {
        0b1000000000000,
        0b1100000000000,
        0b1110000000000,
        0b1111000000000,
        0b1111100000000,
        0b1111110000000,
        0b1111111000000,
        0b1111111100000,
        0b1111111110000,
        0b1111111111000,
        0b1111111111100,
        0b1111111111110,
        0b1111111111111,
        0b1111111111110,
        0b1111111111100,
        0b1111111111000,
        0b1111111110000,
        0b1111111100000,
        0b1111111000000,
        0b1011110000000,
        0b0011100000000
    };
    for (int row = 0; row < 21; ++row) {
        uint16_t m = mask[row];
        for (int col = 0; col < 13; ++col) {
            if (m & (1 << (12 - col))) {
                // border: draw black one-pixel outline around white core
                siv_put_pixel(x + col, y + row, 0x00000000);
                if (col > 0 && (m & (1 << (12 - (col - 1))))) {
                    siv_put_pixel(x + col - 1, y + row, 0x00FFFFFF);
                } else {
                    siv_put_pixel(x + col, y + row, 0x00FFFFFF);
                }
            }
        }
    }
}

bool gui_is_active(void)
{
	return g_gui_active;
}

void gui_init(void)
{
	siv_get_screen_size(&g_screen_w, &g_screen_h);
	if (g_screen_w == 0 || g_screen_h == 0) {
		serial_writestring("[GUI] No framebuffer. GUI disabled.\n");
		g_gui_active = false;
		return;
	}
    // Turn on double buffering to avoid flicker on some emulators
    siv_enable_double_buffer(true);
    // Desktop background (bright color for visibility)
    siv_clear(0x0033CC99);
	// Demo window
	g_demo.x = (int)(g_screen_w / 2) - 200;
	g_demo.y = (int)(g_screen_h / 2) - 120;
	g_demo.w = 400;
	g_demo.h = 240;
	g_demo.dragging = false;
	g_demo.drag_off_x = 0;
	g_demo.drag_off_y = 0;

	g_gui_active = true;

    // Center mouse
    mouse_set_position((int32_t)(g_screen_w / 2), (int32_t)(g_screen_h / 2));
}

void gui_update(void)
{
	if (!g_gui_active) return;

	// Input
	const mouse_state_t* ms = mouse_get_state();
	int mx = ms->x;
	int my = ms->y;
	if (mx < 0) mx = 0;
	if (my < 0) my = 0;
	if (mx > (int)g_screen_w - 1) mx = (int)g_screen_w - 1;
	if (my > (int)g_screen_h - 1) my = (int)g_screen_h - 1;

	// Drag logic on title bar
	bool on_title = (mx >= g_demo.x && mx < g_demo.x + g_demo.w && my >= g_demo.y && my < g_demo.y + 24);
	if (ms->left_button && on_title) {
		if (!g_demo.dragging) {
			g_demo.dragging = true;
			g_demo.drag_off_x = mx - g_demo.x;
			g_demo.drag_off_y = my - g_demo.y;
		}
	} else if (!ms->left_button) {
		g_demo.dragging = false;
	}
	if (g_demo.dragging) {
		g_demo.x = mx - g_demo.drag_off_x;
		g_demo.y = my - g_demo.drag_off_y;
		// Clamp window within screen (leaving room for taskbar)
		const int tb_h = 32;
		if (g_demo.x < 0) g_demo.x = 0;
		if (g_demo.y < 0) g_demo.y = 0;
		if (g_demo.x + g_demo.w > (int)g_screen_w) g_demo.x = (int)g_screen_w - g_demo.w;
		if (g_demo.y + g_demo.h > (int)g_screen_h - tb_h) g_demo.y = (int)g_screen_h - tb_h - g_demo.h;
	}

    // Draw
    siv_clear(0x0033CC99);
	draw_taskbar();
	draw_window(&g_demo);
    draw_cursor(mx, my);
    // Present backbuffer if double buffering is enabled
    siv_present();
}


