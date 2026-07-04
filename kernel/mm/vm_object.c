#include <plane/mm.h>
#include <plane/printk.h>
#include <plane/compiler.h>
#include <plane/vm_page.h>
#include <plane/vm_object.h>

#include <stddef.h>

#include "vm_object_internal.h"
#include "vm_page_internal.h"

/*
 * Early fixed-size resident hash. XNU sizes vm_page_buckets from managed
 * memory; Plane keeps a small static table until VM metadata allocation grows
 * past early boot constraints.
 */
#define PLANE_VM_OBJECT_RESIDENT_HASH_BUCKETS 256
/*
 * Match XNU's VM_PAGE_HASH_LOOKUP_THRESHOLD direction: for tiny resident
 * sets, a short object-list scan is cheaper than hash lookup machinery.
 */
#define PLANE_VM_OBJECT_HASH_LOOKUP_THRESHOLD 10

static struct plane_page *resident_hash[PLANE_VM_OBJECT_RESIDENT_HASH_BUCKETS];

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

static uint64_t resident_hash_index(const struct plane_vm_object *object,
				    uint64_t offset)
{
	uintptr_t object_key = (uintptr_t)object;
	uint64_t page_key = offset / PAGE_SIZE;

	return (object_key ^ page_key ^ (page_key >> 8)) &
	       (PLANE_VM_OBJECT_RESIDENT_HASH_BUCKETS - 1);
}

static struct plane_page *find_page_in_hash(struct plane_vm_object *object,
					    uint64_t offset)
{
	struct plane_page *page =
		resident_hash[resident_hash_index(object, offset)];

	while (page != NULL) {
		BUG_ON_MSG(!plane_vm_page_object_hashed(page),
			   "resident hash page is not marked hashed");
		BUG_ON_MSG(!plane_vm_page_object_tabled(page),
			   "resident hash page is not marked tabled");
		if (plane_vm_page_object(page) == object &&
		    page_offset_matches(page, offset)) {
			object->resident_hint = page;
			return page;
		}

		page = plane_vm_page_object_hash_next(page);
	}

	return NULL;
}

static struct plane_page *find_page(struct plane_vm_object *object,
				    uint64_t offset)
{
	struct plane_page *page;

	if (object->resident_page_count == 0) {
		BUG_ON_MSG(object->resident_head != NULL ||
			   object->resident_tail != NULL ||
			   object->resident_hint != NULL,
			   "empty object has stale resident links");
		return NULL;
	}

	if (object->resident_hint != NULL) {
		BUG_ON_MSG(!plane_vm_page_object_tabled(object->resident_hint),
			   "resident hint page is not marked tabled");
		BUG_ON_MSG(plane_vm_page_object(object->resident_hint) != object,
			   "resident hint page belongs to another object");
		if (page_offset_matches(object->resident_hint, offset)) {
			return object->resident_hint;
		}

		page = plane_vm_page_object_next(object->resident_hint);
		if (page != NULL) {
			BUG_ON_MSG(!plane_vm_page_object_tabled(page),
				   "resident hint next page is not marked tabled");
			BUG_ON_MSG(plane_vm_page_object(page) != object,
				   "resident hint next page belongs to another object");
			if (page_offset_matches(page, offset)) {
				object->resident_hint = page;
				return page;
			}
		}

		page = plane_vm_page_object_prev(object->resident_hint);
		if (page != NULL) {
			BUG_ON_MSG(!plane_vm_page_object_tabled(page),
				   "resident hint prev page is not marked tabled");
			BUG_ON_MSG(plane_vm_page_object(page) != object,
				   "resident hint prev page belongs to another object");
			if (page_offset_matches(page, offset)) {
				object->resident_hint = page;
				return page;
			}
		}
	}

	if (object->resident_page_count <=
	    PLANE_VM_OBJECT_HASH_LOOKUP_THRESHOLD) {
		page = object->resident_head;
		while (page != NULL) {
			BUG_ON_MSG(!plane_vm_page_object_tabled(page),
				   "resident list page is not marked tabled");
			BUG_ON_MSG(plane_vm_page_object(page) != object,
				   "resident list page belongs to another object");
			if (page_offset_matches(page, offset)) {
				object->resident_hint = page;
				return page;
			}

			page = plane_vm_page_object_next(page);
		}
		return NULL;
	}

	return find_page_in_hash(object, offset);
}

static bool remove_page_from_hash_at(struct plane_vm_object *object,
				     struct plane_page *page,
				     uint64_t offset)
{
	uint64_t index = resident_hash_index(object, offset);
	struct plane_page *current = resident_hash[index];
	struct plane_page *prev = NULL;

	while (current != NULL) {
		struct plane_page *next =
			plane_vm_page_object_hash_next(current);

		BUG_ON_MSG(!plane_vm_page_object_hashed(current),
			   "resident hash page is not marked hashed");
		BUG_ON_MSG(!plane_vm_page_object_tabled(current),
			   "resident hash page is not marked tabled");
		if (current == page) {
			if (prev != NULL) {
				BUG_ON_MSG(!plane_vm_page_set_object_hash_next(prev,
									       next),
					   "failed to unlink resident hash page");
			} else {
				resident_hash[index] = next;
			}
			BUG_ON_MSG(!plane_vm_page_set_object_hash_next(page,
								       NULL),
				   "failed to clear resident hash link");
			return true;
		}

		prev = current;
		current = next;
	}

	return false;
}

static void remove_page_from_hash(struct plane_vm_object *object,
				  struct plane_page *page,
				  uint64_t offset)
{
	BUG_ON_MSG(!plane_vm_page_object_hashed(page),
		   "resident page is not marked hashed");
	BUG_ON_MSG(!remove_page_from_hash_at(object, page, offset),
		   "resident page missing from hash");
	BUG_ON_MSG(!plane_vm_page_set_object_hashed(page, false),
		   "failed to clear resident hash state");
}

static void insert_page_into_hash(struct plane_vm_object *object,
				  struct plane_page *page,
				  uint64_t offset)
{
	uint64_t index = resident_hash_index(object, offset);

	BUG_ON_MSG(plane_vm_page_object(page) != object ||
		   !page_offset_matches(page, offset),
		   "resident page hash insert without object identity");
	BUG_ON_MSG(!plane_vm_page_object_tabled(page),
		   "resident page hash insert before resident list insert");
	BUG_ON_MSG(plane_vm_page_object_hashed(page),
		   "resident page already marked hashed");
	BUG_ON_MSG(plane_vm_page_object_hash_next(page) != NULL,
		   "resident page has stale hash link");
	BUG_ON_MSG(!plane_vm_page_set_object_hash_next(page,
						       resident_hash[index]),
		   "failed to link resident hash page");
	resident_hash[index] = page;
	BUG_ON_MSG(!plane_vm_page_set_object_hashed(page, true),
		   "failed to mark resident page hashed");
}

static void append_resident_page(struct plane_vm_object *object,
				 struct plane_page *page)
{
	struct plane_page *old_tail = object->resident_tail;

	BUG_ON_MSG(plane_vm_page_object(page) != object,
		   "resident page list insert without object identity");
	BUG_ON_MSG(plane_vm_page_object_tabled(page),
		   "resident page already marked tabled");
	BUG_ON_MSG(plane_vm_page_object_hashed(page),
		   "resident page already marked hashed before list insert");
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
	BUG_ON_MSG(!plane_vm_page_set_object_tabled(page, true),
		   "failed to mark resident page tabled");
}

static void remove_resident_page(struct plane_vm_object *object,
				 struct plane_page *page,
				 uint64_t offset)
{
	struct plane_page *prev = plane_vm_page_object_prev(page);
	struct plane_page *next = plane_vm_page_object_next(page);

	BUG_ON_MSG(!plane_vm_page_object_tabled(page),
		   "resident page is not marked tabled");
	remove_page_from_hash(object, page, offset);

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
	BUG_ON_MSG(!plane_vm_page_set_object_tabled(page, false),
		   "failed to clear resident tabled state");

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
	BUILD_BUG_ON(PLANE_VM_OBJECT_RESIDENT_HASH_BUCKETS == 0);
	BUILD_BUG_ON((PLANE_VM_OBJECT_RESIDENT_HASH_BUCKETS &
		      (PLANE_VM_OBJECT_RESIDENT_HASH_BUCKETS - 1)) != 0);

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
	enum plane_vm_page_state page_state;
	uint64_t wire_count;

	page_state = plane_vm_page_state(page);
	if (!offset_valid(object, offset) ||
	    page == NULL ||
	    (page_state != PLANE_VM_PAGE_ALLOCATED &&
	     page_state != PLANE_VM_PAGE_GUARD) ||
	    plane_vm_page_object(page) != NULL ||
	    !plane_vm_page_wire_count(page, &wire_count) ||
	    (page_state == PLANE_VM_PAGE_GUARD && wire_count != 0) ||
	    object->resident_page_count == UINT64_MAX ||
	    (wire_count != 0 && object->wired_page_count == UINT64_MAX) ||
	    find_page(object, offset) != NULL) {
		return false;
	}
	if (!plane_vm_page_attach_object(page, object, offset)) {
		return false;
	}
	append_resident_page(object, page);
	insert_page_into_hash(object, page, offset);
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
	remove_resident_page(object, page, offset);
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
