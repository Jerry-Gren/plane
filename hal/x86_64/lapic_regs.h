#ifndef HAL_X86_64_LAPIC_REGS_H
#define HAL_X86_64_LAPIC_REGS_H

#include <stdbool.h>
#include <stdint.h>

#include <hal/x86_64/arch_mmu.h>
#include <plane/bits.h>

/*
 * Local APIC register definitions.
 *
 * Intel SDM Vol.3 Chapter 13 and AMD APM Vol.2 Chapter 16 describe the
 * xAPIC MMIO register space as 16-byte slots. The enum values below are
 * register numbers; convert them to MMIO byte offsets with
 * x86_64_lapic_mmio_offset().
 *
 * Keep this file scoped to fields Plane currently uses. Timer, ISR/IRR,
 * ESR, and x2APIC MSR-space details should be added with the feature that
 * consumes them, not preloaded as an APIC constant dump.
 */

#define X86_64_MSR_APIC_BASE 0x1bu

#define X86_64_APIC_BASE_X2APIC BIT_ULL(10)
#define X86_64_APIC_BASE_ENABLE BIT_ULL(11)
#define X86_64_APIC_BASE_ADDR   X86_64_PAGE_ENTRY_ADDR_MASK

enum x86_64_lapic_reg {
	X86_64_LAPIC_REG_ID = 0x02u,
	X86_64_LAPIC_REG_VERSION = 0x03u,
	X86_64_LAPIC_REG_TPR = 0x08u,
	X86_64_LAPIC_REG_EOI = 0x0bu,
	X86_64_LAPIC_REG_SVR = 0x0fu,
	X86_64_LAPIC_REG_ICR_LOW = 0x30u,
	X86_64_LAPIC_REG_ICR_HIGH = 0x31u,
	X86_64_LAPIC_REG_LVT_TIMER = 0x32u,
	X86_64_LAPIC_REG_LVT_THERMAL = 0x33u,
	X86_64_LAPIC_REG_LVT_PERFCNT = 0x34u,
	X86_64_LAPIC_REG_LVT_LINT0 = 0x35u,
	X86_64_LAPIC_REG_LVT_LINT1 = 0x36u,
	X86_64_LAPIC_REG_LVT_ERROR = 0x37u,
};

static inline uint32_t
x86_64_lapic_mmio_offset(enum x86_64_lapic_reg reg)
{
	return ((uint32_t)reg) << 4;
}

#define X86_64_LAPIC_ID_SHIFT            24u
#define X86_64_LAPIC_ID_MASK             0xffu
#define X86_64_LAPIC_VERSION_MASK        0xffu
#define X86_64_LAPIC_MIN_VERSION         0x14u

#define X86_64_LAPIC_SVR_VECTOR_MASK     0xffu
#define X86_64_LAPIC_SVR_ENABLE          BIT(8)
#define X86_64_LAPIC_SPURIOUS_VECTOR     0xffu

#define X86_64_LAPIC_ICR_VECTOR_MASK     0xffu
#define X86_64_LAPIC_ICR_DM_FIXED        0x000u
#define X86_64_LAPIC_ICR_PENDING         BIT(12)
#define X86_64_LAPIC_ICR_DEST_SHIFT      24u

#define X86_64_LAPIC_LVT_MASKED          BIT(16)
#define X86_64_LAPIC_VECTOR_MIN          32u

static inline uint32_t x86_64_lapic_xapic_id(uint32_t id_reg)
{
	return (id_reg >> X86_64_LAPIC_ID_SHIFT) & X86_64_LAPIC_ID_MASK;
}

static inline uint32_t x86_64_lapic_version(uint32_t version_reg)
{
	return version_reg & X86_64_LAPIC_VERSION_MASK;
}

static inline bool x86_64_lapic_version_supported(uint32_t version_reg)
{
	return x86_64_lapic_version(version_reg) >= X86_64_LAPIC_MIN_VERSION;
}

static inline bool x86_64_lapic_external_vector_valid(uint8_t vector)
{
	return vector >= X86_64_LAPIC_VECTOR_MIN;
}

static inline uint32_t x86_64_lapic_svr_enable(uint8_t vector)
{
	return ((uint32_t)vector & X86_64_LAPIC_SVR_VECTOR_MASK) |
	       X86_64_LAPIC_SVR_ENABLE;
}

static inline uint32_t x86_64_lapic_icr_fixed_low(uint8_t vector)
{
	return ((uint32_t)vector & X86_64_LAPIC_ICR_VECTOR_MASK) |
	       X86_64_LAPIC_ICR_DM_FIXED;
}

static inline uint32_t x86_64_lapic_icr_dest_high(uint32_t xapic_id)
{
	return (xapic_id & X86_64_LAPIC_ID_MASK) <<
	       X86_64_LAPIC_ICR_DEST_SHIFT;
}

#endif /* HAL_X86_64_LAPIC_REGS_H */
