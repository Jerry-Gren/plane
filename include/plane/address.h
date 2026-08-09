#ifndef PLANE_ADDRESS_H
#define PLANE_ADDRESS_H

#include <stdbool.h>
#include <stdint.h>

#include <machine/page.h>
#include <plane/overflow.h>

typedef struct {
	uint64_t raw;
} plane_vaddr_t;

typedef struct {
	uint64_t raw;
} plane_paddr_t;

static inline plane_vaddr_t plane_vaddr_make(uint64_t raw)
{
	return (plane_vaddr_t){ .raw = raw };
}

static inline uint64_t plane_vaddr_raw(plane_vaddr_t addr)
{
	return addr.raw;
}

static inline plane_vaddr_t plane_vaddr_from_ptr(const void *ptr)
{
	return plane_vaddr_make((uint64_t)(uintptr_t)ptr);
}

static inline void *plane_vaddr_to_ptr(plane_vaddr_t addr)
{
	return (void *)(uintptr_t)addr.raw;
}

static inline bool plane_vaddr_is_null(plane_vaddr_t addr)
{
	return addr.raw == 0;
}

static inline bool plane_vaddr_is_page_aligned(plane_vaddr_t addr)
{
	return (addr.raw & (ARCH_PAGE_SIZE - 1)) == 0;
}

static inline bool plane_vaddr_add_pages(plane_vaddr_t base,
					 uint64_t page_count,
					 plane_vaddr_t *addr)
{
	uint64_t offset;
	uint64_t raw;

	if (addr == NULL ||
	    !plane_checked_mul_u64(page_count, ARCH_PAGE_SIZE, &offset) ||
	    !plane_checked_add_u64(base.raw, offset, &raw)) {
		return false;
	}

	*addr = plane_vaddr_make(raw);
	return true;
}

static inline plane_paddr_t plane_paddr_make(uint64_t raw)
{
	return (plane_paddr_t){ .raw = raw };
}

static inline uint64_t plane_paddr_raw(plane_paddr_t addr)
{
	return addr.raw;
}

static inline bool plane_paddr_is_null(plane_paddr_t addr)
{
	return addr.raw == 0;
}

static inline bool plane_paddr_is_page_aligned(plane_paddr_t addr)
{
	return (addr.raw & (ARCH_PAGE_SIZE - 1)) == 0;
}

static inline bool plane_paddr_add_pages(plane_paddr_t base,
					 uint64_t page_count,
					 plane_paddr_t *addr)
{
	uint64_t offset;
	uint64_t raw;

	if (addr == NULL ||
	    !plane_checked_mul_u64(page_count, ARCH_PAGE_SIZE, &offset) ||
	    !plane_checked_add_u64(base.raw, offset, &raw)) {
		return false;
	}

	*addr = plane_paddr_make(raw);
	return true;
}

static inline bool plane_paddr_equal(plane_paddr_t lhs, plane_paddr_t rhs)
{
	return lhs.raw == rhs.raw;
}

#endif /* PLANE_ADDRESS_H */
