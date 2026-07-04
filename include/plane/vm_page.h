#ifndef PLANE_VM_PAGE_H
#define PLANE_VM_PAGE_H

#include <stdbool.h>
#include <stdint.h>

#include <plane/pmm.h>

/*
 * Early VM page metadata facade.
 *
 * This is the XNU-like layer for resident page metadata queries. It does not
 * allocate physical pages, implement pager state, handle pageout, or provide
 * locking.
 * VM object fields are resident-page metadata only; they do not imply object
 * reference counts, pager state, shadow objects, or pageout behavior.
 */

struct plane_vm_object;

bool plane_vm_page_wire_count(const struct plane_page *page,
			      uint64_t *wire_count);
struct plane_vm_object *plane_vm_page_object(const struct plane_page *page);
bool plane_vm_page_object_offset(const struct plane_page *page,
				 uint64_t *offset);

#endif /* PLANE_VM_PAGE_H */
