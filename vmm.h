#ifndef VMM_H
#define VMM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define PAGE_PRESENT (1 << 0)
#define PAGE_WRITABLE (1 << 1)
#define PAGE_USER (1 << 2)

void vmm_init();
bool vmm_map_page(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags);
void vmm_unmap_page(uint64_t virt_addr);
uint64_t* vmm_get_pml4();
// Map [phys, phys+size) to the same virtual addresses with given flags.
// Returns true on success, false on any allocation failure.
bool vmm_identity_map_range(uint64_t phys_addr, size_t size, uint64_t flags);

#endif 