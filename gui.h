#ifndef GUI_H
#define GUI_H

#include <stdint.h>
#include <stdbool.h>

// Initialize the desktop GUI (background, taskbar, demo window).
// Safe to call if a linear framebuffer is available and SpringIntoView is initialized.
void gui_init(void);

// Render and process one frame. Call this regularly (e.g., from the idle loop).
void gui_update(void);

// Whether the GUI subsystem is active (initialized successfully)
bool gui_is_active(void);

#endif // GUI_H


