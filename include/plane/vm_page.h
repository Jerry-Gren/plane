#ifndef PLANE_VM_PAGE_H
#define PLANE_VM_PAGE_H

#include <stdbool.h>
#include <stdint.h>

#include <plane/address.h>
#include <plane/bits.h>

/*
 * Early VM page metadata facade.
 *
 * This is the XNU-like layer for resident page metadata queries. It does not
 * allocate physical pages, implement pager state, handle pageout, or provide
 * locking.
 * VM object fields are resident-page metadata only; they do not imply object
 * reference counts, pager state, shadow objects, or pageout behavior.
 * plane_vm_page_grab() is the XNU-like entry for resident backing pages;
 * the physical address is a page metadata property, not the primary API.
 * XNU-style busy/wanted wait state is intentionally absent until Plane has
 * object locks, wait/wakeup, fault, and pageout semantics.
 */

struct plane_page;
struct plane_vm_object;

#define PLANE_VM_PAGE_NO_PHYS_RAW UINT64_MAX
#define PLANE_VM_PAGE_GUARD_PHYS_RAW (UINT64_MAX - 1)
#define PLANE_VM_PAGE_NO_PHYS plane_paddr_make(PLANE_VM_PAGE_NO_PHYS_RAW)
#define PLANE_VM_PAGE_GUARD_PHYS plane_paddr_make(PLANE_VM_PAGE_GUARD_PHYS_RAW)
#define PLANE_VM_PAGE_GRAB_ZERO BIT(0)

enum plane_vm_page_state {
	PLANE_VM_PAGE_INVALID = 0,
	PLANE_VM_PAGE_FREE,
	PLANE_VM_PAGE_ALLOCATED,
	PLANE_VM_PAGE_METADATA,
	PLANE_VM_PAGE_GUARD,
};

bool plane_vm_page_grab(uint32_t flags, struct plane_page **page);
bool plane_vm_page_release(struct plane_page *page);
struct plane_page *plane_vm_page_from_phys(plane_paddr_t phys_addr);
plane_paddr_t plane_vm_page_phys(const struct plane_page *page);
enum plane_vm_page_state plane_vm_page_state(const struct plane_page *page);
bool plane_vm_page_wire(struct plane_page *page);
bool plane_vm_page_unwire(struct plane_page *page);
bool plane_vm_page_wire_count(const struct plane_page *page,
			      uint64_t *wire_count);
bool plane_vm_page_is_guard(const struct plane_page *page);
struct plane_vm_object *plane_vm_page_object(const struct plane_page *page);
bool plane_vm_page_object_offset(const struct plane_page *page,
				 uint64_t *offset);

#endif /* PLANE_VM_PAGE_H */
