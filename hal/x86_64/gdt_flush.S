#include <hal/x86_64/gdt.h>

.global x86_64_gdt_flush
.global x86_64_tss_flush

.section .text

/*
 * void x86_64_gdt_flush(uint64_t gdtr_addr);
 * call rule: %rdi = gdtr_addr
 */
x86_64_gdt_flush:
    /* load new gdt */
    lgdt (%rdi)

    /* flush kernel data */
    mov $SELECTOR_KERNEL_DS, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss

    /* flush kernel code */
    pop %rdi
    push $SELECTOR_KERNEL_CS
    push %rdi
    lretq

/*
 * void x86_64_tss_flush(void);
 */
x86_64_tss_flush:
    mov $SELECTOR_TSS, %ax
    ltr %ax
    ret