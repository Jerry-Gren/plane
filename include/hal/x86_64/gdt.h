#ifndef HAL_ARCH_GDT_H
#define HAL_ARCH_GDT_H

#include <stdint.h>

void x86_64_gdt_init(void);

void x86_64_tss_set_kernel_stack(uintptr_t stack);

#endif /* HAL_ARCH_GDT_H */
