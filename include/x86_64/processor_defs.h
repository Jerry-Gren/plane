#ifndef X86_64_PROCESSOR_DEFS_H
#define X86_64_PROCESSOR_DEFS_H

#include <plane/bits.h>

/*
 * x86-64 processor register definitions used by Plane today.
 *
 * Intel SDM Vol.3 Chapters 2, 3, 10, and 11 plus AMD APM Vol.2 define
 * RFLAGS bits and CR0/CR4 paging controls. Keep this header scoped to the
 * bootstrap long-mode setup and current IRQ-save paths; WP, SMEP/SMAP, PGE,
 * PCID, and broader control-register policy are later milestones.
 */
#define X86_64_RFLAGS_IF BIT(9)
#define X86_64_RFLAGS_ID BIT(21)

#define X86_64_CR0_PE BIT(0)
#define X86_64_CR0_PG BIT(31)

#define X86_64_CR4_PAE BIT(5)

#endif /* X86_64_PROCESSOR_DEFS_H */
