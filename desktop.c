#include "desktop.h"
#include "SpringIntoView/spring_into_view.h"
#include "mouse.h"
#include "serial.h"

// Screen dimensions - will be initialized in desktop_init
static uint32_t screen_width;
static uint32_t screen_height;
static bool desktop_is_active = false;

// === NEW: UI constants ===
#define MENU_BAR_HEIGHT 24
#define DOCK_HEIGHT     48
#define DOCK_ICON_SIZE  40
#define DOCK_ICON_PADDING 8

// Colors (0x00RRGGBB)
#define COLOR_BG         0x00336699
#define COLOR_MENU_BG    0x00222222
#define COLOR_DOCK_BG    0x00222222
#define COLOR_DOCK_ICON  0x00444444
#define COLOR_TEXT       0x00FFFFFF

// === NEW: Dock application structure ===
typedef struct {
    int x;
    int y;
    const char* label;
} dock_app_t;

static dock_app_t dock_apps[] = {
    {0, 0, "Terminal"},
    {0, 0, "Editor"},
    {0, 0, "Browser"}
};
#define NUM_DOCK_APPS (sizeof(dock_apps) / sizeof(dock_apps[0]))

#define CURSOR_WIDTH 12
#define CURSOR_HEIGHT 12
static uint32_t cursor_buffer[CURSOR_WIDTH * CURSOR_HEIGHT];

// === NEW: UI drawing helpers ===
static void draw_menu_bar(void) {
    siv_draw_rect(0, 0, screen_width, MENU_BAR_HEIGHT, COLOR_MENU_BG, true);
    siv_draw_text(10, 4, "SentinelOS", 1.0f, COLOR_TEXT);
}

static void draw_dock(void) {
    int dock_y = (int)screen_height - DOCK_HEIGHT;
    siv_draw_rect(0, dock_y, screen_width, DOCK_HEIGHT, COLOR_DOCK_BG, true);

    int x = 20; // initial x offset
    for (int i = 0; i < (int)NUM_DOCK_APPS; ++i) {
        dock_apps[i].x = x;
        dock_apps[i].y = dock_y + (DOCK_HEIGHT - DOCK_ICON_SIZE) / 2;
        siv_draw_rect(dock_apps[i].x, dock_apps[i].y, DOCK_ICON_SIZE, DOCK_ICON_SIZE, COLOR_DOCK_ICON, true);
        siv_draw_text(dock_apps[i].x + 4, dock_apps[i].y + DOCK_ICON_SIZE - 12, dock_apps[i].label, 1.0f, COLOR_TEXT);
        x += DOCK_ICON_SIZE + DOCK_ICON_PADDING;
    }
}

static void draw_static_ui(void) {
    draw_menu_bar();
    draw_dock();
}

// === NEW: Dock click handling ===
static void launch_app(int index) {
    serial_writestring("App launched: ");
    serial_writestring(dock_apps[index].label);
    serial_writestring("\n");
    // TODO: open actual application window
}

static void handle_click(int32_t x, int32_t y) {
    // Check dock icons first
    for (int i = 0; i < (int)NUM_DOCK_APPS; ++i) {
        if (x >= dock_apps[i].x && x < dock_apps[i].x + DOCK_ICON_SIZE &&
            y >= dock_apps[i].y && y < dock_apps[i].y + DOCK_ICON_SIZE) {
            launch_app(i);
            return;
        }
    }
    // Future: check menu bar items or windows
}

static void save_cursor_background(int32_t x, int32_t y) {
    for (int i = 0; i < CURSOR_HEIGHT; ++i) {
        for (int j = 0; j < CURSOR_WIDTH; ++j) {
            if (x + j < (int32_t)screen_width && y + i < (int32_t)screen_height) {
                cursor_buffer[i * CURSOR_WIDTH + j] = siv_get_pixel(x + j, y + i);
            }
        }
    }
}

static void restore_cursor_background(int32_t x, int32_t y) {
    for (int i = 0; i < CURSOR_HEIGHT; ++i) {
        for (int j = 0; j < CURSOR_WIDTH; ++j) {
            if (x + j < (int32_t)screen_width && y + i < (int32_t)screen_height) {
                siv_put_pixel(x + j, y + i, cursor_buffer[i * CURSOR_WIDTH + j]);
            }
        }
    }
}

void desktop_init() {
    // Get screen dimensions from SpringIntoView
    siv_get_screen_size(&screen_width, &screen_height);
    
    // Set initial mouse position to center of screen
    mouse_state_t* m_state = (mouse_state_t*)mouse_get_state();
    m_state->x = screen_width / 2;
    m_state->y = screen_height / 2;

    siv_clear(COLOR_BG); // background

    draw_static_ui(); // draw menu bar and dock

    desktop_is_active = true;
    serial_writestring("Desktop Initialized\n");
}

bool is_desktop_active() {
    return desktop_is_active;
}

static void clamp_mouse_position(mouse_state_t* m_state) {
    if (m_state->x < 0) m_state->x = 0;
    if (m_state->y < 0) m_state->y = 0;
    if (m_state->x >= (int32_t)screen_width) m_state->x = screen_width - 1;
    if (m_state->y >= (int32_t)screen_height) m_state->y = screen_height - 1;
}

void desktop_run() {
    uint32_t bg_color = COLOR_BG;
    uint32_t mouse_color = 0x00FFFFFF;

    const mouse_state_t* m_state = mouse_get_state();
    int32_t last_mouse_x = m_state->x;
    int32_t last_mouse_y = m_state->y;
    bool last_left_button = m_state->left_button;

    save_cursor_background(last_mouse_x, last_mouse_y);
    siv_draw_cursor(last_mouse_x, last_mouse_y, mouse_color);

    while (1) {
        m_state = mouse_get_state();
        // Detect framebuffer resize (e.g., window size change in QEMU with virtio-gpu)
        uint32_t new_w, new_h;
        siv_get_screen_size(&new_w, &new_h);
        if (new_w != screen_width || new_h != screen_height) {
            screen_width  = new_w;
            screen_height = new_h;

            // Clear entire screen and redraw static UI elements
            siv_clear(COLOR_BG);
            draw_static_ui();

            // Re-save cursor background at new location
            restore_cursor_background(last_mouse_x, last_mouse_y); // restore old area before coords maybe out-of-bounds
            clamp_mouse_position((mouse_state_t*)m_state);
            save_cursor_background(m_state->x, m_state->y);
            siv_draw_cursor(m_state->x, m_state->y, mouse_color);
            last_mouse_x = m_state->x;
            last_mouse_y = m_state->y;
        }

        // Clamp mouse coordinates
        clamp_mouse_position((mouse_state_t*)m_state);

        // === NEW: click detection ===
        if (m_state->left_button && !last_left_button) {
            handle_click(m_state->x, m_state->y);
        }
        last_left_button = m_state->left_button;

        if (m_state->x != last_mouse_x || m_state->y != last_mouse_y) {
            // Restore background
            restore_cursor_background(last_mouse_x, last_mouse_y);

            // Save new background
            save_cursor_background(m_state->x, m_state->y);

            // Draw new cursor
            siv_draw_cursor(m_state->x, m_state->y, mouse_color);

            last_mouse_x = m_state->x;
            last_mouse_y = m_state->y;
        }

        // Add a small delay to prevent busy-waiting and hogging CPU
        // A more proper solution would use interrupts to only update when needed
        for (volatile int i = 0; i < 50000; ++i) {
            // busy wait
        }

        asm("hlt");
    }
} 