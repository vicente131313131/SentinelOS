#ifndef MB2_FRAMEBUFFER_H
#define MB2_FRAMEBUFFER_H
#include <stdint.h>

#define MB2_TAG_TYPE_FRAMEBUFFER 8

struct mb2_tag {
    uint32_t type;
    uint32_t size;
};

struct mb2_tag_framebuffer {
    uint32_t type;
    uint32_t size;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;
    uint16_t reserved;
};

#endif // MB2_FRAMEBUFFER_H 