/* vmm.c – Minimal 4-level paging helpers for mapping/unmapping pages */
#include "vmm.h"
#include "pmm.h"
#include "serial.h"
#include "string.h" // For memset
#include "mem.h"

#define PAGE_SIZE 4096
#define PAGING_FLAG_MASK 0xFFF
#define ADDRESS_MASK (~PAGING_FLAG_MASK)
#define PAGE_HUGE (1 << 7)

/* Read CR3 (physical address of PML4) and return it as a pointer. */
static uint64_t* get_pml4() {
    uint64_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    // The address in CR3 is physical. With identity mapping for low memory,
    // we can cast it directly to a pointer.
    return (uint64_t*)(cr3);
}

/* Invalidate a single TLB entry for the given virtual address. */
static inline void invlpg(void* addr) {
    asm volatile("invlpg (%0)" : : "b"(addr) : "memory");
}

/* Walk to the next paging level, optionally allocating a new table. */
static uint64_t* get_next_level_table(uint64_t* table, uint16_t index, bool allocate) {
    uint64_t entry = table[index];
    if (entry & PAGE_PRESENT) {
        // The address in the page table entry is physical.
        // We can access it because the page tables are in identity-mapped low memory.
        return (uint64_t*)(entry & ADDRESS_MASK);
    }

    if (!allocate) {
        return NULL;
    }

    void* new_table_phys = pmm_alloc_page();
    if (!new_table_phys) {
        serial_writestring("VMM: Failed to allocate page for new page table.\n");
        return NULL;
    }
    
    // The new table is in identity-mapped low memory.
    memset(new_table_phys, 0, PAGE_SIZE);
    table[index] = (uint64_t)new_table_phys | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
    return (uint64_t*)new_table_phys;
}

/* Map one 4KB page at virt_addr -> phys_addr with flags. */
bool vmm_map_page(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags) {
    uint64_t* pml4 = get_pml4();
    uint16_t pml4_index = (virt_addr >> 39) & 0x1FF;
    uint16_t pdpt_index = (virt_addr >> 30) & 0x1FF;
    uint16_t pdt_index = (virt_addr >> 21) & 0x1FF;
    uint16_t pt_index = (virt_addr >> 12) & 0x1FF;

    uint64_t* pdpt = get_next_level_table(pml4, pml4_index, true);
    if (!pdpt) return false;

    uint64_t* pdt = get_next_level_table(pdpt, pdpt_index, true);
    if (!pdt) return false;
    
    if (pdt[pdt_index] & PAGE_HUGE) {
        // If a 2MB huge page already maps this region and the caller requests
        // an identity mapping for an address covered by that huge page, treat
        // it as success (no-op). This avoids splitting the huge page.
        uint64_t huge_base = pdt[pdt_index] & ADDRESS_MASK;
        uint64_t virt_huge_base = virt_addr & ~0x1FFFFFULL; // 2MB aligned
        if (virt_addr == phys_addr && virt_huge_base == huge_base) {
            // Already mapped by an existing 2MB page; nothing to do.
            return true;
        }
        serial_writestring("VMM: Attempted to map a 4KB page where a 2MB huge page exists. This is not supported.\n");
        return false;
    }

    uint64_t* pt = get_next_level_table(pdt, pdt_index, true);
    if (!pt) return false;

    pt[pt_index] = phys_addr | flags;
    invlpg((void*)virt_addr);

    return true;
}

/* Unmap one 4KB page at virt_addr. */
void vmm_unmap_page(uint64_t virt_addr) {
    uint64_t* pml4 = get_pml4();
    uint16_t pml4_index = (virt_addr >> 39) & 0x1FF;
    uint16_t pdpt_index = (virt_addr >> 30) & 0x1FF;
    uint16_t pdt_index = (virt_addr >> 21) & 0x1FF;
    uint16_t pt_index = (virt_addr >> 12) & 0x1FF;

    uint64_t* pdpt = get_next_level_table(pml4, pml4_index, false);
    if (!pdpt) return;

    uint64_t* pdt = get_next_level_table(pdpt, pdpt_index, false);
    if (!pdt) return;

    uint64_t* pt = get_next_level_table(pdt, pdt_index, false);
    if (!pt) return;

    pt[pt_index] = 0;
    invlpg((void*)virt_addr);
}

/* Identity-map [phys_addr, phys_addr+size) using 4KB pages. */
bool vmm_identity_map_range(uint64_t phys_addr, size_t size, uint64_t flags) {
    uint64_t aligned_start = phys_addr & ~0xFFFULL;
    uint64_t aligned_end   = (phys_addr + size + 0xFFFULL) & ~0xFFFULL;
    for (uint64_t addr = aligned_start; addr < aligned_end; addr += 0x1000) {
        if (!vmm_map_page(addr, addr, flags)) {
            return false;
        }
    }
    return true;
}

/* Print CR3 address for debugging. */
void vmm_init() {
    uint64_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    serial_writestring("[Serial] VMM Initialized, CR3 is at: ");
    serial_writehex(cr3);
    serial_writestring("\n");
} 