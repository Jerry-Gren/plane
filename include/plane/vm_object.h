#ifndef PLANE_VM_OBJECT_H
#define PLANE_VM_OBJECT_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Early resident VM object metadata.
 *
 * This is an XNU-like foundation for associating object offsets with
 * resident physical page metadata. It does not own page lifetime and does
 * not allocate or free PMM pages.
 * Resident insert/remove maintains each page's object/offset identity.
 * Resident and wired counts are early accounting only; they are not object
 * references, pager accounting, ledgers, or pageout state.
 *
 * A plane_vm_object must be zero-initialized before its first init call.
 * Resident page storage does not need to be zeroed by callers; init resets it.
 */

struct plane_page;

struct plane_vm_object_page {
	uint64_t offset;
	struct plane_page *page;
	bool used;
};

struct plane_vm_object {
	uint64_t offset_limit;
	uint64_t page_capacity;
	uint64_t resident_page_count;
	uint64_t wired_page_count;
	struct plane_vm_object_page *pages;
	bool initialized;
};

bool plane_vm_object_init(struct plane_vm_object *object,
			  struct plane_vm_object_page *pages,
			  uint64_t page_capacity,
			  uint64_t offset_limit);
bool plane_vm_object_insert_page(struct plane_vm_object *object,
				 uint64_t offset,
				 struct plane_page *page);
struct plane_page *plane_vm_object_lookup_page(struct plane_vm_object *object,
					       uint64_t offset);
struct plane_page *plane_vm_object_remove_page(struct plane_vm_object *object,
					       uint64_t offset);
uint64_t plane_vm_object_resident_page_count(
	const struct plane_vm_object *object);
uint64_t plane_vm_object_wired_page_count(
	const struct plane_vm_object *object);

#endif /* PLANE_VM_OBJECT_H */
