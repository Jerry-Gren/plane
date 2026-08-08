#ifndef PLANE_VM_OBJECT_H
#define PLANE_VM_OBJECT_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Resident VM object metadata.
 *
 * This is an XNU-like foundation for associating object offsets with
 * resident physical page metadata. It does not own page lifetime and does
 * not allocate or free PMM pages.
 * Resident insert/remove maintains each page's object/offset identity.
 * ref_count is object lifetime ownership only. Resident and wired counts are
 * current page accounting only; they are not object references, pager
 * accounting, ledgers, or pageout state.
 * internal is reserved for the later internal-vs-external object split.
 * allocated marks storage owned by the object metadata zone. Allocate
 * returns one lifetime reference, mirroring XNU's object-zone allocation
 * boundary in Plane's reduced metadata allocator.
 *
 * A plane_vm_object must be zero-initialized before its first init call.
 */

struct plane_page;

struct plane_vm_object {
	uint64_t offset_limit;
	uint64_t ref_count;
	uint64_t resident_page_count;
	uint64_t wired_page_count;
	struct plane_page *resident_head;
	struct plane_page *resident_tail;
	struct plane_page *resident_hint;
	bool alive;
	bool internal;
	bool allocated;
	bool initialized;
};

bool plane_vm_object_allocate(uint64_t offset_limit,
			      struct plane_vm_object **object);
bool plane_vm_object_init(struct plane_vm_object *object,
			  uint64_t offset_limit);
bool plane_vm_object_reference(struct plane_vm_object *object);
bool plane_vm_object_deallocate(struct plane_vm_object *object);
/* Tests whether one lifetime reference can be released. */
bool plane_vm_object_can_deallocate(const struct plane_vm_object *object);
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
uint64_t plane_vm_object_ref_count(const struct plane_vm_object *object);
uint64_t plane_vm_object_offset_limit(const struct plane_vm_object *object);
bool plane_vm_object_is_alive(const struct plane_vm_object *object);

#endif /* PLANE_VM_OBJECT_H */
