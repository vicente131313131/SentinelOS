#include "bochs_vbe.h"
#include "io.h"
#include "serial.h"

static inline void dispi_write(uint16_t index, uint16_t value) {
    outw(VBE_DISPI_IOPORT_INDEX, index);
    outw(VBE_DISPI_IOPORT_DATA, value);
}

static inline uint16_t dispi_read(uint16_t index) {
    outw(VBE_DISPI_IOPORT_INDEX, index);
    return inw(VBE_DISPI_IOPORT_DATA);
}

bool bochs_vbe_is_present(void) {
    uint16_t id = dispi_read(VBE_DISPI_INDEX_ID);
    return id >= VBE_DISPI_ID0 && id <= VBE_DISPI_ID5;
}

bool bochs_vbe_set_mode(uint16_t width, uint16_t height, uint16_t bpp) {
    if (!bochs_vbe_is_present()) {
        serial_writestring("[BochsVBE] DISPI interface not present.\n");
        return false;
    }

    // Disable before reconfiguring
    dispi_write(VBE_DISPI_INDEX_ENABLE, 0);

    dispi_write(VBE_DISPI_INDEX_XRES, width);
    dispi_write(VBE_DISPI_INDEX_YRES, height);
    dispi_write(VBE_DISPI_INDEX_BPP, bpp);
    dispi_write(VBE_DISPI_INDEX_VIRT_WIDTH, width);
    dispi_write(VBE_DISPI_INDEX_VIRT_HEIGHT, height);
    dispi_write(VBE_DISPI_INDEX_X_OFFSET, 0);
    dispi_write(VBE_DISPI_INDEX_Y_OFFSET, 0);

    // Enable with Linear Framebuffer; NOCLEARMEM to avoid slow clear
    dispi_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED | VBE_DISPI_NOCLEARMEM);

    // Validate
    uint16_t r_w = dispi_read(VBE_DISPI_INDEX_XRES);
    uint16_t r_h = dispi_read(VBE_DISPI_INDEX_YRES);
    uint16_t r_b = dispi_read(VBE_DISPI_INDEX_BPP);
    if (r_w != width || r_h != height || r_b != bpp) {
        serial_writestring("[BochsVBE] Failed to set requested mode.\n");
        return false;
    }
    serial_writestring("[BochsVBE] Mode set via DISPI.\n");
    return true;
}

void bochs_vbe_get_mode(uint16_t* out_width, uint16_t* out_height, uint16_t* out_bpp) {
    if (out_width)  *out_width  = dispi_read(VBE_DISPI_INDEX_XRES);
    if (out_height) *out_height = dispi_read(VBE_DISPI_INDEX_YRES);
    if (out_bpp)    *out_bpp    = dispi_read(VBE_DISPI_INDEX_BPP);
}


