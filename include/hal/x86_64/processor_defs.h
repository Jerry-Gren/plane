#ifndef HAL_X86_64_PROCESSOR_DEFS_H
#define HAL_X86_64_PROCESSOR_DEFS_H

#include <plane/bits.h>

/*
 * x86-64 processor register and PAT definitions used by Plane today.
 *
 * Intel SDM Vol.3 Chapters 2, 3, 10, and 11 plus AMD APM Vol.2 define
 * RFLAGS.ID, CR0/CR4 paging controls, EFER.LME, and PAT memory type fields.
 * Keep this header scoped to the current early long-mode and framebuffer WC
 * path; WP, NX, SMEP/SMAP, PGE, PCID, MTRR, and full cache policy are later
 * milestones.
 */
#define X86_64_RFLAGS_ID BIT(21)

#define X86_64_CR0_PE BIT(0)
#define X86_64_CR0_PG BIT(31)

#define X86_64_CR4_PAE BIT(5)

#define X86_64_EFER_LME BIT(8)

#define X86_64_PAT_MEMORY_WC 0x01
#define X86_64_PAT_ENTRY_BITS 8
#define X86_64_PAT_ENTRY_SHIFT(n) ((n) * X86_64_PAT_ENTRY_BITS)
#ifdef __ASSEMBLER__
#define X86_64_PAT_ENTRY_MASK(n) (0xff << X86_64_PAT_ENTRY_SHIFT(n))
#else
#define X86_64_PAT_ENTRY_MASK(n) (0xffull << X86_64_PAT_ENTRY_SHIFT(n))
#endif

#endif /* HAL_X86_64_PROCESSOR_DEFS_H */
