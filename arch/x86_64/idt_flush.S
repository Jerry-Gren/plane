.global x86_64_idt_flush

.section .text

/*
 * void x86_64_idt_flush(uint64_t idtr_addr);
 * call rule: %rdi = idtr_addr
 */
x86_64_idt_flush:
    lidt (%rdi)
    ret
