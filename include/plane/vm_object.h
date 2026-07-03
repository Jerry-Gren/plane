#ifndef PLANE_VM_OBJECT_H
#define PLANE_VM_OBJECT_H

#include <stdbool.h>
#include <stdint.h>

#include <plane/pmm.h>

/*
 * Early resident VM object metadata.
 *
 * This is an XNU-like foundation for associating object offsets with
 * resident physical page metadata. It does not own page lifetime and does
 * not allocate or free PMM pages.
 *
 * A plane_vm_object must be zero-initialized before its first init call.
 * Resident page storage does not need to be zeroed by callers; init resets it.
 */

struct plane_vm_object_page {
	uint64_t offset;
	struct plane_page *page;
	bool used;
};

struct plane_vm_object {
	uint64_t size;
	uint64_t page_capacity;
	struct plane_vm_object_page *pages;
	bool initialized;
};

bool plane_vm_object_init(struct plane_vm_object *object,
			  struct plane_vm_object_page *pages,
			  uint64_t page_capacity,
			  uint64_t size);
bool plane_vm_object_insert_page(struct plane_vm_object *object,
				 uint64_t offset,
				 struct plane_page *page);
struct plane_page *plane_vm_object_lookup_page(struct plane_vm_object *object,
					       uint64_t offset);
struct plane_page *plane_vm_object_remove_page(struct plane_vm_object *object,
					       uint64_t offset);

#endif /* PLANE_VM_OBJECT_H */
