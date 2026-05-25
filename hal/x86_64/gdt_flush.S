#include <hal/x86_64/gdt.h>

.global hal_gdt_flush
.global hal_tss_flush

.section .text

/*
 * void hal_gdt_flush(uint64_t gdtr_addr);
 * call rule: %rdi = gdtr_addr
 */
hal_gdt_flush:
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
 * void hal_tss_flush(void);
 */
hal_tss_flush:
    mov $SELECTOR_TSS, %ax
    ltr %ax
    ret