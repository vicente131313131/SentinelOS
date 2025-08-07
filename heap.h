#ifndef HEAP_H
#define HEAP_H

#include <stddef.h>

typedef struct {
    size_t total_bytes;
    size_t used_bytes;
    size_t free_bytes;
} heap_info_t;

void heap_init();
void* kmalloc(size_t size);
void kfree(void* ptr);
void heap_get_info(heap_info_t* info);

#endif // HEAP_H 