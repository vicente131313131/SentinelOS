#include "vbe.h"
#include "serial.h"
#include <stddef.h>

static struct multiboot2_tag_vbe* vbe_info_tag = NULL;

void vbe_init(struct multiboot2_tag_vbe* vbe_tag) {
    vbe_info_tag = vbe_tag;
    serial_writestring("VBE Info Initialized\n");
}

vbe_mode_info_t* vbe_get_mode_info(uint16_t mode) {
    if (!vbe_info_tag) {
        serial_writestring("VBE info tag not available.\n");
        return NULL;
    }

    // This is a simplification. In a real scenario, you'd iterate through
    // a list of modes provided by the VBE info block. 
    // The bootloader usually provides info for the *current* mode only in this tag.
    // To get a full list, we would need to parse the VBE controller info,
    // which is more complex.
    if (vbe_info_tag->vbe_mode == mode) {
        return (vbe_mode_info_t*) &vbe_info_tag->vbe_mode_info;
    }

    serial_writestring("Requested VBE mode info not available.\n");
    return NULL;
}

void vbe_set_mode(uint16_t mode) {
    // Setting the mode after boot is complex. It requires calling
    // a real-mode BIOS interrupt. This functionality is non-trivial to implement
    // in a protected mode kernel.
    // For now, we will rely on the bootloader to set the desired mode.
    serial_writestring("vbe_set_mode is not implemented yet.\n");
} 