#ifndef X86_64_GDT_H
#define X86_64_GDT_H

#include <stdint.h>

void x86_64_gdt_init(void);

void x86_64_tss_set_kernel_stack(uintptr_t stack);

#endif /* X86_64_GDT_H */
