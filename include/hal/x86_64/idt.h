#ifndef HAL_ARCH_IDT_H
#define HAL_ARCH_IDT_H

void x86_64_idt_init(void);
void x86_64_idt_load_current(void);

#endif /* HAL_ARCH_IDT_H */
