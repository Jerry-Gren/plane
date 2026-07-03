#include <plane/mm.h>
#include <plane/vm_object.h>

#include <stddef.h>

static bool is_page_aligned(uint64_t value)
{
	return (value & (PAGE_SIZE - 1)) == 0;
}

static bool offset_valid(const struct plane_vm_object *object, uint64_t offset)
{
	return object != NULL &&
	       object->initialized &&
	       is_page_aligned(offset) &&
	       offset < object->size;
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

static bool page_is_resident(struct plane_vm_object *object,
			     const struct plane_page *page)
{
	for (uint64_t i = 0; i < object->page_capacity; i++) {
		if (object->pages[i].used &&
		    object->pages[i].page == page) {
			return true;
		}
	}

	return false;
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

bool plane_vm_object_init(struct plane_vm_object *object,
			  struct plane_vm_object_page *pages,
			  uint64_t page_capacity,
			  uint64_t size)
{
	if (object == NULL ||
	    pages == NULL ||
	    page_capacity == 0 ||
	    object->initialized ||
	    size == 0 ||
	    !is_page_aligned(size)) {
		return false;
	}

	for (uint64_t i = 0; i < page_capacity; i++) {
		pages[i] = (struct plane_vm_object_page){0};
	}

	*object = (struct plane_vm_object){
		.size = size,
		.page_capacity = page_capacity,
		.pages = pages,
		.initialized = true,
	};
	return true;
}

bool plane_vm_object_insert_page(struct plane_vm_object *object,
				 uint64_t offset,
				 struct plane_page *page)
{
	int64_t index;

	if (!offset_valid(object, offset) ||
	    page == NULL ||
	    plane_page_state(page) != PLANE_PAGE_ALLOCATED ||
	    page_is_resident(object, page) ||
	    find_page_index(object, offset) >= 0) {
		return false;
	}

	index = find_free_page_index(object);
	if (index < 0) {
		return false;
	}

	object->pages[index].offset = offset;
	object->pages[index].page = page;
	object->pages[index].used = true;
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
	int64_t index;

	if (!offset_valid(object, offset)) {
		return NULL;
	}

	index = find_page_index(object, offset);
	if (index < 0) {
		return NULL;
	}

	page = object->pages[index].page;
	object->pages[index] = (struct plane_vm_object_page){0};
	return page;
}
