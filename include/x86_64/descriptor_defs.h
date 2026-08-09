#ifndef X86_64_DESCRIPTOR_DEFS_H
#define X86_64_DESCRIPTOR_DEFS_H

#include <plane/bits.h>

/*
 * x86-64 segment descriptor and TSS definitions used by Plane today.
 *
 * Intel SDM Vol.3 Chapters 3, 7, and 8 plus AMD APM Vol.2 Chapters 4 and
 * 12 define selector encoding, descriptor access/flags fields, and the
 * 64-bit TSS layout. Keep this header scoped to the current GDT/TSS path:
 * IST stack routing, syscall/sysenter state, LDTs, and user-mode activation
 * are later milestones.
 */
#define X86_64_DESC_GDT_MAX_ENTRIES 8192

#define X86_64_DESC_RPL_KERNEL 0
#define X86_64_DESC_RPL_USER   3
#define X86_64_DESC_DPL_KERNEL 0
#define X86_64_DESC_DPL_USER   3

#define X86_64_DESC_SELECTOR(index, rpl) (((index) << 3) | ((rpl) & 0x03))

#define X86_64_DESC_GDT_NULL        0
#define X86_64_DESC_GDT_KERNEL_CODE 1
#define X86_64_DESC_GDT_KERNEL_DATA 2
#define X86_64_DESC_GDT_USER_DATA   3
#define X86_64_DESC_GDT_USER_CODE   4
#define X86_64_DESC_GDT_TSS         5
#define X86_64_DESC_GDT_TSS_HIGH    6
#define X86_64_DESC_GDT_NR          7

#define X86_64_DESC_SELECTOR_KERNEL_CS \
	X86_64_DESC_SELECTOR(X86_64_DESC_GDT_KERNEL_CODE, \
			     X86_64_DESC_RPL_KERNEL)
#define X86_64_DESC_SELECTOR_KERNEL_DS \
	X86_64_DESC_SELECTOR(X86_64_DESC_GDT_KERNEL_DATA, \
			     X86_64_DESC_RPL_KERNEL)
#define X86_64_DESC_SELECTOR_USER_DS \
	X86_64_DESC_SELECTOR(X86_64_DESC_GDT_USER_DATA, X86_64_DESC_RPL_USER)
#define X86_64_DESC_SELECTOR_USER_CS \
	X86_64_DESC_SELECTOR(X86_64_DESC_GDT_USER_CODE, X86_64_DESC_RPL_USER)
#define X86_64_DESC_SELECTOR_TSS \
	X86_64_DESC_SELECTOR(X86_64_DESC_GDT_TSS, X86_64_DESC_RPL_KERNEL)

#define X86_64_DESC_ACCESS_PRESENT BIT(7)
#define X86_64_DESC_ACCESS_DPL_SHIFT 5
#define X86_64_DESC_ACCESS_CODE_DATA BIT(4)
#define X86_64_DESC_ACCESS_TYPE_MASK 0x0f

#define X86_64_DESC_TYPE_DATA_RW 0x02
#define X86_64_DESC_TYPE_CODE_XR 0x0a
#define X86_64_DESC_TYPE_TSS_AVAILABLE 0x09

#define X86_64_DESC_FLAGS_GRAN_4K BIT(7)
#define X86_64_DESC_FLAGS_DEFAULT_BIG BIT(6)
#define X86_64_DESC_FLAGS_LONG_MODE BIT(5)
#define X86_64_DESC_FLAGS_AVAILABLE BIT(4)
#define X86_64_DESC_FLAGS_MASK 0xf0

#ifndef __ASSEMBLER__

#include <stdbool.h>
#include <stdint.h>

#include <plane/compiler.h>

struct x86_64_desc_gdt_entry {
	uint16_t limit_low;
	uint16_t base_low;
	uint8_t base_middle;
	uint8_t access;
	uint8_t flags_limit;
	uint8_t base_high;
} __packed;

struct x86_64_desc_tss_entry {
	uint16_t limit_low;
	uint16_t base_low;
	uint8_t base_middle;
	uint8_t access;
	uint8_t flags_limit;
	uint8_t base_high;
	uint32_t base_upper32;
	uint32_t reserved;
} __packed;

struct x86_64_desc_tss64 {
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
} __packed;

struct x86_64_desc_ptr {
	uint16_t limit;
	uint64_t base;
} __packed;

static inline uint16_t x86_64_desc_selector(uint16_t index, uint8_t rpl)
{
	return X86_64_DESC_SELECTOR(index, rpl);
}

static inline uint8_t x86_64_desc_access(bool present,
					 uint8_t dpl,
					 bool code_data,
					 uint8_t type)
{
	uint8_t access = type & X86_64_DESC_ACCESS_TYPE_MASK;

	if (present) {
		access |= X86_64_DESC_ACCESS_PRESENT;
	}
	if (code_data) {
		access |= X86_64_DESC_ACCESS_CODE_DATA;
	}

	access |= (dpl & 0x03) << X86_64_DESC_ACCESS_DPL_SHIFT;
	return access;
}

static inline uint8_t x86_64_desc_flags(bool granularity_4k,
					bool default_big,
					bool long_mode,
					bool available)
{
	uint8_t flags = 0;

	if (granularity_4k) {
		flags |= X86_64_DESC_FLAGS_GRAN_4K;
	}
	if (default_big) {
		flags |= X86_64_DESC_FLAGS_DEFAULT_BIG;
	}
	if (long_mode) {
		flags |= X86_64_DESC_FLAGS_LONG_MODE;
	}
	if (available) {
		flags |= X86_64_DESC_FLAGS_AVAILABLE;
	}

	return flags;
}

static inline void
x86_64_desc_set_gdt_entry(struct x86_64_desc_gdt_entry *entry,
			  uint32_t base,
			  uint32_t limit,
			  uint8_t access,
			  uint8_t flags)
{
	entry->base_low = base & 0xffff;
	entry->base_middle = (base >> 16) & 0xff;
	entry->base_high = (base >> 24) & 0xff;
	entry->limit_low = limit & 0xffff;
	entry->flags_limit = (flags & X86_64_DESC_FLAGS_MASK) |
			     ((limit >> 16) & 0x0f);
	entry->access = access;
}

static inline void
x86_64_desc_set_tss_entry(struct x86_64_desc_tss_entry *entry,
			  uintptr_t base,
			  uint32_t limit)
{
	x86_64_desc_set_gdt_entry((struct x86_64_desc_gdt_entry *)entry,
				  (uint32_t)base,
				  limit,
				  x86_64_desc_access(
					  true, X86_64_DESC_DPL_KERNEL,
					  false,
					  X86_64_DESC_TYPE_TSS_AVAILABLE),
				  x86_64_desc_flags(false, false, false,
						    false));
	entry->base_upper32 = (uint32_t)(base >> 32);
	entry->reserved = 0;
}

static inline uint64_t
x86_64_desc_tss_entry_base(const struct x86_64_desc_tss_entry *entry)
{
	return (uint64_t)entry->base_low |
	       ((uint64_t)entry->base_middle << 16) |
	       ((uint64_t)entry->base_high << 24) |
	       ((uint64_t)entry->base_upper32 << 32);
}

#endif /* !__ASSEMBLER__ */

#endif /* X86_64_DESCRIPTOR_DEFS_H */
