#ifndef VBE_H
#define VBE_H

#include <stdint.h>
#include <stdbool.h>
#include "multiboot2.h"

typedef struct {
    uint16_t mode_attributes;
    uint8_t win_a_attributes;
    uint8_t win_b_attributes;
    uint16_t win_granularity;
    uint16_t win_size;
    uint16_t win_a_segment;
    uint16_t win_b_segment;
    uint32_t win_func_ptr;
    uint16_t bytes_per_scan_line;
    uint16_t x_resolution;
    uint16_t y_resolution;
    uint8_t x_char_size;
    uint8_t y_char_size;
    uint8_t number_of_planes;
    uint8_t bits_per_pixel;
    uint8_t number_of_banks;
    uint8_t memory_model;
    uint8_t bank_size;
    uint8_t number_of_image_pages;
    uint8_t reserved1;
    uint8_t red_mask_size;
    uint8_t red_field_position;
    uint8_t green_mask_size;
    uint8_t green_field_position;
    uint8_t blue_mask_size;
    uint8_t blue_field_position;
    uint8_t rsvd_mask_size;
    uint8_t rsvd_field_position;
    uint8_t direct_color_mode_info;
    uint32_t phys_base_ptr;
    uint32_t reserved2;
    uint16_t reserved3;
} __attribute__((packed)) vbe_mode_info_t;

/* VBE Controller Information block (partial) as defined by VESA BIOS Extensions
 * Bootloader (GRUB) fills this structure via BIOS INT 0x10 AX=0x4F00 and copies
 * it into the Multiboot2 tag. We only define the fields we need: the pointer to
 * the video mode list. All other fields can be extended later if required.
 */
typedef struct {
    char     signature[4];      /* 'VESA' */
    uint16_t version;           /* BCD BIOS version, e.g. 0x0300 => 3.0 */
    uint32_t oem_string_ptr;    /* Far pointer to OEM string */
    uint32_t capabilities;      /* Capabilities of graphics controller */
    uint32_t video_mode_ptr;    /* Far pointer to list of supported modes */
    uint16_t total_memory;      /* Number of 64-KiB blocks of memory */
    /* Remaining fields are unused in this kernel. */
} __attribute__((packed)) vbe_controller_info_t;

void vbe_init(struct multiboot2_tag_vbe* vbe_tag);
vbe_mode_info_t* vbe_get_mode_info(uint16_t mode);
const vbe_controller_info_t* vbe_get_controller_info(void);
const uint16_t* vbe_get_mode_list(void);
void vbe_set_mode(uint16_t mode);

// Convenience: set mode by explicit WxHxbpp using Bochs/QEMU DISPI if available.
// Returns true on success.
bool vbe_set_mode_lfb(uint16_t width, uint16_t height, uint16_t bpp);


#endif // VBE_H 