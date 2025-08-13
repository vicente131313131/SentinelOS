#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>
#include <stdbool.h>
#include "isr.h"

typedef struct {
    int32_t x;
    int32_t y;
    bool left_button;
    bool right_button;
    bool middle_button;
} mouse_state_t;

void mouse_init(void);
void mouse_handler(registers *r);
const mouse_state_t* mouse_get_state(void);
// Set inclusive maximum bounds for x and y in pixels (0..max_x, 0..max_y)
void mouse_set_bounds(int32_t max_x, int32_t max_y);
// Set absolute mouse position (clamped to bounds)
void mouse_set_position(int32_t x, int32_t y);

#endif // MOUSE_H 