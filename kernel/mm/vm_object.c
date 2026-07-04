#include <plane/mm.h>
#include <plane/printk.h>
#include <plane/vm_page.h>
#include <plane/vm_object.h>

#include <stddef.h>

#include "vm_object_internal.h"
#include "vm_page_internal.h"

static bool is_page_aligned(uint64_t value)
{
	return (value & (PAGE_SIZE - 1)) == 0;
}

static bool offset_valid(const struct plane_vm_object *object, uint64_t offset)
{
	return object != NULL &&
	       object->initialized &&
	       is_page_aligned(offset) &&
	       offset < object->offset_limit;
}

static bool page_offset_matches(struct plane_page *page, uint64_t offset)
{
	uint64_t page_offset;

	return plane_vm_page_object_offset(page, &page_offset) &&
	       page_offset == offset;
}

static struct plane_page *find_page(struct plane_vm_object *object,
				    uint64_t offset)
{
	struct plane_page *page;

	if (object->resident_hint != NULL &&
	    plane_vm_page_object(object->resident_hint) == object &&
	    page_offset_matches(object->resident_hint, offset)) {
		return object->resident_hint;
	}

	page = object->resident_head;
	while (page != NULL) {
		if (plane_vm_page_object(page) == object &&
		    page_offset_matches(page, offset)) {
			object->resident_hint = page;
			return page;
		}

		page = plane_vm_page_object_next(page);
	}

	return NULL;
}

static void append_resident_page(struct plane_vm_object *object,
				 struct plane_page *page)
{
	struct plane_page *old_tail = object->resident_tail;

	BUG_ON_MSG(!plane_vm_page_set_object_prev(page, old_tail),
		   "failed to set resident page prev link");
	BUG_ON_MSG(!plane_vm_page_set_object_next(page, NULL),
		   "failed to clear resident page next link");

	if (old_tail != NULL) {
		BUG_ON_MSG(!plane_vm_page_set_object_next(old_tail, page),
			   "failed to append resident page");
	} else {
		object->resident_head = page;
	}

	object->resident_tail = page;
	if (object->resident_hint == NULL) {
		object->resident_hint = page;
	}
}

static void remove_resident_page(struct plane_vm_object *object,
				 struct plane_page *page)
{
	struct plane_page *prev = plane_vm_page_object_prev(page);
	struct plane_page *next = plane_vm_page_object_next(page);

	if (prev != NULL) {
		BUG_ON_MSG(!plane_vm_page_set_object_next(prev, next),
			   "failed to unlink resident page from previous page");
	} else {
		object->resident_head = next;
	}

	if (next != NULL) {
		BUG_ON_MSG(!plane_vm_page_set_object_prev(next, prev),
			   "failed to unlink resident page from next page");
	} else {
		object->resident_tail = prev;
	}

	BUG_ON_MSG(!plane_vm_page_set_object_prev(page, NULL),
		   "failed to clear resident page prev link");
	BUG_ON_MSG(!plane_vm_page_set_object_next(page, NULL),
		   "failed to clear resident page next link");

	if (object->resident_hint == page) {
		object->resident_hint = next != NULL ? next : prev;
	}
}

static bool object_count_valid(const struct plane_vm_object *object)
{
	return object != NULL && object->initialized;
}

bool plane_vm_object_page_became_wired(struct plane_vm_object *object)
{
	if (!object_count_valid(object) ||
	    object->wired_page_count == UINT64_MAX) {
		return false;
	}

	object->wired_page_count++;
	return true;
}

bool plane_vm_object_page_became_unwired(struct plane_vm_object *object)
{
	if (!object_count_valid(object) ||
	    object->wired_page_count == 0) {
		return false;
	}

	object->wired_page_count--;
	return true;
}

bool plane_vm_object_init(struct plane_vm_object *object,
			  uint64_t offset_limit)
{
	if (object == NULL ||
	    object->initialized ||
	    offset_limit == 0 ||
	    !is_page_aligned(offset_limit)) {
		return false;
	}

	*object = (struct plane_vm_object){
		.offset_limit = offset_limit,
		.resident_page_count = 0,
		.wired_page_count = 0,
		.resident_head = NULL,
		.resident_tail = NULL,
		.resident_hint = NULL,
		.initialized = true,
	};
	return true;
}

bool plane_vm_object_insert_page(struct plane_vm_object *object,
				 uint64_t offset,
				 struct plane_page *page)
{
	uint64_t wire_count;

	if (!offset_valid(object, offset) ||
	    page == NULL ||
	    plane_vm_page_state(page) != PLANE_VM_PAGE_ALLOCATED ||
	    plane_vm_page_object(page) != NULL ||
	    !plane_vm_page_wire_count(page, &wire_count) ||
	    object->resident_page_count == UINT64_MAX ||
	    (wire_count != 0 && object->wired_page_count == UINT64_MAX) ||
	    find_page(object, offset) != NULL) {
		return false;
	}
	if (!plane_vm_page_attach_object(page, object, offset)) {
		return false;
	}
	append_resident_page(object, page);
	object->resident_page_count++;
	if (wire_count != 0) {
		object->wired_page_count++;
	}
	return true;
}

struct plane_page *plane_vm_object_lookup_page(struct plane_vm_object *object,
					       uint64_t offset)
{
	if (!offset_valid(object, offset)) {
		return NULL;
	}

	return find_page(object, offset);
}

struct plane_page *plane_vm_object_remove_page(struct plane_vm_object *object,
					       uint64_t offset)
{
	struct plane_page *page;
	uint64_t wire_count;

	if (!offset_valid(object, offset)) {
		return NULL;
	}

	page = find_page(object, offset);
	if (object->resident_page_count == 0 ||
	    page == NULL ||
	    !plane_vm_page_wire_count(page, &wire_count) ||
	    (wire_count != 0 && object->wired_page_count == 0)) {
		return NULL;
	}
	remove_resident_page(object, page);
	BUG_ON_MSG(!plane_vm_page_detach_object(page, object, offset),
		   "failed to detach resident page from object");
	object->resident_page_count--;
	if (wire_count != 0) {
		object->wired_page_count--;
	}
	return page;
}

uint64_t plane_vm_object_resident_page_count(
	const struct plane_vm_object *object)
{
	if (!object_count_valid(object)) {
		return 0;
	}

	return object->resident_page_count;
}

uint64_t plane_vm_object_wired_page_count(
	const struct plane_vm_object *object)
{
	if (!object_count_valid(object)) {
		return 0;
	}

	return object->wired_page_count;
}
