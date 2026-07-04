#include <plane/mm.h>
#include <plane/vm_page.h>
#include <plane/vm_object.h>

#include <stddef.h>

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

static int64_t find_page_index(struct plane_vm_object *object, uint64_t offset)
{
	for (uint64_t i = 0; i < object->page_capacity; i++) {
		if (object->pages[i].used &&
		    object->pages[i].offset == offset) {
			return (int64_t)i;
		}
	}

	return -1;
}

static int64_t find_free_page_index(struct plane_vm_object *object)
{
	for (uint64_t i = 0; i < object->page_capacity; i++) {
		if (!object->pages[i].used) {
			return (int64_t)i;
		}
	}

	return -1;
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
			  struct plane_vm_object_page *pages,
			  uint64_t page_capacity,
			  uint64_t offset_limit)
{
	if (object == NULL ||
	    pages == NULL ||
	    page_capacity == 0 ||
	    object->initialized ||
	    offset_limit == 0 ||
	    !is_page_aligned(offset_limit)) {
		return false;
	}

	for (uint64_t i = 0; i < page_capacity; i++) {
		pages[i] = (struct plane_vm_object_page){0};
	}

	*object = (struct plane_vm_object){
		.offset_limit = offset_limit,
		.page_capacity = page_capacity,
		.resident_page_count = 0,
		.wired_page_count = 0,
		.pages = pages,
		.initialized = true,
	};
	return true;
}

bool plane_vm_object_insert_page(struct plane_vm_object *object,
				 uint64_t offset,
				 struct plane_page *page)
{
	uint64_t wire_count;
	int64_t index;

	if (!offset_valid(object, offset) ||
	    page == NULL ||
	    plane_vm_page_state(page) != PLANE_VM_PAGE_ALLOCATED ||
	    plane_vm_page_object(page) != NULL ||
	    !plane_vm_page_wire_count(page, &wire_count) ||
	    object->resident_page_count == UINT64_MAX ||
	    (wire_count != 0 && object->wired_page_count == UINT64_MAX) ||
	    find_page_index(object, offset) >= 0) {
		return false;
	}

	index = find_free_page_index(object);
	if (index < 0) {
		return false;
	}
	if (!plane_vm_page_attach_object(page, object, offset)) {
		return false;
	}

	object->pages[index].offset = offset;
	object->pages[index].page = page;
	object->pages[index].used = true;
	object->resident_page_count++;
	if (wire_count != 0) {
		object->wired_page_count++;
	}
	return true;
}

struct plane_page *plane_vm_object_lookup_page(struct plane_vm_object *object,
					       uint64_t offset)
{
	int64_t index;

	if (!offset_valid(object, offset)) {
		return NULL;
	}

	index = find_page_index(object, offset);
	if (index < 0) {
		return NULL;
	}

	return object->pages[index].page;
}

struct plane_page *plane_vm_object_remove_page(struct plane_vm_object *object,
					       uint64_t offset)
{
	struct plane_page *page;
	uint64_t wire_count;
	int64_t index;

	if (!offset_valid(object, offset)) {
		return NULL;
	}

	index = find_page_index(object, offset);
	if (index < 0) {
		return NULL;
	}

	page = object->pages[index].page;
	if (object->resident_page_count == 0 ||
	    !plane_vm_page_wire_count(page, &wire_count) ||
	    (wire_count != 0 && object->wired_page_count == 0)) {
		return NULL;
	}
	if (!plane_vm_page_detach_object(page, object, offset)) {
		return NULL;
	}
	object->pages[index] = (struct plane_vm_object_page){0};
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
