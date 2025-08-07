#ifndef MEM_H
#define MEM_H

#include <stdint.h>

#define KERNEL_HH_BASE 0xFFFFFFFF80000000
#define PHYS_TO_VIRT(p) ((p) + KERNEL_HH_BASE)
#define VIRT_TO_PHYS(v) ((v) - KERNEL_HH_BASE)

#endif 