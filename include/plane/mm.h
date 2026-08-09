#ifndef PLANE_MM_H
#define PLANE_MM_H

#include <hal/page.h>

#ifndef __ASSEMBLER__
#include <stdbool.h>
#include <stdint.h>

#include <plane/overflow.h>
#endif

/* must have */
#ifndef ARCH_PAGE_SIZE
	#error "Architecture must define ARCH_PAGE_SIZE!"
#endif
#define PAGE_SIZE       ARCH_PAGE_SIZE
#define PAGE_SHIFT      ARCH_PAGE_SHIFT
#define PAGE_MASK       (~(PAGE_SIZE - 1))

/* optional */
/* large page */
#ifdef ARCH_LARGE_PAGE_SIZE
	#define LARGE_PAGE_SIZE     ARCH_LARGE_PAGE_SIZE
	#define LARGE_PAGE_SHIFT    ARCH_LARGE_PAGE_SHIFT
	#define LARGE_PAGE_MASK     (~(LARGE_PAGE_SIZE - 1))
#endif
/* huge page */
#ifdef ARCH_HUGE_PAGE_SIZE
	#define HUGE_PAGE_SIZE      ARCH_HUGE_PAGE_SIZE
	#define HUGE_PAGE_SHIFT     ARCH_HUGE_PAGE_SHIFT
	#define HUGE_PAGE_MASK      (~(HUGE_PAGE_SIZE - 1))
#endif

#ifndef __ASSEMBLER__
static inline bool plane_addr_is_page_aligned(uint64_t value)
{
	return (value & (PAGE_SIZE - 1)) == 0;
}

static inline bool plane_checked_page_offset(uint64_t page_index,
					     uint64_t *offset)
{
	return plane_checked_mul_u64(page_index, PAGE_SIZE, offset);
}

static inline bool plane_checked_page_range_end(uint64_t base,
						uint64_t page_count,
						uint64_t *end)
{
	uint64_t size;

	if (!plane_checked_page_offset(page_count, &size)) {
		return false;
	}

	return plane_checked_add_u64(base, size, end);
}
#endif /* !__ASSEMBLER__ */

#endif /* PLANE_MM_H */
