#ifndef HAL_ARCH_IDT_H
#define HAL_ARCH_IDT_H

#define IDT_MAX_DESCRIPTORS 256

/* idt long mode gate types */
#define IDT_TYPE_INTR64 0xe  /* 64-bit Interrupt Gate, will disable interrupt when entering interrupt */
#define IDT_TYPE_TRAP64 0xf  /* 64-bit Trap Gate, will not disable interrupt when entering interrupt */

/*
 * attributes byte
 * bit 7: p (present)
 * bit 5-6: dpl
 * bit 4: 0 (system segment)
 * bit 0-3: gate type
 */
#define IDT_ATTR(p, dpl, type) \
    (((p) << 7) | (((dpl) & 0x03) << 5) | ((type) & 0x0f))

#ifndef __ASSEMBLER__

#include <stdint.h>
#include <plane/compiler.h>

struct idt_descriptor {
    uint16_t offset_low;      /* offset bits 0..15 */
    uint16_t selector;        /* a code segment selector in gdt */
    uint8_t  ist;             /* bits 0..2 holds interrupt stack table offset, rest of bits 0. */
    uint8_t  attributes;      /* gate type, dpl, and p fields */
    uint16_t offset_middle;   /* offset bits 16..31 */
    uint32_t offset_high;     /* offset bits 32..63 */
    uint32_t reserved;        /* must be 0 */
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

void x86_64_idt_init(void);

#endif /* __ASSEMBLER__ */

#endif /* HAL_ARCH_IDT_H */