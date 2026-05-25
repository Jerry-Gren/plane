#ifndef HAL_X86_64_GDT_H
#define HAL_X86_64_GDT_H

#define SELECTOR_KERNEL_CS 0x08  // 0x08 | 0
#define SELECTOR_KERNEL_DS 0x10  // 0x10 | 0
#define SELECTOR_USER_DS   0x1b  // 0x18 | 3 (rpl=3)
#define SELECTOR_USER_CS   0x23  // 0x20 | 3 (rpl=3)
#define SELECTOR_TSS       0x28  // 0x28 | 0

#define DPL_KERNEL 0
#define DPL_USER   3

/*
 * access byte
 * bit 7: p (present)
 * bit 5-6: dpl (descriptor privilege level)
 * bit 4: s (descriptor type, 0=system, 1=code/data)
 * bit 0-3: type
 */
#define GDT_ACCESS(p, dpl, s, type) \
    (((p) << 7) | (((dpl) & 0x03) << 5) | ((s) << 4) | ((type) & 0x0f))

/*
 * flags byte
 * bit 7: g (granularity, 0=1B, 1=4KB)
 * bit 6: d/b (size, 0=16bit, 1=32bit)
 * bit 5: l (long mode, 1=64bit code)
 * bit 4: avl (available for system)
 */
#define GDT_FLAGS(g, db, l, avl) \
    (((g) << 7) | (((db) & 0x01) << 6) | (((l) & 0x01) << 5) | (((avl) & 0x01) << 4))

/* type, s=1 */
#define TYPE_DATA_RW 0x02 // read/write
#define TYPE_CODE_XR 0x0a // execute/read

/* type, s=0 */
#define TYPE_TSS_AVAILABLE 0x09 // 64 bit tss (available)

#ifndef __ASSEMBLER__

#include <stdint.h>
#include <plane/compiler.h>

struct gdt_descriptor {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;       /* p, dpl, s, type */
    uint8_t  flags_limit;  /* g, d/b, l, avl, seg. limit*/
    uint8_t  base_high;
} __attribute__((packed));

struct tss_descriptor {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  flags_limit;
    uint8_t  base_high;
    uint32_t base_upper32;
    uint32_t reserved;
} __attribute__((packed));

struct tss64 {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

void hal_gdt_init(void);

void hal_tss_set_kernel_stack(uintptr_t stack);

#endif /* __ASSEMBLER__ */

#endif /* HAL_X86_64_GDT_H */