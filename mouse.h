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

#endif // MOUSE_H 