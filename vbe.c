/*
 * vbe.c – Minimal helpers to read VBE info provided by the bootloader
 *
 * What this module provides
 * - Access to VBE controller info (512-byte block) and current mode info (256-byte block)
 *   copied into the Multiboot2 VBE tag by the bootloader (GRUB).
 * - Optional runtime mode change via Bochs/QEMU DISPI (for emulators only),
 *   exposed through vbe_set_mode_lfb(). No BIOS calls are used.
 *
 * What this module does NOT do
 * - It does not call VBE BIOS (INT 10h). On real hardware, runtime switching is
 *   outside the scope of this kernel.
 */

#include "vbe.h"
#include "serial.h"
#include "bochs_vbe.h"
#include <stddef.h>
#include <stdint.h>

/* Static pointers into the Multiboot2 VBE tag. Valid after vbe_init(). */
static struct multiboot2_tag_vbe*      vbe_info_tag   = NULL;
static const vbe_controller_info_t*    vbe_ctrl_info  = NULL;
static const uint16_t*                 vbe_mode_list  = NULL;  /* 0xFFFF-terminated list */

/* Convert a 32-bit far pointer (seg:off in 16-bit real mode) to a physical addr:
 *   phys = segment * 16 + offset
 */
static inline uintptr_t far_ptr_to_phys(uint32_t far_ptr)
{
    uint16_t offset  = far_ptr & 0xFFFF;
    uint16_t segment = (far_ptr >> 16) & 0xFFFF;
    return (uintptr_t)segment * 16 + offset;
}

/* Initialize internal pointers from the Multiboot2 VBE tag. */
void vbe_init(struct multiboot2_tag_vbe* vbe_tag)
{
    vbe_info_tag  = vbe_tag;
    vbe_ctrl_info = (const vbe_controller_info_t*)vbe_tag->vbe_control_info;

    /* Locate the firmware mode list (0xFFFF-terminated list of 16-bit mode ids).
     * GRUB places the list inside the 512-byte controller-info block and stores
     * a far pointer to it. We verify that the pointer resolves inside that block
     * and only then expose it via vbe_get_mode_list().
     */
    uintptr_t phys_ptr = far_ptr_to_phys(vbe_ctrl_info->video_mode_ptr);
    uintptr_t ctrl_phys = (uintptr_t)vbe_ctrl_info;
    if (phys_ptr >= ctrl_phys && phys_ptr < (ctrl_phys + sizeof(vbe_tag->vbe_control_info))) {
        vbe_mode_list = (const uint16_t*)phys_ptr;
    } else {
        vbe_mode_list = NULL;
        serial_writestring("[VBE] Mode list pointer outside control block – mode enumeration disabled.\n");
    }

    serial_writestring("[VBE] Controller info and current mode parsed.\n");
}

/* Return pointer to the VBE controller info block (read-only). */
const vbe_controller_info_t* vbe_get_controller_info(void)
{
    return vbe_ctrl_info;
}

/* Return 0xFFFF-terminated list of mode numbers (or NULL if not available). */
const uint16_t* vbe_get_mode_list(void)
{
    return vbe_mode_list;
}

/* Return VBE mode information for the given mode if GRUB provided it.
 * Note: GRUB only fills the block for the current mode; other modes require BIOS.
 */
vbe_mode_info_t* vbe_get_mode_info(uint16_t mode)
{
    if (!vbe_info_tag)
        return NULL;

    /* Fast path: bootloader filled mode_info for the current mode only. */
    if (vbe_info_tag->vbe_mode == mode) {
        return (vbe_mode_info_t*)vbe_info_tag->vbe_mode_info;
    }

    /* No BIOS: cannot query arbitrary mode info here. */
    serial_writestring("[VBE] vbe_get_mode_info: information for requested mode is not available.\n");
    return NULL;
}

/* Stub for BIOS-based runtime mode switch (not implemented). */
void vbe_set_mode(uint16_t mode)
{
    (void)mode;
    serial_writestring("[VBE] vbe_set_mode: runtime mode switching is not implemented.\n");
}

/* Runtime mode switch for emulators (Bochs/QEMU) via the DISPI register set.
 * Works in protected/long mode without BIOS. Returns true on success.
 */
bool vbe_set_mode_lfb(uint16_t width, uint16_t height, uint16_t bpp)
{
    // Prefer Bochs/QEMU VBE DISPI if present
    if (bochs_vbe_is_present()) {
        bool ok = bochs_vbe_set_mode(width, height, bpp);
        if (!ok) return false;
        serial_writestring("[VBE] Mode changed using Bochs/QEMU DISPI.\n");
        return true;
    }

    serial_writestring("[VBE] DISPI not available; BIOS-based runtime switching not implemented.\n");
    return false;
}
