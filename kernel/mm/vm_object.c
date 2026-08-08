#include <plane/mm.h>
#include <plane/printk.h>
#include <plane/compiler.h>
#include <plane/spinlock.h>
#include <plane/vm_page.h>
#include <plane/vm_object.h>

#include <stddef.h>

#include "vm_object_internal.h"
#include "vm_page_internal.h"
#include "vm_zone_internal.h"

/*
 * Fixed-size resident hash table. XNU sizes vm_page_buckets from managed
 * memory; Plane keeps a small static table until VM metadata allocation grows
 * enough to replace this startup storage.
 */
#define PLANE_VM_OBJECT_RESIDENT_HASH_BUCKETS 256
/*
 * Match XNU's VM_PAGE_HASH_LOOKUP_THRESHOLD direction: for tiny resident
 * sets, a short object-list scan is cheaper than hash lookup machinery.
 */
#define PLANE_VM_OBJECT_HASH_LOOKUP_THRESHOLD 10
#define PLANE_VM_OBJECT_POOL_SIZE 256

static struct plane_page *bootstrap_resident_hash_buckets[PLANE_VM_OBJECT_RESIDENT_HASH_BUCKETS];
static struct plane_page **resident_hash = bootstrap_resident_hash_buckets;
static uint64_t resident_hash_bucket_count = PLANE_VM_OBJECT_RESIDENT_HASH_BUCKETS;
static struct plane_spinlock resident_hash_lock = PLANE_SPINLOCK_INIT;
static struct plane_vm_object bootstrap_object_pool[PLANE_VM_OBJECT_POOL_SIZE];
static struct plane_vm_zone_segment bootstrap_object_segment;
static struct plane_vm_zone object_zone;
static struct plane_spinlock object_zone_lock = PLANE_SPINLOCK_INIT;

static bool ensure_object_zone_locked(void)
{
	if (object_zone.initialized) {
		return true;
	}

	return plane_vm_zone_init(&object_zone, sizeof(bootstrap_object_pool[0]),
				  bootstrap_object_pool,
				  PLANE_VM_OBJECT_POOL_SIZE,
				  &bootstrap_object_segment);
}

static struct plane_spinlock *object_lock(const struct plane_vm_object *object)
{
	return (struct plane_spinlock *)&object->lock;
}

static plane_irq_state_t lock_object(const struct plane_vm_object *object)
{
	return plane_spin_lock_irqsave(object_lock(object));
}

static void unlock_object(const struct plane_vm_object *object,
			  plane_irq_state_t state)
{
	plane_spin_unlock_irqrestore(object_lock(object), state);
}

static plane_irq_state_t lock_resident_hash(void)
{
	return plane_spin_lock_irqsave(&resident_hash_lock);
}

static void unlock_resident_hash(plane_irq_state_t state)
{
	plane_spin_unlock_irqrestore(&resident_hash_lock, state);
}

static plane_irq_state_t lock_object_zone(void)
{
	return plane_spin_lock_irqsave(&object_zone_lock);
}

static void unlock_object_zone(plane_irq_state_t state)
{
	plane_spin_unlock_irqrestore(&object_zone_lock, state);
}

static bool pointer_array_range(struct plane_page **buckets,
				uint64_t bucket_count,
				uintptr_t *start,
				uintptr_t *end)
{
	uint64_t bytes;
	uintptr_t base = (uintptr_t)buckets;

	if (buckets == NULL ||
	    start == NULL ||
	    end == NULL ||
	    !plane_checked_mul_u64((uint64_t)sizeof(buckets[0]),
				   bucket_count, &bytes) ||
	    bytes > UINTPTR_MAX - base) {
		return false;
	}

	*start = base;
	*end = base + (uintptr_t)bytes;
	return true;
}

static bool pointer_array_ranges_overlap(struct plane_page **first,
					 uint64_t first_count,
					 struct plane_page **second,
					 uint64_t second_count)
{
	uintptr_t first_start;
	uintptr_t first_end;
	uintptr_t second_start;
	uintptr_t second_end;

	if (!pointer_array_range(first, first_count, &first_start, &first_end) ||
	    !pointer_array_range(second, second_count,
				 &second_start, &second_end)) {
		return true;
	}

	return first_start < second_end && second_start < first_end;
}

static bool offset_valid(const struct plane_vm_object *object, uint64_t offset)
{
	return object != NULL &&
	       object->initialized &&
	       object->alive &&
	       plane_is_page_aligned(offset) &&
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
	       (resident_hash_bucket_count - 1);
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
	return object != NULL && object->initialized && object->alive;
}

static bool can_deallocate_locked(const struct plane_vm_object *object)
{
	if (!object_count_valid(object) ||
	    object->ref_count == 0) {
		return false;
	}

	if (object->ref_count > 1) {
		return true;
	}

	return object->resident_page_count == 0 &&
	       object->wired_page_count == 0 &&
	       object->resident_head == NULL &&
	       object->resident_tail == NULL &&
	       object->resident_hint == NULL;
}

bool plane_vm_object_page_became_wired(struct plane_vm_object *object)
{
	plane_irq_state_t state;
	bool wired = false;

	if (object == NULL) {
		return false;
	}

	state = lock_object(object);
	if (!object_count_valid(object) ||
	    object->wired_page_count == UINT64_MAX) {
		goto out;
	}

	object->wired_page_count++;
	wired = true;

out:
	unlock_object(object, state);
	return wired;
}

bool plane_vm_object_page_became_unwired(struct plane_vm_object *object)
{
	plane_irq_state_t state;
	bool unwired = false;

	if (object == NULL) {
		return false;
	}

	state = lock_object(object);
	if (!object_count_valid(object) ||
	    object->wired_page_count == 0) {
		goto out;
	}

	object->wired_page_count--;
	unwired = true;

out:
	unlock_object(object, state);
	return unwired;
}

bool plane_vm_object_add_zone_storage(struct plane_vm_object *storage,
				      uint64_t count,
				      struct plane_vm_zone_segment *segment)
{
	plane_irq_state_t state = lock_object_zone();
	bool added;

	added = ensure_object_zone_locked() &&
		plane_vm_zone_add_storage(&object_zone, storage, count, segment);
	unlock_object_zone(state);
	return added;
}

bool plane_vm_object_rehome_resident_hash(struct plane_page **buckets,
					  uint64_t bucket_count)
{
	struct plane_page **old_hash;
	uint64_t old_bucket_count;
	plane_irq_state_t state;
	bool rehomed = false;

	if (buckets == NULL || !plane_is_power_of_two_u64(bucket_count)) {
		return false;
	}

	state = lock_resident_hash();
	old_hash = resident_hash;
	old_bucket_count = resident_hash_bucket_count;
	if (buckets == resident_hash) {
		rehomed = bucket_count == resident_hash_bucket_count;
		goto out;
	}
	if (pointer_array_ranges_overlap(resident_hash,
					 resident_hash_bucket_count,
					 buckets, bucket_count)) {
		goto out;
	}

	for (uint64_t i = 0; i < bucket_count; i++) {
		buckets[i] = NULL;
	}

	resident_hash = buckets;
	resident_hash_bucket_count = bucket_count;

	for (uint64_t i = 0; i < old_bucket_count; i++) {
		struct plane_page *page = old_hash[i];

		while (page != NULL) {
			struct plane_page *next =
				plane_vm_page_object_hash_next(page);
			struct plane_vm_object *object =
				plane_vm_page_object(page);
			uint64_t offset;
			uint64_t index;

			BUG_ON_MSG(!plane_vm_page_object_offset(page, &offset),
				   "hashed resident page missing object offset");
			index = resident_hash_index(object, offset);
			BUG_ON_MSG(!plane_vm_page_set_object_hash_next(
					   page, resident_hash[index]),
				   "failed to rehash resident page");
			resident_hash[index] = page;
			page = next;
		}
		old_hash[i] = NULL;
	}

	rehomed = true;

out:
	unlock_resident_hash(state);
	return rehomed;
}

void plane_vm_object_reset_bootstrap_for_tests(void)
{
	plane_spin_init(&resident_hash_lock);
	plane_spin_init(&object_zone_lock);
	object_zone = (struct plane_vm_zone){0};
	bootstrap_object_segment = (struct plane_vm_zone_segment){0};
	for (uint64_t i = 0; i < PLANE_VM_OBJECT_POOL_SIZE; i++) {
		bootstrap_object_pool[i] = (struct plane_vm_object){0};
	}
	for (uint64_t i = 0; i < PLANE_VM_OBJECT_RESIDENT_HASH_BUCKETS; i++) {
		bootstrap_resident_hash_buckets[i] = NULL;
	}
	resident_hash = bootstrap_resident_hash_buckets;
	resident_hash_bucket_count = PLANE_VM_OBJECT_RESIDENT_HASH_BUCKETS;
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
	    !plane_is_page_aligned(offset_limit)) {
		return false;
	}

	*object = (struct plane_vm_object){
		.offset_limit = offset_limit,
		.ref_count = 1,
		.resident_page_count = 0,
		.wired_page_count = 0,
		.resident_head = NULL,
		.resident_tail = NULL,
		.resident_hint = NULL,
		.alive = true,
		.internal = true,
		.allocated = false,
		.initialized = true,
	};
	plane_spin_init(&object->lock);
	return true;
}

bool plane_vm_object_allocate(uint64_t offset_limit,
			      struct plane_vm_object **object)
{
	struct plane_vm_object *candidate;
	plane_irq_state_t state;

	if (object == NULL ||
	    offset_limit == 0 ||
	    !plane_is_page_aligned(offset_limit)) {
		return false;
	}

	state = lock_object_zone();
	if (!ensure_object_zone_locked()) {
		unlock_object_zone(state);
		return false;
	}

	candidate = plane_vm_zone_alloc(&object_zone);
	if (candidate == NULL) {
		unlock_object_zone(state);
		return false;
	}

	*candidate = (struct plane_vm_object){
		.offset_limit = offset_limit,
		.ref_count = 1,
		.resident_page_count = 0,
		.wired_page_count = 0,
		.resident_head = NULL,
		.resident_tail = NULL,
		.resident_hint = NULL,
		.alive = true,
		.internal = true,
		.allocated = true,
		.initialized = true,
	};
	plane_spin_init(&candidate->lock);
	unlock_object_zone(state);
	*object = candidate;
	return true;
}

bool plane_vm_object_reference(struct plane_vm_object *object)
{
	plane_irq_state_t state;
	bool referenced = false;

	if (object == NULL) {
		return false;
	}

	state = lock_object(object);
	if (!object_count_valid(object) ||
	    object->ref_count == UINT64_MAX) {
		goto out;
	}

	object->ref_count++;
	referenced = true;

out:
	unlock_object(object, state);
	return referenced;
}

bool plane_vm_object_deallocate(struct plane_vm_object *object)
{
	plane_irq_state_t state;
	bool allocated;
	bool deallocated = false;

	if (object == NULL) {
		return false;
	}

	state = lock_object(object);
	if (!can_deallocate_locked(object)) {
		goto out;
	}

	if (object->ref_count > 1) {
		object->ref_count--;
		deallocated = true;
		goto out;
	}

	if (object->resident_page_count != 0 ||
	    object->wired_page_count != 0 ||
	    object->resident_head != NULL ||
	    object->resident_tail != NULL ||
	    object->resident_hint != NULL) {
		goto out;
	}

	allocated = object->allocated;
	if (!allocated) {
		object->ref_count = 0;
		object->alive = false;
		deallocated = true;
		goto out;
	}

	object->ref_count = 0;
	object->alive = false;
	object->initialized = false;
	unlock_object(object, state);
	state = lock_object_zone();
	deallocated = plane_vm_zone_free(&object_zone, object);
	unlock_object_zone(state);
	BUG_ON_MSG(!deallocated, "failed to free zone-backed VM object");
	return true;

out:
	unlock_object(object, state);
	return deallocated;
}

bool plane_vm_object_can_deallocate(const struct plane_vm_object *object)
{
	plane_irq_state_t state;
	bool can_deallocate;

	if (object == NULL) {
		return false;
	}

	state = lock_object(object);
	can_deallocate = can_deallocate_locked(object);
	unlock_object(object, state);
	return can_deallocate;
}

bool plane_vm_object_insert_page(struct plane_vm_object *object,
				 uint64_t offset,
				 struct plane_page *page)
{
	enum plane_vm_page_state page_state;
	uint64_t wire_count;
	plane_irq_state_t object_state;
	plane_irq_state_t hash_state;
	bool inserted = false;

	page_state = plane_vm_page_state(page);
	if (page == NULL ||
	    (page_state != PLANE_VM_PAGE_ALLOCATED &&
	     page_state != PLANE_VM_PAGE_GUARD) ||
	    plane_vm_page_object(page) != NULL ||
	    !plane_vm_page_wire_count(page, &wire_count) ||
	    (page_state == PLANE_VM_PAGE_GUARD && wire_count != 0) ||
	    object == NULL) {
		return false;
	}

	object_state = lock_object(object);
	hash_state = lock_resident_hash();
	if (!offset_valid(object, offset) ||
	    object->resident_page_count == UINT64_MAX ||
	    (wire_count != 0 && object->wired_page_count == UINT64_MAX) ||
	    find_page(object, offset) != NULL) {
		goto out;
	}
	if (!plane_vm_page_attach_object(page, object, offset)) {
		goto out;
	}
	append_resident_page(object, page);
	insert_page_into_hash(object, page, offset);
	object->resident_page_count++;
	if (wire_count != 0) {
		object->wired_page_count++;
	}
	inserted = true;

out:
	unlock_resident_hash(hash_state);
	unlock_object(object, object_state);
	return inserted;
}

struct plane_page *plane_vm_object_lookup_page(struct plane_vm_object *object,
					       uint64_t offset)
{
	plane_irq_state_t object_state;
	plane_irq_state_t hash_state;
	struct plane_page *page = NULL;

	if (object == NULL) {
		return NULL;
	}

	object_state = lock_object(object);
	hash_state = lock_resident_hash();
	if (!offset_valid(object, offset)) {
		goto out;
	}

	page = find_page(object, offset);

out:
	unlock_resident_hash(hash_state);
	unlock_object(object, object_state);
	return page;
}

struct plane_page *plane_vm_object_remove_page(struct plane_vm_object *object,
					       uint64_t offset)
{
	struct plane_page *page;
	uint64_t wire_count;
	plane_irq_state_t object_state;
	plane_irq_state_t hash_state;

	if (object == NULL) {
		return NULL;
	}

	object_state = lock_object(object);
	hash_state = lock_resident_hash();
	if (!offset_valid(object, offset)) {
		page = NULL;
		goto out;
	}

	page = find_page(object, offset);
	if (object->resident_page_count == 0 ||
	    page == NULL ||
	    !plane_vm_page_wire_count(page, &wire_count) ||
	    (wire_count != 0 && object->wired_page_count == 0)) {
		page = NULL;
		goto out;
	}
	remove_resident_page(object, page, offset);
	BUG_ON_MSG(!plane_vm_page_detach_object(page, object, offset),
		   "failed to detach resident page from object");
	object->resident_page_count--;
	if (wire_count != 0) {
		object->wired_page_count--;
	}

out:
	unlock_resident_hash(hash_state);
	unlock_object(object, object_state);
	return page;
}

uint64_t plane_vm_object_resident_page_count(
	const struct plane_vm_object *object)
{
	plane_irq_state_t state;
	uint64_t count;

	if (object == NULL) {
		return 0;
	}

	state = lock_object(object);
	if (!object_count_valid(object)) {
		unlock_object(object, state);
		return 0;
	}

	count = object->resident_page_count;
	unlock_object(object, state);
	return count;
}

uint64_t plane_vm_object_wired_page_count(
	const struct plane_vm_object *object)
{
	plane_irq_state_t state;
	uint64_t count;

	if (object == NULL) {
		return 0;
	}

	state = lock_object(object);
	if (!object_count_valid(object)) {
		unlock_object(object, state);
		return 0;
	}

	count = object->wired_page_count;
	unlock_object(object, state);
	return count;
}

uint64_t plane_vm_object_ref_count(const struct plane_vm_object *object)
{
	plane_irq_state_t state;
	uint64_t ref_count;

	if (object == NULL) {
		return 0;
	}

	state = lock_object(object);
	ref_count = object->ref_count;
	unlock_object(object, state);
	return ref_count;
}

uint64_t plane_vm_object_offset_limit(const struct plane_vm_object *object)
{
	plane_irq_state_t state;
	uint64_t offset_limit;

	if (object == NULL) {
		return 0;
	}

	state = lock_object(object);
	if (!object_count_valid(object)) {
		unlock_object(object, state);
		return 0;
	}

	offset_limit = object->offset_limit;
	unlock_object(object, state);
	return offset_limit;
}

bool plane_vm_object_is_alive(const struct plane_vm_object *object)
{
	plane_irq_state_t state;
	bool alive;

	if (object == NULL) {
		return false;
	}

	state = lock_object(object);
	alive = object_count_valid(object);
	unlock_object(object, state);
	return alive;
}
