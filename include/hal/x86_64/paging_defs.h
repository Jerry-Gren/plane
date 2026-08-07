#ifndef HAL_X86_64_PAGING_DEFS_H
#define HAL_X86_64_PAGING_DEFS_H

#include <hal/x86_64/page.h>
#include <plane/bits.h>

/*
 * x86-64 paging structure bits used by Plane today.
 *
 * Intel SDM Vol.3 Chapter 5 and AMD APM Vol.2 Chapter 5 define the
 * page-table-entry format and the 9-bit index consumed at each paging level.
 * Keep this header focused on fields that current pmap/early-MMU code uses;
 * NX, global, user, accessed/dirty, PAT high bit, PCID, and broader cache
 * policy selection are later paging milestones.
 */
#define X86_64_PAGING_ENTRY_PRESENT BIT(0)
#define X86_64_PAGING_ENTRY_WRITE   BIT(1)
#define X86_64_PAGING_ENTRY_PWT     BIT(3)
#define X86_64_PAGING_ENTRY_PCD     BIT(4)
#define X86_64_PAGING_ENTRY_PS      BIT(7)

#define X86_64_PAGING_TABLE_ENTRIES        512
#define X86_64_PAGING_ENTRY_ADDR_LOW_BIT   12
#define X86_64_PAGING_ENTRY_ADDR_HIGH_BIT  51
#define X86_64_PAGING_INDEX_BITS           9
#define X86_64_PAGING_INDEX_MASK \
	((1 << X86_64_PAGING_INDEX_BITS) - 1)

/* Page table indices for assembly and preprocessor-time range checks. */
#define X86_64_PAGING_PML4_INDEX(vaddr) \
	(((vaddr) >> 39) & X86_64_PAGING_INDEX_MASK)
#define X86_64_PAGING_PDPT_INDEX(vaddr) \
	(((vaddr) >> 30) & X86_64_PAGING_INDEX_MASK)
#define X86_64_PAGING_PD_INDEX(vaddr) \
	(((vaddr) >> 21) & X86_64_PAGING_INDEX_MASK)
#define X86_64_PAGING_PT_INDEX(vaddr) \
	(((vaddr) >> 12) & X86_64_PAGING_INDEX_MASK)

#ifndef __ASSEMBLER__

#include <stdbool.h>
#include <stdint.h>

/* Physical address field in CR3 and page-table entries. */
#define X86_64_PAGING_ENTRY_ADDR_MASK \
	GENMASK_ULL(X86_64_PAGING_ENTRY_ADDR_HIGH_BIT, \
		    X86_64_PAGING_ENTRY_ADDR_LOW_BIT)
static inline uint64_t x86_64_paging_index(uint8_t level, uint64_t vaddr)
{
	switch (level) {
	case 4:
		return X86_64_PAGING_PML4_INDEX(vaddr);
	case 3:
		return X86_64_PAGING_PDPT_INDEX(vaddr);
	case 2:
		return X86_64_PAGING_PD_INDEX(vaddr);
	default:
		return X86_64_PAGING_PT_INDEX(vaddr);
	}
}

static inline bool x86_64_paging_entry_present(uint64_t entry)
{
	return (entry & X86_64_PAGING_ENTRY_PRESENT) != 0;
}

static inline bool x86_64_paging_entry_leaf(uint64_t entry, uint8_t level)
{
	return level == 1 ||
	       (level < 4 && (entry & X86_64_PAGING_ENTRY_PS) != 0);
}

static inline uint64_t x86_64_paging_entry_phys(uint64_t entry)
{
	return entry & X86_64_PAGING_ENTRY_ADDR_MASK;
}

static inline uint64_t x86_64_paging_entry_flags(uint64_t entry)
{
	return entry & ~X86_64_PAGING_ENTRY_ADDR_MASK;
}

static inline uint64_t x86_64_paging_entry_make(uint64_t phys_addr,
						uint64_t flags)
{
	return (phys_addr & X86_64_PAGING_ENTRY_ADDR_MASK) | flags;
}

#endif /* !__ASSEMBLER__ */

#endif /* HAL_X86_64_PAGING_DEFS_H */
