#ifndef X86_64_DESC_H
#define X86_64_DESC_H

#include <stdint.h>

void x86_64_gdt_init(void);
void x86_64_tss_set_kernel_stack(uintptr_t stack);

void x86_64_idt_init(void);
void x86_64_idt_load_current(void);

#endif /* X86_64_DESC_H */
