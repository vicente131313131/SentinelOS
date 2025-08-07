#include "mouse.h"
#include "io.h"
#include "isr.h"
#include "pic.h"
#include "serial.h"

#define MOUSE_PORT   0x60
#define MOUSE_STATUS 0x64
#define MOUSE_CMD    0x64

#define MOUSE_WRITE_CMD 0xD4
#define MOUSE_ENABLE_PACKET_STREAMING 0xF4
#define MOUSE_SET_DEFAULTS 0xF6
#define MOUSE_ACK 0xFA

static mouse_state_t mouse_state;
static uint8_t mouse_cycle = 0;
static int8_t mouse_byte[3];

static void mouse_wait(uint8_t type) {
    uint32_t timeout = 100000;
    if (type == 0) {
        while (timeout-- && (inb(MOUSE_STATUS) & 1) == 0);
    } else {
        while (timeout-- && (inb(MOUSE_STATUS) & 2) == 0);
    }
}

static void mouse_write(uint8_t write) {
    mouse_wait(1);
    outb(MOUSE_CMD, 0xD4);
    mouse_wait(1);
    outb(MOUSE_PORT, write);
}

static uint8_t mouse_read() {
    mouse_wait(0);
    return inb(MOUSE_PORT);
}

void mouse_handler(registers *r) {
    uint8_t status = inb(MOUSE_STATUS);
    while (status & 0x01) {
        uint8_t scancode = inb(MOUSE_PORT);
        if (status & 0x20) { // Check if data is from mouse
            switch (mouse_cycle) {
                case 0:
                    mouse_byte[0] = scancode;
                    if (!(scancode & 0x08)) return;
                    mouse_cycle++;
                    break;
                case 1:
                    mouse_byte[1] = scancode;
                    mouse_cycle++;
                    break;
                case 2:
                    mouse_byte[2] = scancode;
                    mouse_cycle = 0;

                    mouse_state.left_button = mouse_byte[0] & 0x1;
                    mouse_state.right_button = mouse_byte[0] & 0x2;
                    mouse_state.middle_button = mouse_byte[0] & 0x4;

                    int32_t delta_x = mouse_byte[1];
                    int32_t delta_y = mouse_byte[2];

                    if (mouse_byte[0] & 0x10) { // x negative
                        delta_x = (int8_t)mouse_byte[1];
                    }
                    if (mouse_byte[0] & 0x20) { // y negative
                        delta_y = (int8_t)mouse_byte[2];
                    }
                    
                    // We flip the y-axis
                    mouse_state.x += delta_x;
                    mouse_state.y -= delta_y;

                    // Clamp to screen dimensions (will need to get these from somewhere)
                    // For now, let's assume some large bounds.
                    // This will be fixed when integrating with the desktop.
                    if (mouse_state.x < 0) mouse_state.x = 0;
                    if (mouse_state.y < 0) mouse_state.y = 0;
                    // if (mouse_state.x > screen_width) mouse_state.x = screen_width;
                    // if (mouse_state.y > screen_height) mouse_state.y = screen_height;
                    break;
            }
        }
        status = inb(MOUSE_STATUS);
    }
    pic_send_eoi(12);
}

void mouse_init(void) {
    mouse_wait(1);
    outb(MOUSE_CMD, 0xA8); // Enable auxiliary device

    mouse_wait(1);
    outb(MOUSE_CMD, 0x20); // Get Compaq status byte
    mouse_wait(0);
    uint8_t status = inb(MOUSE_PORT);
    status |= 0x02; // set bit 1, enable IRQ12
    status &= 0xDF; // clear bit 5, disable mouse clock
    mouse_wait(1);
    outb(MOUSE_CMD, 0x60); // Set Compaq status byte
    mouse_wait(1);
    outb(MOUSE_PORT, status);

    mouse_write(MOUSE_SET_DEFAULTS);
    mouse_read(); 

    mouse_write(MOUSE_ENABLE_PACKET_STREAMING);
    mouse_read();

    register_interrupt_handler(IRQ12, mouse_handler);
    pic_unmask_irq(12);
    
    mouse_state.x = 800 / 2; // starting position
    mouse_state.y = 600 / 2;
    serial_writestring("Mouse Initialized\n");
}

const mouse_state_t* mouse_get_state(void) {
    return &mouse_state;
} 