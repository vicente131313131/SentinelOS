#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include <stddef.h>
#include "multiboot2.h"

#define PAGE_SIZE 4096

typedef struct {
    size_t total_pages;
    size_t used_pages;
    size_t free_pages;
} pmm_info_t;

bool pmm_init(struct multiboot2_tag_mmap *mmap_tag);
void* pmm_alloc_page();
void pmm_free_page(void* page);
void* pmm_alloc(size_t size);
void pmm_get_info(pmm_info_t* info);

#endif // PMM_H 