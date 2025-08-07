#include "heap.h"
#include "pmm.h"
#include "serial.h"
#include <stddef.h>

typedef struct header {
    struct header *next;
    size_t size;
} header_t;

static header_t base;
static header_t *free_list = NULL;
static size_t heap_total_size = 0;

static header_t* morecore(size_t num_units) {
    char *cp;
    header_t *up;

    if (num_units < (PAGE_SIZE / sizeof(header_t))) {
        num_units = (PAGE_SIZE / sizeof(header_t));
    }

    cp = pmm_alloc_page();
    if (cp == NULL) {
        serial_writestring("[Serial] PMM out of memory for heap.\n");
        return NULL;
    }
    
    up = (header_t*) cp;
    up->size = num_units;
    heap_total_size += num_units * sizeof(header_t);
    kfree((void*)(up + 1));
    
    return free_list;
}

void heap_init() {
    base.next = &base;
    base.size = 0;
    free_list = &base;
    morecore(1);
    serial_writestring("[Serial] Kernel heap initialized.\n");
}

void* kmalloc(size_t nbytes) {
    header_t *p, *prevp;
    size_t nunits;

    if (nbytes == 0) return NULL;

    nunits = (nbytes + sizeof(header_t) - 1) / sizeof(header_t) + 1;

    prevp = free_list;
    for (p = prevp->next; ; prevp = p, p = p->next) {
        if (p->size >= nunits) {
            if (p->size == nunits) {
                prevp->next = p->next;
            } else {
                p->size -= nunits;
                p += p->size;
                p->size = nunits;
            }
            free_list = prevp;
            return (void*)(p + 1);
        }
        if (p == free_list) {
            if ((p = morecore(nunits)) == NULL) {
                return NULL;
            }
        }
    }
}

void kfree(void* ptr) {
    if (ptr == NULL) {
        return;
    }

    header_t *bp = (header_t*)ptr - 1;
    header_t *p;

    for (p = free_list; !(bp > p && bp < p->next); p = p->next) {
        if (p >= p->next && (bp > p || bp < p->next)) {
            break;
        }
    }

    if (bp + bp->size == p->next) {
        bp->size += p->next->size;
        bp->next = p->next->next;
    } else {
        bp->next = p->next;
    }

    if (p + p->size == bp) {
        p->size += bp->size;
        p->next = bp->next;
    } else {
        p->next = bp;
    }
    
    free_list = p;
}

void heap_get_info(heap_info_t* info) {
    if (!info) return;

    header_t* p;
    size_t free_bytes = 0;
    for (p = free_list->next; p != &base; p = p->next) {
        free_bytes += p->size * sizeof(header_t);
    }

    info->total_bytes = heap_total_size;
    info->free_bytes = free_bytes;
    info->used_bytes = heap_total_size - free_bytes;
} 