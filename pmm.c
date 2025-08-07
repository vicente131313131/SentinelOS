#include <stdint.h>
#include <stddef.h>
#include "pmm.h"
#include "serial.h"
#include "string.h"
#include <stdbool.h>

#define PMM_BASE 0x1000000UL // Start at 16MB (after kernel)
#define PMM_MAX  0x40000000UL // Up to 1GB for demo
#define PAGE_SIZE 4096

static uint64_t pmm_next_free = PMM_BASE;
static uint8_t* bitmap = NULL;
static size_t total_pages = 0;
static size_t last_alloc_index = 0;

extern uint8_t _kernel_end[];

void bitmap_set(size_t bit) {
    bitmap[bit / 8] |= (1 << (bit % 8));
}

void bitmap_clear(size_t bit) {
    bitmap[bit / 8] &= ~(1 << (bit % 8));
}

bool bitmap_test(size_t bit) {
    return bitmap[bit / 8] & (1 << (bit % 8));
}

bool pmm_init(struct multiboot2_tag_mmap* mmap_tag) {
    uint64_t highest_addr = 0;

    for (struct multiboot2_mmap_entry* mmap = mmap_tag->entries;
         (uint8_t*)mmap < (uint8_t*)mmap_tag + mmap_tag->size;
         mmap = (struct multiboot2_mmap_entry*)((uint8_t*)mmap + mmap_tag->entry_size)) {
        
        if (mmap->type == MULTIBOOT2_MEMORY_AVAILABLE) {
            uint64_t top = mmap->addr + mmap->len;
            if (top > highest_addr) {
                highest_addr = top;
            }
        }
    }

    total_pages = highest_addr / PAGE_SIZE;
    size_t bitmap_size = (total_pages + 7) / 8;
    
    uint64_t kernel_end_addr = (uint64_t)_kernel_end;
    uint64_t bitmap_search_start = (kernel_end_addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    serial_writestring("PMM: Kernel end address: ");
    serial_writehex(kernel_end_addr);
    serial_writestring("\n");
    serial_writestring("PMM: Bitmap search starts at: ");
    serial_writehex(bitmap_search_start);
    serial_writestring("\n");

    for (struct multiboot2_mmap_entry* mmap = mmap_tag->entries;
         (uint8_t*)mmap < (uint8_t*)mmap_tag + mmap_tag->size;
         mmap = (struct multiboot2_mmap_entry*)((uint8_t*)mmap + mmap_tag->entry_size)) {
        
        if (mmap->type == MULTIBOOT2_MEMORY_AVAILABLE) {
            uint64_t region_start = mmap->addr;
            uint64_t region_len = mmap->len;

            if (region_start < bitmap_search_start) {
                if (region_start + region_len > bitmap_search_start) {
                    uint64_t new_len = region_start + region_len - bitmap_search_start;
                    if (new_len >= bitmap_size) {
                        bitmap = (uint8_t*)bitmap_search_start;
                        break; 
                    }
                }
            } else {
                if (region_len >= bitmap_size) {
                    bitmap = (uint8_t*)region_start;
                    break;
                }
            }
        }
    }

    if (bitmap == NULL) {
        serial_writestring("Error: Could not find a suitable location for the PMM bitmap.\n");
        return false;
    }

    memset(bitmap, 0xFF, bitmap_size); // Mark all pages as used initially

    for (struct multiboot2_mmap_entry* mmap = mmap_tag->entries;
         (uint8_t*)mmap < (uint8_t*)mmap_tag + mmap_tag->size;
         mmap = (struct multiboot2_mmap_entry*)((uint8_t*)mmap + mmap_tag->entry_size)) {
        
        if (mmap->type == MULTIBOOT2_MEMORY_AVAILABLE) {
            for (uint64_t i = 0; i < mmap->len; i += PAGE_SIZE) {
                pmm_free_page((void*)(mmap->addr + i));
            }
        }
    }
    
    uintptr_t bitmap_addr = (uintptr_t)bitmap;
    size_t bitmap_pages = (bitmap_size + PAGE_SIZE - 1) / PAGE_SIZE;
    for (size_t i = 0; i < bitmap_pages; i++) {
        void* page_to_alloc = (void*)(bitmap_addr + i * PAGE_SIZE);
        size_t bit = (uint64_t)page_to_alloc / PAGE_SIZE;
        if(bit < total_pages) {
            bitmap_set(bit);
        }
    }

    serial_writestring("[Serial] PMM Initialized\n");
    return true;
}

void* pmm_alloc_page() {
    for (size_t i = 0; i < total_pages; i++) {
        size_t current_index = (last_alloc_index + i) % total_pages;
        if (!bitmap_test(current_index)) {
            bitmap_set(current_index);
            last_alloc_index = current_index + 1;
            return (void*)(current_index * PAGE_SIZE);
        }
    }
    serial_writestring("[Serial] PMM: Out of memory\n");
    return NULL; // Out of memory
}

void pmm_free_page(void* page) {
    if (page == NULL || (uint64_t)page < 0x100000) return;
    size_t bit = (uint64_t)page / PAGE_SIZE;
    if (bit < total_pages) {
        bitmap_clear(bit);
    }
}

void pmm_get_info(pmm_info_t* info) {
    if (!info) return;

    size_t used_pages = 0;
    for (size_t i = 0; i < total_pages; i++) {
        if (bitmap_test(i)) {
            used_pages++;
        }
    }

    info->total_pages = total_pages;
    info->used_pages = used_pages;
    info->free_pages = total_pages - used_pages;
}

void* pmm_alloc(size_t size) {
    // Align the size to a page boundary
    size_t aligned_size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    if (pmm_next_free + aligned_size > PMM_MAX) {
        return NULL; // Out of memory
    }

    void* addr = (void*)pmm_next_free;
    pmm_next_free += aligned_size;
    return addr;
} 