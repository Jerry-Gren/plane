#include <stdint.h>

#include <plane/bits.h>
#include <plane/mm.h>
#include <plane/vm_map.h>
#include <plane/vm_object.h>

#include "support/test.h"

#define TEST_KERNEL_MAP_BASE 0xffff900000000000ull
#define TEST_KERNEL_MAP_PAGES 256
#define TEST_KERNEL_MAP_SIZE (TEST_KERNEL_MAP_PAGES * PAGE_SIZE)
#define TEST_MAP_ENTRIES 128
#define TEST_REHOME_MAP_ENTRIES 160
#define TEST_VM_OBJECT_POOL_SIZE 256

static struct plane_vm_map_entry test_entries[TEST_MAP_ENTRIES];
static struct plane_vm_map_entry rehome_entries[TEST_REHOME_MAP_ENTRIES];
static struct plane_vm_map test_map;
static struct plane_vm_object test_object;
static struct plane_vm_object second_test_object;
static struct plane_vm_object test_object_pool[TEST_VM_OBJECT_POOL_SIZE];

static bool test_lookup_page(struct plane_vm_map *map,
			     uint64_t vaddr,
			     struct plane_vm_map_page_info *info)
{
	return plane_vm_map_lookup_page(map, plane_vaddr_make(vaddr), info);
}

static plane_vaddr_t test_vaddr(uint64_t raw)
{
	return plane_vaddr_make(raw);
}

static uint64_t test_vaddr_raw(plane_vaddr_t vaddr)
{
	return plane_vaddr_raw(vaddr);
}

static bool test_map_delete_range(struct plane_vm_map *map,
				  uint64_t start,
				  uint64_t page_count)
{
	return plane_vm_map_delete_range(map, test_vaddr(start), page_count);
}

static bool test_map_lookup_allocation(
	struct plane_vm_map *map,
	uint64_t vaddr,
	uint64_t page_count,
	struct plane_vm_map_allocation_info *info)
{
	return plane_vm_map_lookup_allocation(map, test_vaddr(vaddr),
					      page_count, info);
}

static bool test_map_protect_pages(struct plane_vm_map *map,
				   uint64_t vaddr,
				   uint64_t page_count,
				   uint32_t prot)
{
	return plane_vm_map_protect_pages(map, test_vaddr(vaddr), page_count,
					  prot);
}

static bool test_map_protect_max_pages(struct plane_vm_map *map,
				       uint64_t vaddr,
				       uint64_t page_count,
				       uint32_t max_prot)
{
	return plane_vm_map_protect_max_pages(map, test_vaddr(vaddr),
					      page_count, max_prot);
}

static bool test_map_wire_pages(struct plane_vm_map *map,
				uint64_t vaddr,
				uint64_t page_count)
{
	return plane_vm_map_wire_pages(map, test_vaddr(vaddr), page_count);
}

static bool test_map_unwire_pages(struct plane_vm_map *map,
				  uint64_t vaddr,
				  uint64_t page_count)
{
	return plane_vm_map_unwire_pages(map, test_vaddr(vaddr), page_count);
}

static bool test_map_free_pages(struct plane_vm_map *map,
				uint64_t vaddr,
				uint64_t page_count)
{
	return plane_vm_map_free_pages(map, test_vaddr(vaddr), page_count);
}

static bool test_map_init(struct plane_vm_map *map,
			  struct plane_vm_map_entry *entries,
			  uint64_t entry_capacity,
			  uint64_t base,
			  uint64_t size)
{
	return plane_vm_map_init(map, entries, entry_capacity, test_vaddr(base),
				 size);
}

bool plane_vm_object_init(struct plane_vm_object *object,
			  uint64_t offset_limit)
{
	if (object == NULL ||
	    object->initialized ||
	    offset_limit == 0 ||
	    !plane_addr_is_page_aligned(offset_limit)) {
		return false;
	}

	*object = (struct plane_vm_object){
		.offset_limit = offset_limit,
		.ref_count = 1,
		.alive = true,
		.internal = true,
		.allocated = false,
		.initialized = true,
	};
	return true;
}

bool plane_vm_object_allocate(uint64_t offset_limit,
			      struct plane_vm_object **object)
{
	if (object == NULL ||
	    offset_limit == 0 ||
	    !plane_addr_is_page_aligned(offset_limit)) {
		return false;
	}

	for (uint64_t i = 0; i < TEST_VM_OBJECT_POOL_SIZE; i++) {
		struct plane_vm_object *candidate = &test_object_pool[i];

		if (candidate->initialized) {
			continue;
		}

		*candidate = (struct plane_vm_object){
			.offset_limit = offset_limit,
			.ref_count = 1,
			.alive = true,
			.internal = true,
			.allocated = true,
			.initialized = true,
		};
		*object = candidate;
		return true;
	}

	return false;
}

bool plane_vm_object_reference(struct plane_vm_object *object)
{
	if (object == NULL ||
	    !object->initialized ||
	    !object->alive ||
	    object->ref_count == UINT64_MAX) {
		return false;
	}

	object->ref_count++;
	return true;
}

bool plane_vm_object_deallocate(struct plane_vm_object *object)
{
	if (!plane_vm_object_can_deallocate(object)) {
		return false;
	}

	if (object->ref_count > 1) {
		object->ref_count--;
		return true;
	}

	if (object->resident_page_count != 0 ||
	    object->wired_page_count != 0) {
		return false;
	}

	if (object->allocated) {
		*object = (struct plane_vm_object){0};
		return true;
	}

	object->ref_count = 0;
	object->alive = false;
	return true;
}

bool plane_vm_object_can_deallocate(const struct plane_vm_object *object)
{
	if (object == NULL ||
	    !object->initialized ||
	    !object->alive ||
	    object->ref_count == 0) {
		return false;
	}

	if (object->ref_count > 1) {
		return true;
	}

	return object->resident_page_count == 0 &&
	       object->wired_page_count == 0;
}

uint64_t plane_vm_object_ref_count(const struct plane_vm_object *object)
{
	if (object == NULL || !object->initialized) {
		return 0;
	}

	return object->ref_count;
}

uint64_t plane_vm_object_offset_limit(const struct plane_vm_object *object)
{
	if (object == NULL ||
	    !object->initialized ||
	    !object->alive) {
		return 0;
	}

	return object->offset_limit;
}

bool plane_vm_object_is_alive(const struct plane_vm_object *object)
{
	return object != NULL && object->initialized && object->alive;
}

static void reset_vm_map_test(void)
{
	test_map = (struct plane_vm_map){0};
	test_object = (struct plane_vm_object){0};
	second_test_object = (struct plane_vm_object){0};
	for (uint64_t i = 0; i < TEST_VM_OBJECT_POOL_SIZE; i++) {
		test_object_pool[i] = (struct plane_vm_object){0};
	}
	for (uint64_t i = 0; i < TEST_MAP_ENTRIES; i++) {
		test_entries[i] = (struct plane_vm_map_entry){0};
	}
	for (uint64_t i = 0; i < TEST_REHOME_MAP_ENTRIES; i++) {
		rehome_entries[i] = (struct plane_vm_map_entry){0};
	}
}

static uint64_t page_vaddr(uint64_t page)
{
	return TEST_KERNEL_MAP_BASE + page * PAGE_SIZE;
}

static bool test_map_enter_pages_protected_max(struct plane_vm_map *map,
					       uint64_t page_count,
					       uint64_t guard_pages,
					       uint32_t prot,
					       uint32_t max_prot,
					       uint64_t *vaddr)
{
	plane_vaddr_t out;
	bool ok;

	if (vaddr == NULL) {
		return false;
	}

	ok = plane_vm_map_enter(
		map,
		&(struct plane_vm_map_enter_options){
			.page_count = page_count,
			.guard_pages = guard_pages,
			.prot = prot,
			.max_prot = max_prot,
			.flags = PLANE_VM_MAP_ENTER_ANYWHERE,
		},
		&out);
	if (ok) {
		*vaddr = test_vaddr_raw(out);
	}
	return ok;
}

static bool test_map_enter_pages_protected(struct plane_vm_map *map,
					   uint64_t page_count,
					   uint64_t guard_pages,
					   uint32_t prot,
					   uint64_t *vaddr)
{
	return test_map_enter_pages_protected_max(map, page_count,
						  guard_pages, prot,
						  PLANE_VM_PROT_ALL, vaddr);
}

static bool test_map_enter_pages(struct plane_vm_map *map,
				 uint64_t page_count,
				 uint64_t *vaddr)
{
	return test_map_enter_pages_protected(map, page_count, 0,
					      PLANE_VM_PROT_READ |
					      PLANE_VM_PROT_WRITE, vaddr);
}

static bool test_map_enter_pages_object(struct plane_vm_map *map,
					uint64_t page_count,
					uint64_t guard_pages,
					struct plane_vm_object *object,
					uint64_t object_offset,
					uint32_t prot,
					uint32_t max_prot,
					uint64_t *vaddr)
{
	plane_vaddr_t out;
	bool ok;

	if (vaddr == NULL) {
		return false;
	}

	ok = plane_vm_map_enter(
		map,
		&(struct plane_vm_map_enter_options){
			.page_count = page_count,
			.guard_pages = guard_pages,
			.object = object,
			.object_offset = object_offset,
			.prot = prot,
			.max_prot = max_prot,
			.flags = PLANE_VM_MAP_ENTER_ANYWHERE,
		},
		&out);
	if (ok) {
		*vaddr = test_vaddr_raw(out);
	}
	return ok;
}

static bool test_map_enter_fixed(uint64_t address,
				 uint64_t page_count,
				 uint64_t guard_pages,
				 struct plane_vm_object *object,
				 uint64_t object_offset,
				 uint32_t flags,
				 uint64_t *vaddr)
{
	plane_vaddr_t out;
	bool ok;

	if (vaddr == NULL) {
		return false;
	}

	ok = plane_vm_map_enter(
		&test_map,
		&(struct plane_vm_map_enter_options){
			.address = test_vaddr(address),
			.page_count = page_count,
			.guard_pages = guard_pages,
			.object = object,
			.object_offset = object_offset,
			.prot = PLANE_VM_PROT_READ | PLANE_VM_PROT_WRITE,
			.max_prot = PLANE_VM_PROT_ALL,
			.flags = flags,
		},
		&out);
	if (ok) {
		*vaddr = test_vaddr_raw(out);
	}
	return ok;
}

static int check_stats(const char *name,
		       uint64_t free_pages,
		       uint64_t reserved_pages,
		       uint64_t user_pages,
		       uint64_t free_range_total,
		       uint64_t allocations)
{
	struct plane_vm_map_stats stats = plane_vm_map_get_stats(&test_map);
	int failures = 0;

	failures += test_expect_u64(name, stats.total_pages,
				    TEST_KERNEL_MAP_PAGES);
	failures += test_expect_u64("kernel map free pages",
				    stats.free_pages, free_pages);
	failures += test_expect_u64("kernel map reserved pages",
				    stats.reserved_pages, reserved_pages);
	failures += test_expect_u64("kernel map user pages",
				    stats.user_pages, user_pages);
	failures += test_expect_u64("kernel map free ranges",
				    stats.free_range_count, free_range_total);
	failures += test_expect_u64("kernel map allocations",
				    stats.allocation_count, allocations);
	return failures;
}

static int check_anonymous_object(const char *name,
				  const struct plane_vm_map_allocation_info *info,
				  uint64_t offset_limit)
{
	int failures = 0;

	failures += test_expect_not_null(name, info->object);
	if (info->object == NULL) {
		return failures;
	}

	failures += test_expect_u64("anonymous object offset",
				    info->object_offset, 0);
	failures += test_expect_u64("anonymous object ref count",
				    plane_vm_object_ref_count(info->object), 1);
	failures += test_expect_u64("anonymous object limit",
				    plane_vm_object_offset_limit(info->object),
				    offset_limit);
	failures += test_expect_bool("anonymous object alive",
				     info->object->alive, true);
	failures += test_expect_bool("anonymous object internal",
				     info->object->internal, true);
	failures += test_expect_bool("anonymous object allocated",
				     info->object->allocated, true);
	return failures;
}

static int test_init_stats(void)
{
	int failures = 0;

	failures += test_expect_bool("map init",
				     test_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     true);
	failures += check_stats("kernel map total pages",
				TEST_KERNEL_MAP_PAGES, 0, 0, 1, 0);
	return failures;
}

static int test_rejects_invalid_init(void)
{
	struct plane_vm_map_stats stats;
	int failures = 0;

	failures += test_expect_bool("zero size init",
				     test_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE,
							   0),
				     false);
	failures += test_expect_bool("unaligned base init",
				     test_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE + 1,
							   TEST_KERNEL_MAP_SIZE),
				     false);
	failures += test_expect_bool("unaligned size init",
				     test_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE - 1),
				     false);
	failures += test_expect_bool("wrapping init",
				     test_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, UINT64_MAX - PAGE_SIZE + 1,
							   2 * PAGE_SIZE),
				     false);

	stats = plane_vm_map_get_stats(&test_map);
	failures += test_expect_u64("invalid init total", stats.total_pages, 0);
	failures += test_expect_u64("invalid init free", stats.free_pages, 0);
	failures += test_expect_u64("invalid init reserved",
				    stats.reserved_pages, 0);
	failures += test_expect_u64("invalid init user", stats.user_pages, 0);
	failures += test_expect_u64("invalid init ranges",
				    stats.free_range_count, 0);
	failures += test_expect_u64("invalid init allocations",
				    stats.allocation_count, 0);
	return failures;
}

static int test_init_is_one_shot_in_production_mode(void)
{
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("oneshot init",
				     test_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("oneshot alloc",
				     test_map_enter_pages(&test_map, 2, &vaddr),
				     true);

	failures += test_expect_bool("oneshot reject valid reinit",
				     test_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     false);
	failures += test_expect_bool("oneshot reject invalid reinit",
				     test_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE,
							   0),
				     false);
	failures += test_expect_bool("oneshot allocation preserved",
				     test_map_lookup_allocation(&test_map, vaddr, 2,
									NULL),
				     true);
	failures += check_stats("oneshot stats preserved",
				TEST_KERNEL_MAP_PAGES - 2, 2, 2, 1, 1);

	return failures;
}

static int test_rehome_entries_rejects_invalid_inputs(void)
{
	struct plane_vm_map_stats before;
	int failures = 0;

	failures += test_expect_bool("rehome null map",
				     plane_vm_map_rehome_entries(
					     NULL, rehome_entries,
					     TEST_REHOME_MAP_ENTRIES),
				     false);
	failures += test_expect_bool("rehome uninitialized map",
				     plane_vm_map_rehome_entries(
					     &test_map, rehome_entries,
					     TEST_REHOME_MAP_ENTRIES),
				     false);
	failures += test_expect_bool("rehome init",
				     test_map_init(&test_map, test_entries,
						       TEST_MAP_ENTRIES,
						       TEST_KERNEL_MAP_BASE,
						       TEST_KERNEL_MAP_SIZE),
				     true);
	before = plane_vm_map_get_stats(&test_map);
	failures += test_expect_bool("rehome null entries",
				     plane_vm_map_rehome_entries(
					     &test_map, NULL,
					     TEST_REHOME_MAP_ENTRIES),
				     false);
	failures += test_expect_bool("rehome shrink capacity",
				     plane_vm_map_rehome_entries(
					     &test_map, rehome_entries,
					     TEST_MAP_ENTRIES - 1),
				     false);
	failures += test_expect_bool("rehome same storage no-op",
				     plane_vm_map_rehome_entries(
					     &test_map, test_entries,
					     TEST_MAP_ENTRIES),
				     true);
	failures += test_expect_bool("rehome overlapping storage",
				     plane_vm_map_rehome_entries(
					     &test_map, &test_entries[1],
					     TEST_MAP_ENTRIES),
				     false);
	failures += test_expect_u64("rehome invalid free unchanged",
				    plane_vm_map_get_stats(&test_map).free_pages,
				    before.free_pages);
	return failures;
}

static int test_rehome_entries_preserves_map_behavior(void)
{
	struct plane_vm_map_allocation_info info = {0};
	struct plane_vm_map_stats before;
	uint64_t first = 0;
	uint64_t second = 0;
	int failures = 0;

	failures += test_expect_bool("rehome behavior init",
				     test_map_init(&test_map, test_entries,
						       TEST_MAP_ENTRIES,
						       TEST_KERNEL_MAP_BASE,
						       TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("rehome behavior enter first",
				     test_map_enter_pages(&test_map, 4, &first),
				     true);
	failures += test_expect_bool("rehome behavior enter second",
				     test_map_enter_pages(&test_map, 2, &second),
				     true);
	before = plane_vm_map_get_stats(&test_map);
	failures += test_expect_bool("rehome behavior move",
				     plane_vm_map_rehome_entries(
					     &test_map, rehome_entries,
					     TEST_REHOME_MAP_ENTRIES),
				     true);
	failures += test_expect_bool("rehome lookup first",
				     test_map_lookup_allocation(
					     &test_map, first, 4, &info),
				     true);
	failures += test_expect_u64("rehome first object ref",
				    plane_vm_object_ref_count(info.object), 1);
	failures += test_expect_bool("rehome protect clipped middle",
				     test_map_protect_pages(
					     &test_map, first + PAGE_SIZE, 2,
					     PLANE_VM_PROT_READ),
				     true);
	failures += test_expect_bool("rehome wire clipped middle",
				     test_map_wire_pages(
					     &test_map, first + PAGE_SIZE, 1),
				     true);
	failures += test_expect_bool("rehome unwire clipped middle",
				     test_map_unwire_pages(
					     &test_map, first + PAGE_SIZE, 1),
				     true);
	failures += test_expect_bool("rehome free second",
				     test_map_free_pages(&test_map,
							     second, 2),
				     true);
	failures += test_expect_u64("rehome reserved after free",
				    plane_vm_map_get_stats(&test_map).reserved_pages,
				    before.reserved_pages - 2);
	return failures;
}

static int test_alloc_and_free_pages(void)
{
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("alloc init",
				     test_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("alloc pages",
				     test_map_enter_pages(&test_map, 2, &vaddr),
				     true);
	failures += test_expect_u64("alloc vaddr", vaddr,
				    TEST_KERNEL_MAP_BASE);
	failures += test_expect_bool("has allocation",
				     test_map_lookup_allocation(&test_map, vaddr, 2,
									NULL),
				     true);
	failures += check_stats("alloc stats", TEST_KERNEL_MAP_PAGES - 2,
				2, 2, 1, 1);

	failures += test_expect_bool("free pages",
				     test_map_free_pages(&test_map, vaddr, 2),
				     true);
	failures += check_stats("free stats", TEST_KERNEL_MAP_PAGES, 0, 0, 1, 0);
	return failures;
}

static int test_anywhere_enter_allocates_anonymous_object(void)
{
	struct plane_vm_map_allocation_info info = {0};
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("anonymous anywhere init",
				     test_map_init(&test_map, test_entries,
						       TEST_MAP_ENTRIES,
						       TEST_KERNEL_MAP_BASE,
						       TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("anonymous anywhere enter",
				     test_map_enter_pages(&test_map, 2, &vaddr),
				     true);
	failures += test_expect_bool("anonymous anywhere lookup",
				     test_map_lookup_allocation(
					     &test_map, vaddr, 2, &info),
				     true);
	failures += check_anonymous_object("anonymous anywhere object",
					   &info, 2 * PAGE_SIZE);
	failures += test_expect_bool("anonymous anywhere free",
				     test_map_free_pages(&test_map, vaddr, 2),
				     true);
	return failures;
}

static int test_fixed_enter_allocates_anonymous_object(void)
{
	struct plane_vm_map_allocation_info info = {0};
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("anonymous fixed init",
				     test_map_init(&test_map, test_entries,
						       TEST_MAP_ENTRIES,
						       TEST_KERNEL_MAP_BASE,
						       TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("anonymous fixed enter",
				     test_map_enter_fixed(
					     page_vaddr(4), 3, 1, NULL, 0,
					     PLANE_VM_MAP_ENTER_FIXED,
					     &vaddr),
				     true);
	failures += test_expect_bool("anonymous fixed lookup",
				     test_map_lookup_allocation(
					     &test_map, vaddr, 3, &info),
				     true);
	failures += check_anonymous_object("anonymous fixed object",
					   &info, 3 * PAGE_SIZE);
	failures += test_expect_u64("anonymous fixed reserved start",
				    test_vaddr_raw(info.reserved_start),
				    page_vaddr(3));
	failures += test_expect_bool("anonymous fixed free",
				     test_map_free_pages(&test_map, vaddr, 3),
				     true);
	return failures;
}

static int test_va_only_enter_reserves_without_object_backing(void)
{
	struct plane_vm_map_allocation_info allocation = {0};
	struct plane_vm_map_page_info page = {0};
	plane_vaddr_t out;
	uint64_t vaddr;
	int failures = 0;

	failures += test_expect_bool("va-only init",
				     test_map_init(&test_map, test_entries,
						       TEST_MAP_ENTRIES,
						       TEST_KERNEL_MAP_BASE,
						       TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool(
		"va-only enter",
		plane_vm_map_enter(
			&test_map,
			&(struct plane_vm_map_enter_options){
				.page_count = 2,
				.prot = PLANE_VM_PROT_READ |
					PLANE_VM_PROT_WRITE,
				.max_prot = PLANE_VM_PROT_READ |
					PLANE_VM_PROT_WRITE,
				.flags = PLANE_VM_MAP_ENTER_ANYWHERE |
					 PLANE_VM_MAP_ENTER_VA_ONLY,
			},
			&out),
		true);
	vaddr = test_vaddr_raw(out);
	failures += test_expect_bool("va-only allocation lookup",
				     test_map_lookup_allocation(&test_map,
								vaddr, 2,
								&allocation),
				     true);
	failures += test_expect_ptr("va-only has no object",
				    allocation.object, NULL);
	failures += test_expect_bool("va-only page lookup fails",
				     test_lookup_page(&test_map, vaddr, &page),
				     false);
	failures += test_expect_bool("va-only free succeeds",
				     test_map_free_pages(&test_map, vaddr, 2),
				     true);

	failures += test_expect_bool("va-only rejects explicit object",
				     plane_vm_object_init(&test_object,
							  PAGE_SIZE),
				     true);
	failures += test_expect_bool(
		"va-only object enter rejected",
		plane_vm_map_enter(
			&test_map,
			&(struct plane_vm_map_enter_options){
				.page_count = 1,
				.object = &test_object,
				.prot = PLANE_VM_PROT_READ,
				.max_prot = PLANE_VM_PROT_READ,
				.flags = PLANE_VM_MAP_ENTER_ANYWHERE |
					 PLANE_VM_MAP_ENTER_VA_ONLY,
			},
			&out),
		false);
	failures += test_expect_u64("explicit object ref unchanged",
				    plane_vm_object_ref_count(&test_object), 1);

	return failures;
}

static int test_lookup_page_uses_user_page_semantics(void)
{
	struct plane_vm_map_page_info info = {0};
	uint64_t guarded = 0;
	uint64_t plain = 0;
	int failures = 0;

	failures += test_expect_bool("lookup page object init",
				     plane_vm_object_init(&test_object,
							  8 * PAGE_SIZE),
				     true);
	failures += test_expect_bool("lookup page init",
				     test_map_init(&test_map, test_entries,
						       TEST_MAP_ENTRIES,
						       TEST_KERNEL_MAP_BASE,
						       TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("lookup guarded enter",
				     test_map_enter_fixed(
					     page_vaddr(4), 2, 1,
					     &test_object, PAGE_SIZE,
					     PLANE_VM_MAP_ENTER_FIXED,
					     &guarded),
				     true);
	failures += test_expect_bool("lookup guard before rejected",
				     test_lookup_page(
					     &test_map, page_vaddr(3), &info),
				     false);
	failures += test_expect_bool("lookup guard after rejected",
				     test_lookup_page(
					     &test_map, page_vaddr(6), &info),
				     false);
	failures += test_expect_bool("lookup guarded user",
				     test_lookup_page(
					     &test_map, guarded + PAGE_SIZE + 17,
					     &info),
				     true);
	failures += test_expect_u64("lookup page vaddr",
				    plane_vaddr_raw(info.page_vaddr), guarded + PAGE_SIZE);
	failures += test_expect_ptr("lookup page object",
				    info.object, &test_object);
	failures += test_expect_u64("lookup page object offset",
				    info.object_offset, 2 * PAGE_SIZE);
	failures += test_expect_u64("lookup page wired",
				    info.wired_count, 0);
	failures += test_expect_u32("lookup page prot",
				    info.prot, PLANE_VM_PROT_DEFAULT);

	failures += test_expect_bool("lookup plain enter",
				     test_map_enter_pages(&test_map, 1,
							  &plain),
				     true);
	failures += test_expect_bool("lookup null info accepted",
				     test_lookup_page(
					     &test_map, plain, NULL),
				     true);
	return failures;
}

static int test_lookup_page_tracks_split_object_offsets(void)
{
	struct plane_vm_map_page_info info = {0};
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("lookup split object init",
				     plane_vm_object_init(&test_object,
							  8 * PAGE_SIZE),
				     true);
	failures += test_expect_bool("lookup split init",
				     test_map_init(&test_map, test_entries,
						       TEST_MAP_ENTRIES,
						       TEST_KERNEL_MAP_BASE,
						       TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("lookup split enter",
				     test_map_enter_pages_object(
					     &test_map, 3, 0, &test_object,
					     PAGE_SIZE, PLANE_VM_PROT_DEFAULT,
					     PLANE_VM_PROT_ALL, &vaddr),
				     true);
	failures += test_expect_bool("lookup split protect middle",
				     test_map_protect_pages(
					     &test_map, vaddr + PAGE_SIZE, 1,
					     PLANE_VM_PROT_READ),
				     true);
	failures += test_expect_bool("lookup split left",
				     test_lookup_page(
					     &test_map, vaddr + 11, &info),
				     true);
	failures += test_expect_u64("lookup split left offset",
				    info.object_offset, PAGE_SIZE);
	failures += test_expect_u32("lookup split left prot",
				    info.prot, PLANE_VM_PROT_DEFAULT);
	failures += test_expect_bool("lookup split middle",
				     test_lookup_page(
					     &test_map,
					     vaddr + PAGE_SIZE + 11,
					     &info),
				     true);
	failures += test_expect_u64("lookup split middle offset",
				    info.object_offset, 2 * PAGE_SIZE);
	failures += test_expect_u32("lookup split middle prot",
				    info.prot, PLANE_VM_PROT_READ);
	failures += test_expect_bool("lookup split right",
				     test_lookup_page(
					     &test_map,
					     vaddr + 2 * PAGE_SIZE + 11,
					     &info),
				     true);
	failures += test_expect_u64("lookup split right offset",
				    info.object_offset, 3 * PAGE_SIZE);
	failures += test_expect_u32("lookup split right prot",
				    info.prot, PLANE_VM_PROT_DEFAULT);
	return failures;
}

static int test_free_releases_anonymous_object_slot(void)
{
	struct plane_vm_map_allocation_info first_info = {0};
	struct plane_vm_map_allocation_info second_info = {0};
	struct plane_vm_object *first_object;
	uint64_t first = 0;
	uint64_t second = 0;
	int failures = 0;

	failures += test_expect_bool("anonymous free init",
				     test_map_init(&test_map, test_entries,
						       TEST_MAP_ENTRIES,
						       TEST_KERNEL_MAP_BASE,
						       TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("anonymous free first",
				     test_map_enter_pages(&test_map, 1, &first),
				     true);
	failures += test_expect_bool("anonymous free first lookup",
				     test_map_lookup_allocation(
					     &test_map, first, 1, &first_info),
				     true);
	first_object = first_info.object;
	failures += test_expect_bool("anonymous free release",
				     test_map_free_pages(&test_map, first, 1),
				     true);
	failures += test_expect_bool("anonymous free second",
				     test_map_enter_pages(&test_map, 1, &second),
				     true);
	failures += test_expect_bool("anonymous free second lookup",
				     test_map_lookup_allocation(
					     &test_map, second, 1, &second_info),
				     true);
	failures += test_expect_ptr("anonymous free reused object slot",
				    second_info.object, first_object);
	failures += test_expect_bool("anonymous free cleanup",
				     test_map_free_pages(&test_map,
							     second, 1),
				     true);
	return failures;
}

static int test_delete_releases_anonymous_object_slot(void)
{
	struct plane_vm_map_allocation_info first_info = {0};
	struct plane_vm_map_allocation_info second_info = {0};
	struct plane_vm_object *first_object;
	uint64_t first = 0;
	uint64_t second = 0;
	int failures = 0;

	failures += test_expect_bool("anonymous delete init",
				     test_map_init(&test_map, test_entries,
						       TEST_MAP_ENTRIES,
						       TEST_KERNEL_MAP_BASE,
						       TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("anonymous delete first",
				     test_map_enter_fixed(
					     page_vaddr(2), 1, 1, NULL, 0,
					     PLANE_VM_MAP_ENTER_FIXED,
					     &first),
				     true);
	failures += test_expect_bool("anonymous delete first lookup",
				     test_map_lookup_allocation(
					     &test_map, first, 1, &first_info),
				     true);
	first_object = first_info.object;
	failures += test_expect_bool("anonymous delete range",
				     test_map_delete_range(&test_map,
							       page_vaddr(1),
							       3),
				     true);
	failures += test_expect_bool("anonymous delete second",
				     test_map_enter_pages(&test_map, 1, &second),
				     true);
	failures += test_expect_bool("anonymous delete second lookup",
				     test_map_lookup_allocation(
					     &test_map, second, 1, &second_info),
				     true);
	failures += test_expect_ptr("anonymous delete reused object slot",
				    second_info.object, first_object);
	return failures;
}

static int test_overwrite_releases_old_anonymous_object_slot(void)
{
	struct plane_vm_map_allocation_info old_info = {0};
	struct plane_vm_map_allocation_info new_info = {0};
	struct plane_vm_map_allocation_info reuse_info = {0};
	struct plane_vm_object *old_object;
	uint64_t old_vaddr = 0;
	uint64_t new_vaddr = 0;
	uint64_t reuse_vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("anonymous overwrite init",
				     test_map_init(&test_map, test_entries,
						       TEST_MAP_ENTRIES,
						       TEST_KERNEL_MAP_BASE,
						       TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("anonymous overwrite old",
				     test_map_enter_fixed(
					     page_vaddr(2), 1, 0, NULL, 0,
					     PLANE_VM_MAP_ENTER_FIXED,
					     &old_vaddr),
				     true);
	failures += test_expect_bool("anonymous overwrite old lookup",
				     test_map_lookup_allocation(
					     &test_map, old_vaddr, 1, &old_info),
				     true);
	old_object = old_info.object;
	failures += test_expect_bool("anonymous overwrite new",
				     test_map_enter_fixed(
					     page_vaddr(2), 2, 0, NULL, 0,
					     PLANE_VM_MAP_ENTER_FIXED |
					     PLANE_VM_MAP_ENTER_OVERWRITE,
					     &new_vaddr),
				     true);
	failures += test_expect_bool("anonymous overwrite new lookup",
				     test_map_lookup_allocation(
					     &test_map, new_vaddr, 2, &new_info),
				     true);
	failures += check_anonymous_object("anonymous overwrite new object",
					   &new_info, 2 * PAGE_SIZE);
	failures += test_expect_bool("anonymous overwrite reuse enter",
				     test_map_enter_fixed(
					     page_vaddr(10), 1, 0, NULL, 0,
					     PLANE_VM_MAP_ENTER_FIXED,
					     &reuse_vaddr),
				     true);
	failures += test_expect_bool("anonymous overwrite reuse lookup",
				     test_map_lookup_allocation(
					     &test_map, reuse_vaddr, 1,
					     &reuse_info),
				     true);
	failures += test_expect_ptr("anonymous overwrite old slot reused",
				    reuse_info.object, old_object);
	return failures;
}

static int test_anonymous_allocation_failure_keeps_map_state(void)
{
	struct plane_vm_object *objects[TEST_VM_OBJECT_POOL_SIZE];
	struct plane_vm_map_stats before;
	struct plane_vm_map_stats after;
	uint64_t vaddr = 0;
	int failures = 0;

	for (uint64_t i = 0; i < TEST_VM_OBJECT_POOL_SIZE; i++) {
		objects[i] = NULL;
		failures += test_expect_bool("anonymous pool fill",
					     plane_vm_object_allocate(
						     PAGE_SIZE, &objects[i]),
					     true);
	}

	failures += test_expect_bool("anonymous exhausted init",
				     test_map_init(&test_map, test_entries,
						       TEST_MAP_ENTRIES,
						       TEST_KERNEL_MAP_BASE,
						       TEST_KERNEL_MAP_SIZE),
				     true);
	before = plane_vm_map_get_stats(&test_map);
	failures += test_expect_bool("anonymous exhausted enter",
				     test_map_enter_pages(&test_map, 1, &vaddr),
				     false);
	after = plane_vm_map_get_stats(&test_map);
	failures += test_expect_u64("anonymous exhausted free unchanged",
				    after.free_pages, before.free_pages);
	failures += test_expect_u64("anonymous exhausted count unchanged",
				    after.allocation_count,
				    before.allocation_count);
	return failures;
}

static int test_rejects_invalid_alloc_and_free(void)
{
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("invalid init",
				     test_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("alloc zero pages",
				     test_map_enter_pages(&test_map, 0, &vaddr),
				     false);
	failures += test_expect_bool("alloc null out",
				     test_map_enter_pages(&test_map, 1, NULL),
				     false);
	failures += test_expect_bool("free zero pages",
				     test_map_free_pages(&test_map, TEST_KERNEL_MAP_BASE,
								 0),
				     false);
	failures += test_expect_bool("free unaligned",
				     test_map_free_pages(&test_map, TEST_KERNEL_MAP_BASE + 1,
								 1),
				     false);

	failures += test_expect_bool("alloc valid",
				     test_map_enter_pages(&test_map, 2, &vaddr),
				     true);
	failures += test_expect_bool("partial free accepted",
				     test_map_free_pages(&test_map, vaddr, 1),
				     true);
	failures += test_expect_bool("partial freed allocation absent",
				     test_map_lookup_allocation(&test_map, vaddr, 1,
									NULL),
				     false);
	failures += test_expect_bool("partial remainder present",
				     test_map_lookup_allocation(
					     &test_map, vaddr + PAGE_SIZE, 1,
					     NULL),
				     true);
	failures += test_expect_bool("partial remainder free accepted",
				     test_map_free_pages(&test_map,
							     vaddr + PAGE_SIZE, 1),
				     true);
	failures += test_expect_bool("double free rejected",
				     test_map_free_pages(&test_map, vaddr, 2),
				     false);
	return failures;
}

static int test_partial_free_clips_object_offsets_and_refs(void)
{
	struct plane_vm_map_allocation_info info = {0};
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("partial object free init",
				     test_map_init(&test_map, test_entries,
						       TEST_MAP_ENTRIES,
						       TEST_KERNEL_MAP_BASE,
						       TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("partial object init",
				     plane_vm_object_init(&test_object,
							  4 * PAGE_SIZE),
				     true);
	failures += test_expect_bool("partial object enter",
				     test_map_enter_pages_object(
					     &test_map, 3, 0, &test_object, 0,
					     PLANE_VM_PROT_DEFAULT,
					     PLANE_VM_PROT_ALL, &vaddr),
				     true);
	failures += test_expect_u64("partial object ref before",
				    plane_vm_object_ref_count(&test_object), 2);
	failures += test_expect_bool("partial object free middle",
				     test_map_free_pages(
					     &test_map, vaddr + PAGE_SIZE, 1),
				     true);
	failures += test_expect_u64("partial object ref after",
				    plane_vm_object_ref_count(&test_object), 3);
	failures += test_expect_bool("partial object left lookup",
				     test_map_lookup_allocation(
					     &test_map, vaddr, 1, &info),
				     true);
	failures += test_expect_ptr("partial object left object",
				    info.object, &test_object);
	failures += test_expect_u64("partial object left offset",
				    info.object_offset, 0);
	failures += test_expect_bool("partial object middle absent",
				     test_map_lookup_allocation(
					     &test_map, vaddr + PAGE_SIZE, 1,
					     NULL),
				     false);
	failures += test_expect_bool("partial object right lookup",
				     test_map_lookup_allocation(
					     &test_map, vaddr + 2 * PAGE_SIZE, 1,
					     &info),
				     true);
	failures += test_expect_ptr("partial object right object",
				    info.object, &test_object);
	failures += test_expect_u64("partial object right offset",
				    info.object_offset, 2 * PAGE_SIZE);
	failures += check_stats("partial object free stats",
				TEST_KERNEL_MAP_PAGES - 2, 2, 2, 2, 2);
	return failures;
}

static int test_partial_free_rejects_exhausted_split_entries(void)
{
	struct plane_vm_map_entry small_entries[2];
	struct plane_vm_map small_map = {0};
	struct plane_vm_map_stats before;
	struct plane_vm_map_stats after;
	uint64_t vaddr = 0;
	int failures = 0;

	for (uint64_t i = 0; i < TEST_ARRAY_SIZE(small_entries); i++) {
		small_entries[i] = (struct plane_vm_map_entry){0};
	}
	failures += test_expect_bool("free split exhausted init",
				     test_map_init(&small_map, small_entries,
						       TEST_ARRAY_SIZE(small_entries),
						       TEST_KERNEL_MAP_BASE,
						       TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("free split exhausted alloc",
				     test_map_enter_pages(&small_map, 3, &vaddr),
				     true);
	before = plane_vm_map_get_stats(&small_map);
	failures += test_expect_bool("free split exhausted rejected",
				     test_map_free_pages(
					     &small_map, vaddr + PAGE_SIZE, 1),
				     false);
	after = plane_vm_map_get_stats(&small_map);
	failures += test_expect_u64("free split exhausted free unchanged",
				    after.free_pages, before.free_pages);
	failures += test_expect_u64("free split exhausted count unchanged",
				    after.allocation_count,
				    before.allocation_count);
	failures += test_expect_bool("free split exhausted preserved",
				     test_map_lookup_allocation(
					     &small_map, vaddr, 3, NULL),
				     true);
	return failures;
}

static int test_rejects_exhausted_vaddr_space(void)
{
	uint64_t vaddr = 0;
	struct plane_vm_map_stats stats;
	int failures = 0;

	failures += test_expect_bool("space init",
				     test_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE,
							   PAGE_SIZE),
				     true);
	failures += test_expect_bool("space alloc too large",
				     test_map_enter_pages(&test_map, 2, &vaddr),
				     false);
	stats = plane_vm_map_get_stats(&test_map);
	failures += test_expect_u64("space total", stats.total_pages, 1);
	failures += test_expect_u64("space free", stats.free_pages, 1);
	failures += test_expect_u64("space reserved", stats.reserved_pages, 0);
	failures += test_expect_u64("space user", stats.user_pages, 0);
	failures += test_expect_u64("space ranges", stats.free_range_count, 1);
	failures += test_expect_u64("space allocations",
				    stats.allocation_count, 0);
	return failures;
}

static int test_rejects_exhausted_entries(void)
{
	uint64_t vaddr = 0;
	struct plane_vm_map_stats before;
	struct plane_vm_map_stats after;
	int failures = 0;

	failures += test_expect_bool("entries init",
				     test_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     true);
	for (uint64_t i = 0; i < TEST_MAP_ENTRIES; i++) {
		failures += test_expect_bool("entry alloc",
					     test_map_enter_pages(&test_map, 1,
									  &vaddr),
					     true);
	}

	before = plane_vm_map_get_stats(&test_map);
	failures += test_expect_bool("entry exhausted",
				     test_map_enter_pages(&test_map, 1, &vaddr),
				     false);
	after = plane_vm_map_get_stats(&test_map);
	failures += test_expect_u64("entry exhausted free unchanged",
				    after.free_pages, before.free_pages);
	failures += test_expect_u64("entry exhausted reserved unchanged",
				    after.reserved_pages, before.reserved_pages);
	failures += test_expect_u64("entry exhausted user unchanged",
				    after.user_pages, before.user_pages);
	failures += test_expect_u64("entry exhausted range unchanged",
				    after.free_range_count, before.free_range_count);
	failures += test_expect_u64("entry exhausted count unchanged",
				    after.allocation_count, before.allocation_count);
	return failures;
}

static int test_first_fit_reuses_lowest_hole(void)
{
	uint64_t first = 0;
	uint64_t second = 0;
	uint64_t reused = 0;
	int failures = 0;

	failures += test_expect_bool("fit init",
				     test_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("fit alloc first",
				     test_map_enter_pages(&test_map, 2, &first),
				     true);
	failures += test_expect_bool("fit alloc second",
				     test_map_enter_pages(&test_map, 2, &second),
				     true);
	failures += test_expect_u64("fit second address", second,
				    page_vaddr(2));
	failures += test_expect_bool("fit free first",
				     test_map_free_pages(&test_map, first, 2),
				     true);
	failures += check_stats("fit hole stats", TEST_KERNEL_MAP_PAGES - 2,
				2, 2, 2, 1);
	failures += test_expect_bool("fit reuse hole",
				     test_map_enter_pages(&test_map, 1, &reused),
				     true);
	failures += test_expect_u64("fit reused lowest hole", reused, first);
	failures += check_stats("fit reused stats", TEST_KERNEL_MAP_PAGES - 3,
				3, 3, 2, 2);
	return failures;
}

static int test_holes_merge_after_entry_removal(void)
{
	uint64_t addrs[5];
	int failures = 0;

	failures += test_expect_bool("merge init",
				     test_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     true);
	for (uint64_t i = 0; i < TEST_ARRAY_SIZE(addrs); i++) {
		failures += test_expect_bool("merge alloc",
					     test_map_enter_pages(&test_map, 1,
									  &addrs[i]),
					     true);
	}

	failures += test_expect_bool("merge free page 1",
				     test_map_free_pages(&test_map, addrs[1], 1),
				     true);
	failures += test_expect_bool("merge free page 3",
				     test_map_free_pages(&test_map, addrs[3], 1),
				     true);
	failures += check_stats("merge separated holes",
				TEST_KERNEL_MAP_PAGES - 3, 3, 3, 3, 3);
	failures += test_expect_bool("merge bridge holes",
				     test_map_free_pages(&test_map, addrs[2], 1),
				     true);
	failures += check_stats("merge bridged holes",
				TEST_KERNEL_MAP_PAGES - 2, 2, 2, 2, 2);
	failures += test_expect_bool("merge with tail hole",
				     test_map_free_pages(&test_map, addrs[4], 1),
				     true);
	failures += check_stats("merge tail hole",
				TEST_KERNEL_MAP_PAGES - 1, 1, 1, 1, 1);
	return failures;
}

static int test_delete_range_removes_single_entry(void)
{
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("delete init",
				     test_map_init(&test_map, test_entries,
						       TEST_MAP_ENTRIES,
						       TEST_KERNEL_MAP_BASE,
						       TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("delete fixed enter",
				     test_map_enter_fixed(
					     page_vaddr(2), 2, 0, NULL, 0,
					     PLANE_VM_MAP_ENTER_FIXED,
					     &vaddr),
				     true);
	failures += test_expect_bool("delete range",
				     test_map_delete_range(&test_map,
							       page_vaddr(2),
							       2),
				     true);
	failures += test_expect_bool("delete lookup gone",
				     test_map_lookup_allocation(
					     &test_map, vaddr, 2, NULL),
				     false);
	failures += check_stats("delete stats", TEST_KERNEL_MAP_PAGES,
				0, 0, 1, 0);
	return failures;
}

static int test_delete_range_removes_multiple_entries(void)
{
	uint64_t first = 0;
	uint64_t second = 0;
	int failures = 0;

	failures += test_expect_bool("delete multi init",
				     test_map_init(&test_map, test_entries,
						       TEST_MAP_ENTRIES,
						       TEST_KERNEL_MAP_BASE,
						       TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("delete multi first",
				     test_map_enter_fixed(
					     page_vaddr(1), 1, 0, NULL, 0,
					     PLANE_VM_MAP_ENTER_FIXED,
					     &first),
				     true);
	failures += test_expect_bool("delete multi second",
				     test_map_enter_fixed(
					     page_vaddr(3), 1, 0, NULL, 0,
					     PLANE_VM_MAP_ENTER_FIXED,
					     &second),
				     true);
	failures += test_expect_bool("delete multi range",
				     test_map_delete_range(&test_map,
							       page_vaddr(0),
							       5),
				     true);
	failures += test_expect_bool("delete multi first gone",
				     test_map_lookup_allocation(
					     &test_map, first, 1, NULL),
				     false);
	failures += test_expect_bool("delete multi second gone",
				     test_map_lookup_allocation(
					     &test_map, second, 1, NULL),
				     false);
	failures += check_stats("delete multi stats",
				TEST_KERNEL_MAP_PAGES, 0, 0, 1, 0);
	return failures;
}

static int test_delete_range_removes_guarded_entry(void)
{
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("delete guard init",
				     test_map_init(&test_map, test_entries,
						       TEST_MAP_ENTRIES,
						       TEST_KERNEL_MAP_BASE,
						       TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("delete guard enter",
				     test_map_enter_pages_protected_max(
					     &test_map, 2, 1,
					     PLANE_VM_PROT_DEFAULT,
					     PLANE_VM_PROT_ALL, &vaddr),
				     true);
	failures += test_expect_u64("delete guard user", vaddr,
				    page_vaddr(1));
	failures += test_expect_bool("delete guard range",
				     test_map_delete_range(&test_map,
							       page_vaddr(0),
							       4),
				     true);
	failures += test_expect_bool("delete guard lookup gone",
				     test_map_lookup_allocation(
					     &test_map, vaddr, 2, NULL),
				     false);
	failures += check_stats("delete guard stats",
				TEST_KERNEL_MAP_PAGES, 0, 0, 1, 0);
	return failures;
}

static int test_delete_range_empty_hole_is_noop(void)
{
	struct plane_vm_map_stats before;
	struct plane_vm_map_stats after;
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("delete noop init",
				     test_map_init(&test_map, test_entries,
						       TEST_MAP_ENTRIES,
						       TEST_KERNEL_MAP_BASE,
						       TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("delete noop allocation",
				     test_map_enter_fixed(
					     page_vaddr(4), 1, 0, NULL, 0,
					     PLANE_VM_MAP_ENTER_FIXED,
					     &vaddr),
				     true);
	before = plane_vm_map_get_stats(&test_map);
	failures += test_expect_bool("delete noop range",
				     test_map_delete_range(&test_map,
							       page_vaddr(1),
							       2),
				     true);
	after = plane_vm_map_get_stats(&test_map);
	failures += test_expect_u64("delete noop free unchanged",
				    after.free_pages, before.free_pages);
	failures += test_expect_u64("delete noop reserved unchanged",
				    after.reserved_pages, before.reserved_pages);
	failures += test_expect_u64("delete noop count unchanged",
				    after.allocation_count,
				    before.allocation_count);
	failures += test_expect_bool("delete noop allocation preserved",
				     test_map_lookup_allocation(
					     &test_map, vaddr, 1, NULL),
				     true);
	return failures;
}

static int test_delete_range_rejects_invalid_ranges(void)
{
	struct plane_vm_map_stats before;
	struct plane_vm_map_stats after;
	int failures = 0;

	failures += test_expect_bool("delete invalid init",
				     test_map_init(&test_map, test_entries,
						       TEST_MAP_ENTRIES,
						       TEST_KERNEL_MAP_BASE,
						       TEST_KERNEL_MAP_SIZE),
				     true);
	before = plane_vm_map_get_stats(&test_map);
	failures += test_expect_bool("delete invalid unaligned",
				     test_map_delete_range(
					     &test_map, TEST_KERNEL_MAP_BASE + 1,
					     1),
				     false);
	failures += test_expect_bool("delete invalid zero",
				     test_map_delete_range(
					     &test_map, TEST_KERNEL_MAP_BASE,
					     0),
				     false);
	failures += test_expect_bool("delete invalid overflow",
				     test_map_delete_range(
					     &test_map,
					     UINT64_MAX - PAGE_SIZE + 1,
					     2),
				     false);
	failures += test_expect_bool("delete invalid out of map",
				     test_map_delete_range(
					     &test_map,
					     page_vaddr(TEST_KERNEL_MAP_PAGES),
					     1),
				     false);
	after = plane_vm_map_get_stats(&test_map);
	failures += test_expect_u64("delete invalid free unchanged",
				    after.free_pages, before.free_pages);
	failures += test_expect_u64("delete invalid count unchanged",
				    after.allocation_count,
				    before.allocation_count);
	return failures;
}

static int test_delete_range_clips_partial_overlap(void)
{
	struct plane_vm_map_stats after;
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("delete partial init",
				     test_map_init(&test_map, test_entries,
						       TEST_MAP_ENTRIES,
						       TEST_KERNEL_MAP_BASE,
						       TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("delete partial enter",
				     test_map_enter_fixed(
					     page_vaddr(2), 2, 0, NULL, 0,
					     PLANE_VM_MAP_ENTER_FIXED,
					     &vaddr),
				     true);
	failures += test_expect_bool("delete partial accepted",
				     test_map_delete_range(&test_map,
							       page_vaddr(3),
							       1),
				     true);
	after = plane_vm_map_get_stats(&test_map);
	failures += test_expect_u64("delete partial free updated",
				    after.free_pages, TEST_KERNEL_MAP_PAGES - 1);
	failures += test_expect_u64("delete partial count clipped",
				    after.allocation_count, 1);
	failures += test_expect_bool("delete partial old absent",
				     test_map_lookup_allocation(
					     &test_map, vaddr, 2, NULL),
				     false);
	failures += test_expect_bool("delete partial remainder preserved",
				     test_map_lookup_allocation(
					     &test_map, vaddr, 1, NULL),
				     true);
	return failures;
}

static int test_delete_range_rejects_guarded_partial_overlap(void)
{
	struct plane_vm_map_stats before;
	struct plane_vm_map_stats after;
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("delete guard partial init",
				     test_map_init(&test_map, test_entries,
						       TEST_MAP_ENTRIES,
						       TEST_KERNEL_MAP_BASE,
						       TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("delete guard partial enter",
				     test_map_enter_pages_protected_max(
					     &test_map, 2, 1,
					     PLANE_VM_PROT_DEFAULT,
					     PLANE_VM_PROT_ALL, &vaddr),
				     true);
	before = plane_vm_map_get_stats(&test_map);
	failures += test_expect_bool("delete guard partial rejected",
				     test_map_delete_range(&test_map, vaddr, 1),
				     false);
	after = plane_vm_map_get_stats(&test_map);
	failures += test_expect_u64("delete guard partial free unchanged",
				    after.free_pages, before.free_pages);
	failures += test_expect_u64("delete guard partial count unchanged",
				    after.allocation_count,
				    before.allocation_count);
	failures += test_expect_bool("delete guard partial preserved",
				     test_map_lookup_allocation(
					     &test_map, vaddr, 2, NULL),
				     true);
	return failures;
}

static int test_delete_range_rejects_wired_entry(void)
{
	struct plane_vm_map_allocation_info info = {0};
	struct plane_vm_map_stats before;
	struct plane_vm_map_stats after;
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("delete wired init",
				     test_map_init(&test_map, test_entries,
						       TEST_MAP_ENTRIES,
						       TEST_KERNEL_MAP_BASE,
						       TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("delete wired enter",
				     test_map_enter_fixed(
					     page_vaddr(2), 2, 0, NULL, 0,
					     PLANE_VM_MAP_ENTER_FIXED,
					     &vaddr),
				     true);
	failures += test_expect_bool("delete wired wire",
				     test_map_wire_pages(&test_map, vaddr, 2),
				     true);
	before = plane_vm_map_get_stats(&test_map);
	failures += test_expect_bool("delete wired rejected",
				     test_map_delete_range(&test_map,
							       page_vaddr(2),
							       2),
				     false);
	after = plane_vm_map_get_stats(&test_map);
	failures += test_expect_u64("delete wired free unchanged",
				    after.free_pages, before.free_pages);
	failures += test_expect_u64("delete wired count unchanged",
				    after.allocation_count,
				    before.allocation_count);
	failures += test_expect_bool("delete wired lookup",
				     test_map_lookup_allocation(
					     &test_map, vaddr, 2, &info),
				     true);
	failures += test_expect_u64("delete wired count", info.wired_count,
				    1);
	return failures;
}

static int test_delete_range_rejects_object_ref_release_failure(void)
{
	struct plane_vm_map_stats before;
	struct plane_vm_map_stats after;
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("delete object init",
				     test_map_init(&test_map, test_entries,
						       TEST_MAP_ENTRIES,
						       TEST_KERNEL_MAP_BASE,
						       TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("delete object object init",
				     plane_vm_object_init(&test_object,
							  TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("delete object enter",
				     test_map_enter_fixed(
					     page_vaddr(2), 2, 0,
					     &test_object, 0,
					     PLANE_VM_MAP_ENTER_FIXED,
					     &vaddr),
				     true);
	failures += test_expect_bool("release anonymous object",
				     plane_vm_object_deallocate(&test_object),
				     true);
	test_object.resident_page_count = 1;
	before = plane_vm_map_get_stats(&test_map);
	failures += test_expect_bool("delete object rejected",
				     test_map_delete_range(&test_map,
							       page_vaddr(2),
							       2),
				     false);
	after = plane_vm_map_get_stats(&test_map);
	failures += test_expect_u64("delete object ref unchanged",
				    plane_vm_object_ref_count(&test_object), 1);
	failures += test_expect_u64("delete object free unchanged",
				    after.free_pages, before.free_pages);
	failures += test_expect_u64("delete object count unchanged",
				    after.allocation_count,
				    before.allocation_count);
	failures += test_expect_bool("delete object allocation preserved",
				     test_map_lookup_allocation(
					     &test_map, vaddr, 2, NULL),
				     true);
	test_object.resident_page_count = 0;
	return failures;
}

static int test_delete_range_releases_object_reference(void)
{
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("delete ref init",
				     test_map_init(&test_map, test_entries,
						       TEST_MAP_ENTRIES,
						       TEST_KERNEL_MAP_BASE,
						       TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("delete ref object init",
				     plane_vm_object_init(&test_object,
							  TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("delete ref enter",
				     test_map_enter_fixed(
					     page_vaddr(2), 2, 0,
					     &test_object, 0,
					     PLANE_VM_MAP_ENTER_FIXED,
					     &vaddr),
				     true);
	failures += test_expect_u64("delete ref held",
				    plane_vm_object_ref_count(&test_object), 2);
	failures += test_expect_bool("delete ref range",
				     test_map_delete_range(&test_map,
							       page_vaddr(2),
							       2),
				     true);
	failures += test_expect_u64("delete ref released",
				    plane_vm_object_ref_count(&test_object), 1);
	failures += test_expect_bool("delete ref lookup gone",
				     test_map_lookup_allocation(
					     &test_map, vaddr, 2, NULL),
				     false);
	failures += check_stats("delete ref stats",
				TEST_KERNEL_MAP_PAGES, 0, 0, 1, 0);
	return failures;
}

static int test_fixed_overwrite_reuses_zapped_entry_slot(void)
{
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("overwrite reuse init",
				     test_map_init(&test_map, test_entries,
						       TEST_MAP_ENTRIES,
						       TEST_KERNEL_MAP_BASE,
						       TEST_KERNEL_MAP_SIZE),
				     true);
	for (uint64_t i = 0; i < TEST_MAP_ENTRIES; i++) {
		failures += test_expect_bool("overwrite reuse fill",
					     test_map_enter_pages(&test_map, 1,
								  &vaddr),
					     true);
	}
	failures += test_expect_bool("overwrite reuse enter",
				     test_map_enter_fixed(
					     TEST_KERNEL_MAP_BASE, 1, 0,
					     NULL, 0,
					     PLANE_VM_MAP_ENTER_FIXED |
					     PLANE_VM_MAP_ENTER_OVERWRITE,
					     &vaddr),
				     true);
	failures += test_expect_u64("overwrite reuse address", vaddr,
				    TEST_KERNEL_MAP_BASE);
	failures += check_stats("overwrite reuse stats",
				TEST_KERNEL_MAP_PAGES - TEST_MAP_ENTRIES,
				TEST_MAP_ENTRIES, TEST_MAP_ENTRIES, 1,
				TEST_MAP_ENTRIES);
	return failures;
}

static int test_fixed_enter_succeeds_in_empty_hole(void)
{
	struct plane_vm_map_allocation_info info = {0};
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("fixed init",
				     test_map_init(&test_map, test_entries,
						       TEST_MAP_ENTRIES,
						       TEST_KERNEL_MAP_BASE,
						       TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("fixed enter",
				     test_map_enter_fixed(
					     page_vaddr(4), 2, 1, NULL, 0,
					     PLANE_VM_MAP_ENTER_FIXED,
					     &vaddr),
				     true);
	failures += test_expect_u64("fixed returned address", vaddr,
				    page_vaddr(4));
	failures += test_expect_bool("fixed lookup",
				     test_map_lookup_allocation(
					     &test_map, vaddr, 2, &info),
				     true);
	failures += test_expect_u64("fixed reserved start",
				    test_vaddr_raw(info.reserved_start),
				    page_vaddr(3));
	failures += test_expect_u64("fixed reserved pages",
				    info.reserved_pages, 4);
	failures += check_stats("fixed stats", TEST_KERNEL_MAP_PAGES - 4,
				4, 2, 2, 1);
	return failures;
}

static int test_fixed_enter_rejects_invalid_ranges(void)
{
	struct plane_vm_map_stats before;
	struct plane_vm_map_stats after;
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("fixed reject init",
				     test_map_init(&test_map, test_entries,
						       TEST_MAP_ENTRIES,
						       TEST_KERNEL_MAP_BASE,
						       TEST_KERNEL_MAP_SIZE),
				     true);
	before = plane_vm_map_get_stats(&test_map);
	failures += test_expect_bool("fixed reject unaligned",
				     test_map_enter_fixed(
					     page_vaddr(1) + 1, 1, 0, NULL,
					     0, PLANE_VM_MAP_ENTER_FIXED,
					     &vaddr),
				     false);
	failures += test_expect_bool("fixed reject guard underflow",
				     test_map_enter_fixed(
					     0, 1, 1, NULL, 0,
					     PLANE_VM_MAP_ENTER_FIXED,
					     &vaddr),
				     false);
	failures += test_expect_bool("fixed reject below map",
				     test_map_enter_fixed(
					     TEST_KERNEL_MAP_BASE, 1, 1, NULL,
					     0, PLANE_VM_MAP_ENTER_FIXED,
					     &vaddr),
				     false);
	failures += test_expect_bool("fixed reject past map",
				     test_map_enter_fixed(
					     page_vaddr(TEST_KERNEL_MAP_PAGES),
					     1, 0, NULL, 0,
					     PLANE_VM_MAP_ENTER_FIXED,
					     &vaddr),
				     false);
	failures += test_expect_bool("fixed reject overwrite alone",
				     test_map_enter_fixed(
					     page_vaddr(1), 1, 0, NULL, 0,
					     PLANE_VM_MAP_ENTER_OVERWRITE,
					     &vaddr),
				     false);
	failures += test_expect_bool("fixed reject anywhere plus fixed",
				     test_map_enter_fixed(
					     page_vaddr(1), 1, 0, NULL, 0,
					     PLANE_VM_MAP_ENTER_ANYWHERE |
					     PLANE_VM_MAP_ENTER_FIXED,
					     &vaddr),
				     false);
	after = plane_vm_map_get_stats(&test_map);
	failures += test_expect_u64("fixed reject free unchanged",
				    after.free_pages, before.free_pages);
	failures += test_expect_u64("fixed reject reserved unchanged",
				    after.reserved_pages, before.reserved_pages);
	failures += test_expect_u64("fixed reject allocations unchanged",
				    after.allocation_count,
				    before.allocation_count);
	return failures;
}

static int test_fixed_enter_rejects_overlap_without_overwrite(void)
{
	struct plane_vm_map_stats before;
	struct plane_vm_map_stats after;
	uint64_t first = 0;
	uint64_t second = 0;
	int failures = 0;

	failures += test_expect_bool("fixed overlap init",
				     test_map_init(&test_map, test_entries,
						       TEST_MAP_ENTRIES,
						       TEST_KERNEL_MAP_BASE,
						       TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("fixed overlap first",
				     test_map_enter_fixed(
					     page_vaddr(2), 2, 0, NULL, 0,
					     PLANE_VM_MAP_ENTER_FIXED,
					     &first),
				     true);
	before = plane_vm_map_get_stats(&test_map);
	failures += test_expect_bool("fixed overlap second",
				     test_map_enter_fixed(
					     page_vaddr(3), 1, 0, NULL, 0,
					     PLANE_VM_MAP_ENTER_FIXED,
					     &second),
				     false);
	after = plane_vm_map_get_stats(&test_map);
	failures += test_expect_u64("fixed overlap free unchanged",
				    after.free_pages, before.free_pages);
	failures += test_expect_u64("fixed overlap count unchanged",
				    after.allocation_count,
				    before.allocation_count);
	failures += test_expect_bool("fixed overlap first preserved",
				     test_map_lookup_allocation(
					     &test_map, first, 2, NULL),
				     true);
	return failures;
}

static int test_fixed_overwrite_replaces_single_entry(void)
{
	struct plane_vm_map_allocation_info info = {0};
	uint64_t old_vaddr = 0;
	uint64_t new_vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("overwrite single init",
				     test_map_init(&test_map, test_entries,
						       TEST_MAP_ENTRIES,
						       TEST_KERNEL_MAP_BASE,
						       TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("overwrite old object init",
				     plane_vm_object_init(
					     &test_object,
					     TEST_KERNEL_MAP_BASE +
					     TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("overwrite new object init",
				     plane_vm_object_init(
					     &second_test_object,
					     TEST_KERNEL_MAP_BASE +
					     TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("overwrite old enter",
				     test_map_enter_fixed(
					     page_vaddr(2), 2, 1,
					     &test_object, 0,
					     PLANE_VM_MAP_ENTER_FIXED,
					     &old_vaddr),
				     true);
	failures += test_expect_u64("overwrite old ref",
				    plane_vm_object_ref_count(&test_object), 2);
	failures += test_expect_bool("overwrite new enter",
				     test_map_enter_fixed(
					     page_vaddr(1), 4, 0,
					     &second_test_object, 0,
					     PLANE_VM_MAP_ENTER_FIXED |
					     PLANE_VM_MAP_ENTER_OVERWRITE,
					     &new_vaddr),
				     true);
	failures += test_expect_u64("overwrite returned address",
				    new_vaddr, page_vaddr(1));
	failures += test_expect_u64("overwrite old ref released",
				    plane_vm_object_ref_count(&test_object), 1);
	failures += test_expect_u64("overwrite new ref held",
				    plane_vm_object_ref_count(
					    &second_test_object),
				    2);
	failures += test_expect_bool("overwrite old lookup gone",
				     test_map_lookup_allocation(
					     &test_map, old_vaddr, 2, NULL),
				     false);
	failures += test_expect_bool("overwrite new lookup",
				     test_map_lookup_allocation(
					     &test_map, new_vaddr, 4, &info),
				     true);
	failures += test_expect_ptr("overwrite new object",
				    info.object, &second_test_object);
	failures += check_stats("overwrite single stats",
				TEST_KERNEL_MAP_PAGES - 4, 4, 4, 2, 1);
	return failures;
}

static int test_fixed_overwrite_replaces_multiple_entries(void)
{
	uint64_t first = 0;
	uint64_t second = 0;
	uint64_t replacement = 0;
	int failures = 0;

	failures += test_expect_bool("overwrite multi init",
				     test_map_init(&test_map, test_entries,
						       TEST_MAP_ENTRIES,
						       TEST_KERNEL_MAP_BASE,
						       TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("overwrite multi first",
				     test_map_enter_fixed(
					     page_vaddr(1), 1, 0, NULL, 0,
					     PLANE_VM_MAP_ENTER_FIXED,
					     &first),
				     true);
	failures += test_expect_bool("overwrite multi second",
				     test_map_enter_fixed(
					     page_vaddr(3), 1, 0, NULL, 0,
					     PLANE_VM_MAP_ENTER_FIXED,
					     &second),
				     true);
	failures += test_expect_bool("overwrite multi replacement",
				     test_map_enter_fixed(
					     page_vaddr(0), 5, 0, NULL, 0,
					     PLANE_VM_MAP_ENTER_FIXED |
					     PLANE_VM_MAP_ENTER_OVERWRITE,
					     &replacement),
				     true);
	failures += test_expect_bool("overwrite multi first gone",
				     test_map_lookup_allocation(
					     &test_map, first, 1, NULL),
				     false);
	failures += test_expect_bool("overwrite multi second gone",
				     test_map_lookup_allocation(
					     &test_map, second, 1, NULL),
				     false);
	failures += test_expect_bool("overwrite multi new lookup",
				     test_map_lookup_allocation(
					     &test_map, replacement, 5, NULL),
				     true);
	failures += check_stats("overwrite multi stats",
				TEST_KERNEL_MAP_PAGES - 5, 5, 5, 1, 1);
	return failures;
}

static int test_fixed_overwrite_rejects_partial_overlap(void)
{
	struct plane_vm_map_stats before;
	struct plane_vm_map_stats after;
	uint64_t old_vaddr = 0;
	uint64_t new_vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("overwrite partial init",
				     test_map_init(&test_map, test_entries,
						       TEST_MAP_ENTRIES,
						       TEST_KERNEL_MAP_BASE,
						       TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("overwrite partial old",
				     test_map_enter_fixed(
					     page_vaddr(2), 2, 0, NULL, 0,
					     PLANE_VM_MAP_ENTER_FIXED,
					     &old_vaddr),
				     true);
	before = plane_vm_map_get_stats(&test_map);
	failures += test_expect_bool("overwrite partial rejected",
				     test_map_enter_fixed(
					     page_vaddr(3), 2, 0, NULL, 0,
					     PLANE_VM_MAP_ENTER_FIXED |
					     PLANE_VM_MAP_ENTER_OVERWRITE,
					     &new_vaddr),
				     false);
	after = plane_vm_map_get_stats(&test_map);
	failures += test_expect_u64("overwrite partial free unchanged",
				    after.free_pages, before.free_pages);
	failures += test_expect_u64("overwrite partial count unchanged",
				    after.allocation_count,
				    before.allocation_count);
	failures += test_expect_bool("overwrite partial old preserved",
				     test_map_lookup_allocation(
					     &test_map, old_vaddr, 2, NULL),
				     true);
	return failures;
}

static int test_fixed_overwrite_rejects_wired_entry(void)
{
	struct plane_vm_map_allocation_info info = {0};
	struct plane_vm_map_stats before;
	struct plane_vm_map_stats after;
	uint64_t old_vaddr = 0;
	uint64_t new_vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("overwrite wired init",
				     test_map_init(&test_map, test_entries,
						       TEST_MAP_ENTRIES,
						       TEST_KERNEL_MAP_BASE,
						       TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("overwrite wired old",
				     test_map_enter_fixed(
					     page_vaddr(2), 2, 0, NULL, 0,
					     PLANE_VM_MAP_ENTER_FIXED,
					     &old_vaddr),
				     true);
	failures += test_expect_bool("overwrite wired wire",
				     test_map_wire_pages(&test_map,
							     old_vaddr, 2),
				     true);
	before = plane_vm_map_get_stats(&test_map);
	failures += test_expect_bool("overwrite wired rejected",
				     test_map_enter_fixed(
					     page_vaddr(2), 2, 0, NULL, 0,
					     PLANE_VM_MAP_ENTER_FIXED |
					     PLANE_VM_MAP_ENTER_OVERWRITE,
					     &new_vaddr),
				     false);
	after = plane_vm_map_get_stats(&test_map);
	failures += test_expect_u64("overwrite wired free unchanged",
				    after.free_pages, before.free_pages);
	failures += test_expect_u64("overwrite wired count unchanged",
				    after.allocation_count,
				    before.allocation_count);
	failures += test_expect_bool("overwrite wired lookup",
				     test_map_lookup_allocation(
					     &test_map, old_vaddr, 2, &info),
				     true);
	failures += test_expect_u64("overwrite wired count", info.wired_count,
				    1);
	return failures;
}

static int test_fixed_overwrite_rejects_object_ref_release_failure(void)
{
	struct plane_vm_map_stats before;
	struct plane_vm_map_stats after;
	uint64_t old_vaddr = 0;
	uint64_t new_vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("overwrite object init",
				     test_map_init(&test_map, test_entries,
						       TEST_MAP_ENTRIES,
						       TEST_KERNEL_MAP_BASE,
						       TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("overwrite object object init",
				     plane_vm_object_init(&test_object,
							  TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("overwrite object old",
				     test_map_enter_fixed(
					     page_vaddr(2), 2, 0,
					     &test_object, 0,
					     PLANE_VM_MAP_ENTER_FIXED,
					     &old_vaddr),
				     true);
	failures += test_expect_bool("release overwritten anonymous object",
				     plane_vm_object_deallocate(&test_object),
				     true);
	test_object.resident_page_count = 1;
	before = plane_vm_map_get_stats(&test_map);
	failures += test_expect_bool("overwrite object rejected",
				     test_map_enter_fixed(
					     page_vaddr(2), 2, 0, NULL, 0,
					     PLANE_VM_MAP_ENTER_FIXED |
					     PLANE_VM_MAP_ENTER_OVERWRITE,
					     &new_vaddr),
				     false);
	after = plane_vm_map_get_stats(&test_map);
	failures += test_expect_u64("overwrite object ref unchanged",
				    plane_vm_object_ref_count(&test_object), 1);
	failures += test_expect_u64("overwrite object free unchanged",
				    after.free_pages, before.free_pages);
	failures += test_expect_u64("overwrite object count unchanged",
				    after.allocation_count,
				    before.allocation_count);
	failures += test_expect_bool("overwrite object old preserved",
				     test_map_lookup_allocation(
					     &test_map, old_vaddr, 2, NULL),
				     true);
	test_object.resident_page_count = 0;
	return failures;
}

static int test_guarded_alloc_reserves_unmapped_sentinels(void)
{
	uint64_t vaddr = 0;
	uint64_t reused = 0;
	int failures = 0;

	failures += test_expect_bool("guard init",
				     test_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("guard alloc",
				     test_map_enter_pages_protected_max(
					     &test_map, 2, 1, PLANE_VM_PROT_DEFAULT,
					     PLANE_VM_PROT_ALL, &vaddr),
				     true);
	failures += test_expect_u64("guard user address", vaddr,
				    page_vaddr(1));
	failures += test_expect_bool("guard has user allocation",
				     test_map_lookup_allocation(&test_map, vaddr, 2,
									NULL),
				     true);
	failures += test_expect_bool("guard base not allocation",
				     test_map_lookup_allocation(&test_map,
					     page_vaddr(0), 1, NULL),
				     false);
	failures += check_stats("guard stats", TEST_KERNEL_MAP_PAGES - 4,
				4, 2, 1, 1);

	failures += test_expect_bool("guard partial free rejected",
				     test_map_free_pages(&test_map, vaddr, 1),
				     false);
	failures += test_expect_bool("guard free",
				     test_map_free_pages(&test_map, vaddr, 2),
				     true);
	failures += check_stats("guard free stats",
				TEST_KERNEL_MAP_PAGES, 0, 0, 1, 0);
	failures += test_expect_bool("guard hole reused",
				     test_map_enter_pages(&test_map, 4, &reused),
				     true);
	failures += test_expect_u64("guard reused reserved hole", reused,
				    TEST_KERNEL_MAP_BASE);
	return failures;
}

static int test_guarded_alloc_rejects_invalid_ranges(void)
{
	uint64_t vaddr = 0;
	struct plane_vm_map_stats before;
	struct plane_vm_map_stats after;
	int failures = 0;

	failures += test_expect_bool("guard reject init",
				     test_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE,
							   2 * PAGE_SIZE),
				     true);
	before = plane_vm_map_get_stats(&test_map);
	failures += test_expect_bool("guard zero user",
				     test_map_enter_pages_protected_max(
					     &test_map, 0, 1, PLANE_VM_PROT_DEFAULT,
					     PLANE_VM_PROT_ALL, &vaddr),
				     false);
	failures += test_expect_bool("guard no room",
				     test_map_enter_pages_protected_max(
					     &test_map, 1, 1, PLANE_VM_PROT_DEFAULT,
					     PLANE_VM_PROT_ALL, &vaddr),
				     false);
	failures += test_expect_bool("guard overflow",
				     test_map_enter_pages_protected_max(
					     &test_map, 1, UINT64_MAX / 2 + 1,
					     PLANE_VM_PROT_DEFAULT,
					     PLANE_VM_PROT_ALL, &vaddr),
				     false);
	after = plane_vm_map_get_stats(&test_map);
	failures += test_expect_u64("guard reject free unchanged",
				    after.free_pages, before.free_pages);
	failures += test_expect_u64("guard reject reserved unchanged",
				    after.reserved_pages, before.reserved_pages);
	failures += test_expect_u64("guard reject user unchanged",
				    after.user_pages, before.user_pages);
	failures += test_expect_u64("guard reject count unchanged",
				    after.allocation_count, before.allocation_count);
	return failures;
}

static int test_protected_alloc_rejects_invalid_protection(void)
{
	uint64_t vaddr = 0;
	struct plane_vm_map_stats before;
	struct plane_vm_map_stats after;
	int failures = 0;

	failures += test_expect_bool("prot init",
				     test_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     true);
	before = plane_vm_map_get_stats(&test_map);
	failures += test_expect_bool(
		"prot rejects none",
		test_map_enter_pages_protected(&test_map, 1, 0, 0, &vaddr),
		false);
	failures += test_expect_bool(
		"prot rejects unknown",
		test_map_enter_pages_protected(&test_map, 1, 0, BIT(8), &vaddr),
		false);
	after = plane_vm_map_get_stats(&test_map);
	failures += test_expect_u64("prot reject free unchanged",
				    after.free_pages, before.free_pages);
	failures += test_expect_u64("prot reject reserved unchanged",
				    after.reserved_pages, before.reserved_pages);
	failures += test_expect_u64("prot reject user unchanged",
				    after.user_pages, before.user_pages);
	failures += test_expect_u64("prot reject count unchanged",
				    after.allocation_count, before.allocation_count);
	return failures;
}

static int test_protected_alloc_accepts_write_only_protection(void)
{
	struct plane_vm_map_allocation_info info = {0};
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("write-only init",
				     test_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool(
		"write-only alloc",
		test_map_enter_pages_protected(&test_map,
			1, 0, PLANE_VM_PROT_WRITE, &vaddr),
		true);
	failures += test_expect_bool(
		"write-only lookup",
		test_map_lookup_allocation(&test_map, vaddr, 1, &info),
		true);
	failures += test_expect_u64("write-only reserved start",
				    test_vaddr_raw(info.reserved_start),
				    TEST_KERNEL_MAP_BASE);
	failures += test_expect_u64("write-only reserved pages",
				    info.reserved_pages, 1);
	failures += test_expect_u64("write-only user start",
				    test_vaddr_raw(info.user_start),
				    TEST_KERNEL_MAP_BASE);
	failures += test_expect_u64("write-only user pages",
				    info.user_pages, 1);
	failures += test_expect_u32("write-only prot",
				    info.prot, PLANE_VM_PROT_WRITE);
	failures += test_expect_u32("write-only max prot",
				    info.max_prot,
				    PLANE_VM_PROT_ALL);
	return failures;
}

static int test_protected_guarded_alloc_keeps_user_range_semantics(void)
{
	struct plane_vm_map_allocation_info info = {0};
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("prot guard init",
				     test_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool(
		"prot guard alloc",
		test_map_enter_pages_protected(&test_map,
			2, 1, PLANE_VM_PROT_READ, &vaddr),
		true);
	failures += test_expect_u64("prot guard user address", vaddr,
				    page_vaddr(1));
	failures += test_expect_bool("prot guard has user allocation",
				     test_map_lookup_allocation(&test_map, vaddr, 2,
									NULL),
				     true);
	failures += test_expect_bool("prot guard base not allocation",
				     test_map_lookup_allocation(&test_map,
					     page_vaddr(0), 1, NULL),
				     false);
	failures += test_expect_bool(
		"prot guard lookup",
		test_map_lookup_allocation(&test_map, vaddr, 2, &info),
		true);
	failures += test_expect_u64("prot guard reserved start",
				    test_vaddr_raw(info.reserved_start),
				    TEST_KERNEL_MAP_BASE);
	failures += test_expect_u64("prot guard reserved pages",
				    info.reserved_pages, 4);
	failures += test_expect_u64("prot guard user start",
				    test_vaddr_raw(info.user_start),
				    page_vaddr(1));
	failures += test_expect_u64("prot guard user pages",
				    info.user_pages, 2);
	failures += test_expect_u32("prot guard prot",
				    info.prot, PLANE_VM_PROT_READ);
	failures += test_expect_u32("prot guard max prot",
				    info.max_prot,
				    PLANE_VM_PROT_ALL);
	failures += check_stats("prot guard stats",
				TEST_KERNEL_MAP_PAGES - 4, 4, 2, 1, 1);
	failures += test_expect_bool("prot guard free",
				     test_map_free_pages(&test_map, vaddr, 2),
				     true);
	failures += check_stats("prot guard free stats",
				TEST_KERNEL_MAP_PAGES, 0, 0, 1, 0);
	return failures;
}

static int test_protect_pages_updates_exact_allocation(void)
{
	struct plane_vm_map_allocation_info info = {0};
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("protect init",
				     test_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("protect alloc",
				     test_map_enter_pages(&test_map, 2, &vaddr),
				     true);
	failures += test_expect_bool("protect readonly",
				     test_map_protect_pages(&test_map,
					     vaddr, 2, PLANE_VM_PROT_READ),
				     true);
	failures += test_expect_bool("protect lookup readonly",
				     test_map_lookup_allocation(&test_map, vaddr,
								       2,
								       &info),
				     true);
	failures += test_expect_u32("protect readonly prot",
				    info.prot, PLANE_VM_PROT_READ);
	failures += test_expect_u32("protect readonly max",
				    info.max_prot, PLANE_VM_PROT_ALL);
	failures += test_expect_bool("protect writable again",
				     test_map_protect_pages(&test_map,
					     vaddr, 2, PLANE_VM_PROT_DEFAULT),
				     true);
	failures += test_expect_bool("protect lookup writable",
				     test_map_lookup_allocation(&test_map, vaddr,
								       2,
								       &info),
				     true);
	failures += test_expect_u32("protect writable prot",
				    info.prot, PLANE_VM_PROT_DEFAULT);
	return failures;
}

static int test_protect_pages_clips_middle_fragment(void)
{
	struct plane_vm_map_allocation_info info = {0};
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("protect middle init",
				     test_map_init(&test_map, test_entries,
						       TEST_MAP_ENTRIES,
						       TEST_KERNEL_MAP_BASE,
						       TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("protect middle alloc",
				     test_map_enter_pages(&test_map, 3, &vaddr),
				     true);
	failures += test_expect_bool("protect middle",
				     test_map_protect_pages(
					     &test_map, vaddr + PAGE_SIZE, 1,
					     PLANE_VM_PROT_READ),
				     true);
	failures += test_expect_bool("protect left lookup",
				     test_map_lookup_allocation(
					     &test_map, vaddr, 1, &info),
				     true);
	failures += test_expect_u32("protect left unchanged",
				    info.prot, PLANE_VM_PROT_DEFAULT);
	failures += test_expect_bool("protect middle lookup",
				     test_map_lookup_allocation(
					     &test_map, vaddr + PAGE_SIZE, 1,
					     &info),
				     true);
	failures += test_expect_u32("protect middle readonly",
				    info.prot, PLANE_VM_PROT_READ);
	failures += test_expect_bool("protect right lookup",
				     test_map_lookup_allocation(
					     &test_map, vaddr + 2 * PAGE_SIZE, 1,
					     &info),
				     true);
	failures += test_expect_u32("protect right unchanged",
				    info.prot, PLANE_VM_PROT_DEFAULT);
	failures += test_expect_bool("protect original absent",
				     test_map_lookup_allocation(
					     &test_map, vaddr, 3, NULL),
				     false);
	failures += check_stats("protect middle stats",
				TEST_KERNEL_MAP_PAGES - 3, 3, 3, 1, 3);
	return failures;
}

static int test_protect_pages_rejects_hole_in_range(void)
{
	struct plane_vm_map_stats before;
	struct plane_vm_map_stats after;
	uint64_t first = 0;
	uint64_t second = 0;
	int failures = 0;

	failures += test_expect_bool("protect hole init",
				     test_map_init(&test_map, test_entries,
						       TEST_MAP_ENTRIES,
						       TEST_KERNEL_MAP_BASE,
						       TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("protect hole first",
				     test_map_enter_fixed(
					     page_vaddr(0), 1, 0, NULL, 0,
					     PLANE_VM_MAP_ENTER_FIXED,
					     &first),
				     true);
	failures += test_expect_bool("protect hole second",
				     test_map_enter_fixed(
					     page_vaddr(2), 1, 0, NULL, 0,
					     PLANE_VM_MAP_ENTER_FIXED,
					     &second),
				     true);
	before = plane_vm_map_get_stats(&test_map);
	failures += test_expect_bool("protect hole rejected",
				     test_map_protect_pages(
					     &test_map, first, 3,
					     PLANE_VM_PROT_READ),
				     false);
	after = plane_vm_map_get_stats(&test_map);
	failures += test_expect_u64("protect hole free unchanged",
				    after.free_pages, before.free_pages);
	failures += test_expect_u64("protect hole count unchanged",
				    after.allocation_count,
				    before.allocation_count);
	failures += test_expect_bool("protect hole first preserved",
				     test_map_lookup_allocation(
					     &test_map, first, 1, NULL),
				     true);
	failures += test_expect_bool("protect hole second preserved",
				     test_map_lookup_allocation(
					     &test_map, second, 1, NULL),
				     true);
	return failures;
}

static int test_protect_pages_rejects_exhausted_split_entries(void)
{
	struct plane_vm_map_entry small_entries[2];
	struct plane_vm_map small_map = {0};
	struct plane_vm_map_stats before;
	struct plane_vm_map_stats after;
	uint64_t vaddr = 0;
	int failures = 0;

	for (uint64_t i = 0; i < TEST_ARRAY_SIZE(small_entries); i++) {
		small_entries[i] = (struct plane_vm_map_entry){0};
	}
	failures += test_expect_bool("protect split exhausted init",
				     test_map_init(&small_map, small_entries,
						       TEST_ARRAY_SIZE(small_entries),
						       TEST_KERNEL_MAP_BASE,
						       TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("protect split exhausted alloc",
				     test_map_enter_pages(&small_map, 3, &vaddr),
				     true);
	before = plane_vm_map_get_stats(&small_map);
	failures += test_expect_bool("protect split exhausted rejected",
				     test_map_protect_pages(
					     &small_map, vaddr + PAGE_SIZE, 1,
					     PLANE_VM_PROT_READ),
				     false);
	after = plane_vm_map_get_stats(&small_map);
	failures += test_expect_u64("protect split exhausted free unchanged",
				    after.free_pages, before.free_pages);
	failures += test_expect_u64("protect split exhausted count unchanged",
				    after.allocation_count,
				    before.allocation_count);
	failures += test_expect_bool("protect split exhausted preserved",
				     test_map_lookup_allocation(
					     &small_map, vaddr, 3, NULL),
				     true);
	return failures;
}

static int test_protect_pages_rejects_invalid_ranges(void)
{
	struct plane_vm_map_allocation_info info = {0};
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("protect reject init",
				     test_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("protect reject alloc",
				     test_map_enter_pages(&test_map, 2, &vaddr),
				     true);
	failures += test_expect_bool("protect rejects none",
				     test_map_protect_pages(&test_map, vaddr, 2, 0),
				     false);
	failures += test_expect_bool("protect rejects unknown",
				     test_map_protect_pages(&test_map, vaddr, 2,
								    BIT(8)),
				     false);
	failures += test_expect_bool("protect accepts partial",
				     test_map_protect_pages(&test_map,
					     vaddr, 1, PLANE_VM_PROT_READ),
				     true);
	failures += test_expect_bool("protect rejects absent",
				     test_map_protect_pages(&test_map,
					     page_vaddr(10), 1,
					     PLANE_VM_PROT_READ),
				     false);
	failures += test_expect_bool("protect old full lookup absent",
				     test_map_lookup_allocation(&test_map, vaddr,
								       2,
								       &info),
				     false);
	failures += test_expect_bool("protect partial lookup",
				     test_map_lookup_allocation(&test_map, vaddr,
								       1,
								       &info),
				     true);
	failures += test_expect_u32("protect partial prot",
				    info.prot, PLANE_VM_PROT_READ);
	failures += test_expect_bool(
		"protect remainder lookup",
		test_map_lookup_allocation(&test_map, vaddr + PAGE_SIZE, 1,
					       &info),
		true);
	failures += test_expect_u32("protect remainder prot",
				    info.prot, PLANE_VM_PROT_DEFAULT);
	failures += check_stats("protect reject stats",
				TEST_KERNEL_MAP_PAGES - 2, 2, 2, 1, 2);
	return failures;
}

static int test_protect_max_pages_updates_exact_allocation(void)
{
	struct plane_vm_map_allocation_info info = {0};
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool(
		"protect max exact init",
		test_map_init(&test_map, test_entries, TEST_MAP_ENTRIES,
				  TEST_KERNEL_MAP_BASE, TEST_KERNEL_MAP_SIZE),
		true);
	failures += test_expect_bool("protect max exact alloc",
				     test_map_enter_pages(&test_map, 2, &vaddr),
				     true);
	failures += test_expect_bool(
		"protect max exact readonly",
		test_map_protect_max_pages(&test_map, vaddr, 2,
					       PLANE_VM_PROT_READ),
		true);
	failures += test_expect_bool(
		"protect max exact lookup",
		test_map_lookup_allocation(&test_map, vaddr, 2, &info),
		true);
	failures += test_expect_u32("protect max exact current",
				    info.prot, PLANE_VM_PROT_READ);
	failures += test_expect_u32("protect max exact max",
				    info.max_prot, PLANE_VM_PROT_READ);
	failures += test_expect_bool(
		"protect max rejects current write",
		test_map_protect_pages(&test_map, vaddr, 2,
					   PLANE_VM_PROT_WRITE),
		false);
	failures += test_expect_bool(
		"protect max rejects raise",
		test_map_protect_max_pages(&test_map, vaddr, 2,
					       PLANE_VM_PROT_ALL),
		false);
	failures += test_expect_bool(
		"protect max exact lookup unchanged",
		test_map_lookup_allocation(&test_map, vaddr, 2, &info),
		true);
	failures += test_expect_u32("protect max unchanged current",
				    info.prot, PLANE_VM_PROT_READ);
	failures += test_expect_u32("protect max unchanged max",
				    info.max_prot, PLANE_VM_PROT_READ);
	return failures;
}

static int test_protect_max_pages_clips_object_offsets_and_refs(void)
{
	struct plane_vm_map_allocation_info info = {0};
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool(
		"protect max object init map",
		test_map_init(&test_map, test_entries, TEST_MAP_ENTRIES,
				  TEST_KERNEL_MAP_BASE, TEST_KERNEL_MAP_SIZE),
		true);
	failures += test_expect_bool("protect max object init",
				     plane_vm_object_init(&test_object,
							  4 * PAGE_SIZE),
				     true);
	failures += test_expect_bool(
		"protect max object enter",
		test_map_enter_pages_object(&test_map, 3, 0, &test_object, 0,
					    PLANE_VM_PROT_DEFAULT,
					    PLANE_VM_PROT_ALL, &vaddr),
		true);
	failures += test_expect_u64("protect max object ref before",
				    plane_vm_object_ref_count(&test_object), 2);
	failures += test_expect_bool(
		"protect max object middle",
		test_map_protect_max_pages(&test_map, vaddr + PAGE_SIZE,
					       1, PLANE_VM_PROT_READ),
		true);
	failures += test_expect_u64("protect max object ref after",
				    plane_vm_object_ref_count(&test_object), 4);
	failures += test_expect_bool(
		"protect max object full absent",
		test_map_lookup_allocation(&test_map, vaddr, 3, NULL),
		false);
	failures += test_expect_bool(
		"protect max object left",
		test_map_lookup_allocation(&test_map, vaddr, 1, &info),
		true);
	failures += test_expect_ptr("protect max object left object",
				    info.object, &test_object);
	failures += test_expect_u64("protect max object left offset",
				    info.object_offset, 0);
	failures += test_expect_u32("protect max object left prot",
				    info.prot, PLANE_VM_PROT_DEFAULT);
	failures += test_expect_u32("protect max object left max",
				    info.max_prot, PLANE_VM_PROT_ALL);
	failures += test_expect_bool(
		"protect max object middle lookup",
		test_map_lookup_allocation(&test_map, vaddr + PAGE_SIZE,
					       1, &info),
		true);
	failures += test_expect_ptr("protect max object middle object",
				    info.object, &test_object);
	failures += test_expect_u64("protect max object middle offset",
				    info.object_offset, PAGE_SIZE);
	failures += test_expect_u32("protect max object middle prot",
				    info.prot, PLANE_VM_PROT_READ);
	failures += test_expect_u32("protect max object middle max",
				    info.max_prot, PLANE_VM_PROT_READ);
	failures += test_expect_bool(
		"protect max object right",
		test_map_lookup_allocation(&test_map,
					       vaddr + 2 * PAGE_SIZE, 1,
					       &info),
		true);
	failures += test_expect_ptr("protect max object right object",
				    info.object, &test_object);
	failures += test_expect_u64("protect max object right offset",
				    info.object_offset, 2 * PAGE_SIZE);
	failures += test_expect_u32("protect max object right prot",
				    info.prot, PLANE_VM_PROT_DEFAULT);
	failures += test_expect_u32("protect max object right max",
				    info.max_prot, PLANE_VM_PROT_ALL);
	failures += check_stats("protect max object stats",
				TEST_KERNEL_MAP_PAGES - 3, 3, 3, 1, 3);
	return failures;
}

static int test_protect_max_pages_updates_contiguous_entries(void)
{
	struct plane_vm_map_allocation_info info = {0};
	uint64_t first = 0;
	uint64_t second = 0;
	int failures = 0;

	failures += test_expect_bool(
		"protect max contiguous init",
		test_map_init(&test_map, test_entries, TEST_MAP_ENTRIES,
				  TEST_KERNEL_MAP_BASE, TEST_KERNEL_MAP_SIZE),
		true);
	failures += test_expect_bool(
		"protect max contiguous first",
		test_map_enter_fixed(page_vaddr(0), 1, 0, NULL, 0,
				     PLANE_VM_MAP_ENTER_FIXED, &first),
		true);
	failures += test_expect_bool(
		"protect max contiguous second",
		test_map_enter_fixed(page_vaddr(1), 1, 0, NULL, 0,
				     PLANE_VM_MAP_ENTER_FIXED, &second),
		true);
	failures += test_expect_bool(
		"protect max contiguous range",
		test_map_protect_max_pages(&test_map, first, 2,
					       PLANE_VM_PROT_READ),
		true);
	failures += test_expect_bool(
		"protect max contiguous first lookup",
		test_map_lookup_allocation(&test_map, first, 1, &info),
		true);
	failures += test_expect_u32("protect max contiguous first prot",
				    info.prot, PLANE_VM_PROT_READ);
	failures += test_expect_u32("protect max contiguous first max",
				    info.max_prot, PLANE_VM_PROT_READ);
	failures += test_expect_bool(
		"protect max contiguous second lookup",
		test_map_lookup_allocation(&test_map, second, 1, &info),
		true);
	failures += test_expect_u32("protect max contiguous second prot",
				    info.prot, PLANE_VM_PROT_READ);
	failures += test_expect_u32("protect max contiguous second max",
				    info.max_prot, PLANE_VM_PROT_READ);
	failures += check_stats("protect max contiguous stats",
				TEST_KERNEL_MAP_PAGES - 2, 2, 2, 1, 2);
	return failures;
}

static int test_protect_max_pages_rejects_hole_in_range(void)
{
	struct plane_vm_map_allocation_info info = {0};
	struct plane_vm_map_stats before;
	struct plane_vm_map_stats after;
	uint64_t first = 0;
	uint64_t second = 0;
	int failures = 0;

	failures += test_expect_bool(
		"protect max hole init",
		test_map_init(&test_map, test_entries, TEST_MAP_ENTRIES,
				  TEST_KERNEL_MAP_BASE, TEST_KERNEL_MAP_SIZE),
		true);
	failures += test_expect_bool(
		"protect max hole first",
		test_map_enter_fixed(page_vaddr(0), 1, 0, NULL, 0,
				     PLANE_VM_MAP_ENTER_FIXED, &first),
		true);
	failures += test_expect_bool(
		"protect max hole second",
		test_map_enter_fixed(page_vaddr(2), 1, 0, NULL, 0,
				     PLANE_VM_MAP_ENTER_FIXED, &second),
		true);
	before = plane_vm_map_get_stats(&test_map);
	failures += test_expect_bool(
		"protect max hole rejected",
		test_map_protect_max_pages(&test_map, first, 3,
					       PLANE_VM_PROT_READ),
		false);
	after = plane_vm_map_get_stats(&test_map);
	failures += test_expect_u64("protect max hole free unchanged",
				    after.free_pages, before.free_pages);
	failures += test_expect_u64("protect max hole count unchanged",
				    after.allocation_count,
				    before.allocation_count);
	failures += test_expect_bool(
		"protect max hole first preserved",
		test_map_lookup_allocation(&test_map, first, 1, &info),
		true);
	failures += test_expect_u32("protect max hole first max",
				    info.max_prot, PLANE_VM_PROT_ALL);
	failures += test_expect_bool(
		"protect max hole second preserved",
		test_map_lookup_allocation(&test_map, second, 1, &info),
		true);
	failures += test_expect_u32("protect max hole second max",
				    info.max_prot, PLANE_VM_PROT_ALL);
	return failures;
}

static int test_protect_max_pages_rejects_exhausted_split_entries(void)
{
	struct plane_vm_map_entry small_entries[2];
	struct plane_vm_map small_map = {0};
	struct plane_vm_map_allocation_info info = {0};
	struct plane_vm_map_stats before;
	struct plane_vm_map_stats after;
	struct plane_vm_object *object = NULL;
	uint64_t vaddr = 0;
	int failures = 0;

	for (uint64_t i = 0; i < TEST_ARRAY_SIZE(small_entries); i++) {
		small_entries[i] = (struct plane_vm_map_entry){0};
	}
	failures += test_expect_bool(
		"protect max split exhausted init",
		test_map_init(&small_map, small_entries,
				  TEST_ARRAY_SIZE(small_entries),
				  TEST_KERNEL_MAP_BASE, TEST_KERNEL_MAP_SIZE),
		true);
	failures += test_expect_bool("protect max split exhausted alloc",
				     test_map_enter_pages(&small_map, 3,
							  &vaddr),
				     true);
	failures += test_expect_bool(
		"protect max split exhausted lookup",
		test_map_lookup_allocation(&small_map, vaddr, 3, &info),
		true);
	object = info.object;
	before = plane_vm_map_get_stats(&small_map);
	failures += test_expect_bool(
		"protect max split exhausted rejected",
		test_map_protect_max_pages(&small_map, vaddr + PAGE_SIZE,
					       1, PLANE_VM_PROT_READ),
		false);
	after = plane_vm_map_get_stats(&small_map);
	failures += test_expect_u64("protect max split free unchanged",
				    after.free_pages, before.free_pages);
	failures += test_expect_u64("protect max split count unchanged",
				    after.allocation_count,
				    before.allocation_count);
	failures += test_expect_bool(
		"protect max split preserved",
		test_map_lookup_allocation(&small_map, vaddr, 3, &info),
		true);
	failures += test_expect_u32("protect max split max unchanged",
				    info.max_prot, PLANE_VM_PROT_ALL);
	failures += test_expect_u64("protect max split ref unchanged",
				    plane_vm_object_ref_count(object), 1);
	return failures;
}

static int test_protect_max_pages_rejects_invalid_ranges(void)
{
	struct plane_vm_map_allocation_info info = {0};
	uint64_t overflow_addr = UINT64_MAX - PAGE_SIZE + 1;
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool(
		"protect max reject init",
		test_map_init(&test_map, test_entries, TEST_MAP_ENTRIES,
				  TEST_KERNEL_MAP_BASE, TEST_KERNEL_MAP_SIZE),
		true);
	failures += test_expect_bool("protect max reject alloc",
				     test_map_enter_pages(&test_map, 2, &vaddr),
				     true);
	failures += test_expect_bool(
		"protect max rejects null map",
		test_map_protect_max_pages(NULL, vaddr, 2,
					       PLANE_VM_PROT_READ),
		false);
	failures += test_expect_bool(
		"protect max rejects none",
		test_map_protect_max_pages(&test_map, vaddr, 2,
					       PLANE_VM_PROT_NONE),
		false);
	failures += test_expect_bool(
		"protect max rejects unknown",
		test_map_protect_max_pages(&test_map, vaddr, 2, BIT(8)),
		false);
	failures += test_expect_bool(
		"protect max rejects zero pages",
		test_map_protect_max_pages(&test_map, vaddr, 0,
					       PLANE_VM_PROT_READ),
		false);
	failures += test_expect_bool(
		"protect max rejects unaligned",
		test_map_protect_max_pages(&test_map, vaddr + 1, 1,
					       PLANE_VM_PROT_READ),
		false);
	failures += test_expect_bool(
		"protect max rejects overflow",
		test_map_protect_max_pages(&test_map, overflow_addr, 2,
					       PLANE_VM_PROT_READ),
		false);
	failures += test_expect_bool(
		"protect max rejects absent",
		test_map_protect_max_pages(&test_map, page_vaddr(10), 1,
					       PLANE_VM_PROT_READ),
		false);
	failures += test_expect_bool(
		"protect max reject lookup unchanged",
		test_map_lookup_allocation(&test_map, vaddr, 2, &info),
		true);
	failures += test_expect_u32("protect max reject prot unchanged",
				    info.prot, PLANE_VM_PROT_DEFAULT);
	failures += test_expect_u32("protect max reject max unchanged",
				    info.max_prot, PLANE_VM_PROT_ALL);
	failures += check_stats("protect max reject stats",
				TEST_KERNEL_MAP_PAGES - 2, 2, 2, 1, 1);
	return failures;
}

static int test_wire_pages_updates_exact_allocation(void)
{
	struct plane_vm_map_allocation_info info = {0};
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("wire map init",
				     test_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("wire map alloc",
				     test_map_enter_pages(&test_map, 2, &vaddr),
				     true);
	failures += test_expect_bool("wire map first",
				     test_map_wire_pages(&test_map, vaddr, 2),
				     true);
	failures += test_expect_bool("wire map second",
				     test_map_wire_pages(&test_map, vaddr, 2),
				     true);
	failures += test_expect_bool("wire map lookup",
				     test_map_lookup_allocation(&test_map, vaddr,
								       2,
								       &info),
				     true);
	failures += test_expect_u64("wire map count", info.wired_count, 2);
	failures += test_expect_bool("wire map free rejected",
				     test_map_free_pages(&test_map, vaddr, 2),
				     false);
	failures += test_expect_bool("wire map unwire",
				     test_map_unwire_pages(&test_map, vaddr, 2),
				     true);
	failures += test_expect_bool("wire map lookup one",
				     test_map_lookup_allocation(&test_map, vaddr,
								       2,
								       &info),
				     true);
	failures += test_expect_u64("wire map count one",
				    info.wired_count, 1);
	failures += test_expect_bool("wire map unwire second",
				     test_map_unwire_pages(&test_map, vaddr, 2),
				     true);
	failures += test_expect_bool("wire map unwire zero rejected",
				     test_map_unwire_pages(&test_map, vaddr, 2),
				     false);
	failures += test_expect_bool("wire map free",
				     test_map_free_pages(&test_map, vaddr, 2),
				     true);
	return failures;
}

static int test_wire_pages_clips_middle_fragment(void)
{
	struct plane_vm_map_allocation_info info = {0};
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool(
		"wire middle init",
		test_map_init(&test_map, test_entries, TEST_MAP_ENTRIES,
				  TEST_KERNEL_MAP_BASE, TEST_KERNEL_MAP_SIZE),
		true);
	failures += test_expect_bool("wire middle object init",
				     plane_vm_object_init(&test_object,
							  4 * PAGE_SIZE),
				     true);
	failures += test_expect_bool(
		"wire middle enter",
		test_map_enter_pages_object(&test_map, 3, 0, &test_object, 0,
					    PLANE_VM_PROT_DEFAULT,
					    PLANE_VM_PROT_ALL, &vaddr),
		true);
	failures += test_expect_bool("wire middle page",
				     test_map_wire_pages(
					     &test_map, vaddr + PAGE_SIZE, 1),
				     true);
	failures += test_expect_u64("wire middle ref count",
				    plane_vm_object_ref_count(&test_object), 4);
	failures += test_expect_bool("wire middle original absent",
				     test_map_lookup_allocation(
					     &test_map, vaddr, 3, NULL),
				     false);
	failures += test_expect_bool("wire middle left lookup",
				     test_map_lookup_allocation(
					     &test_map, vaddr, 1, &info),
				     true);
	failures += test_expect_ptr("wire middle left object",
				    info.object, &test_object);
	failures += test_expect_u64("wire middle left offset",
				    info.object_offset, 0);
	failures += test_expect_u64("wire middle left count",
				    info.wired_count, 0);
	failures += test_expect_bool("wire middle target lookup",
				     test_map_lookup_allocation(
					     &test_map, vaddr + PAGE_SIZE, 1,
					     &info),
				     true);
	failures += test_expect_ptr("wire middle target object",
				    info.object, &test_object);
	failures += test_expect_u64("wire middle target offset",
				    info.object_offset, PAGE_SIZE);
	failures += test_expect_u64("wire middle target count",
				    info.wired_count, 1);
	failures += test_expect_bool("wire middle right lookup",
				     test_map_lookup_allocation(
					     &test_map, vaddr + 2 * PAGE_SIZE, 1,
					     &info),
				     true);
	failures += test_expect_ptr("wire middle right object",
				    info.object, &test_object);
	failures += test_expect_u64("wire middle right offset",
				    info.object_offset, 2 * PAGE_SIZE);
	failures += test_expect_u64("wire middle right count",
				    info.wired_count, 0);
	failures += test_expect_bool("wire middle free rejected",
				     test_map_free_pages(
					     &test_map, vaddr + PAGE_SIZE, 1),
				     false);
	failures += check_stats("wire middle stats",
				TEST_KERNEL_MAP_PAGES - 3, 3, 3, 1, 3);
	return failures;
}

static int test_unwire_pages_clips_middle_fragment(void)
{
	struct plane_vm_map_allocation_info info = {0};
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool(
		"unwire middle init",
		test_map_init(&test_map, test_entries, TEST_MAP_ENTRIES,
				  TEST_KERNEL_MAP_BASE, TEST_KERNEL_MAP_SIZE),
		true);
	failures += test_expect_bool("unwire middle object init",
				     plane_vm_object_init(&test_object,
							  4 * PAGE_SIZE),
				     true);
	failures += test_expect_bool(
		"unwire middle enter",
		test_map_enter_pages_object(&test_map, 3, 0, &test_object, 0,
					    PLANE_VM_PROT_DEFAULT,
					    PLANE_VM_PROT_ALL, &vaddr),
		true);
	failures += test_expect_bool("unwire middle wire all",
				     test_map_wire_pages(&test_map, vaddr,
							     3),
				     true);
	failures += test_expect_bool("unwire middle page",
				     test_map_unwire_pages(
					     &test_map, vaddr + PAGE_SIZE, 1),
				     true);
	failures += test_expect_u64("unwire middle ref count",
				    plane_vm_object_ref_count(&test_object), 4);
	failures += test_expect_bool("unwire middle left lookup",
				     test_map_lookup_allocation(
					     &test_map, vaddr, 1, &info),
				     true);
	failures += test_expect_u64("unwire middle left count",
				    info.wired_count, 1);
	failures += test_expect_bool("unwire middle target lookup",
				     test_map_lookup_allocation(
					     &test_map, vaddr + PAGE_SIZE, 1,
					     &info),
				     true);
	failures += test_expect_u64("unwire middle target count",
				    info.wired_count, 0);
	failures += test_expect_bool("unwire middle right lookup",
				     test_map_lookup_allocation(
					     &test_map, vaddr + 2 * PAGE_SIZE, 1,
					     &info),
				     true);
	failures += test_expect_u64("unwire middle right count",
				    info.wired_count, 1);
	failures += test_expect_bool("unwire middle left free rejected",
				     test_map_free_pages(&test_map, vaddr,
							     1),
				     false);
	failures += test_expect_bool("unwire middle target free",
				     test_map_free_pages(
					     &test_map, vaddr + PAGE_SIZE, 1),
				     true);
	failures += check_stats("unwire middle free stats",
				TEST_KERNEL_MAP_PAGES - 2, 2, 2, 2, 2);
	return failures;
}

static int test_wire_pages_updates_contiguous_entries(void)
{
	struct plane_vm_map_allocation_info info = {0};
	uint64_t first = 0;
	uint64_t second = 0;
	int failures = 0;

	failures += test_expect_bool(
		"wire contiguous init",
		test_map_init(&test_map, test_entries, TEST_MAP_ENTRIES,
				  TEST_KERNEL_MAP_BASE, TEST_KERNEL_MAP_SIZE),
		true);
	failures += test_expect_bool(
		"wire contiguous first",
		test_map_enter_fixed(page_vaddr(0), 1, 0, NULL, 0,
				     PLANE_VM_MAP_ENTER_FIXED, &first),
		true);
	failures += test_expect_bool(
		"wire contiguous second",
		test_map_enter_fixed(page_vaddr(1), 1, 0, NULL, 0,
				     PLANE_VM_MAP_ENTER_FIXED, &second),
		true);
	failures += test_expect_bool("wire contiguous range",
				     test_map_wire_pages(&test_map, first,
							     2),
				     true);
	failures += test_expect_bool("wire contiguous first lookup",
				     test_map_lookup_allocation(
					     &test_map, first, 1, &info),
				     true);
	failures += test_expect_u64("wire contiguous first count",
				    info.wired_count, 1);
	failures += test_expect_bool("wire contiguous second lookup",
				     test_map_lookup_allocation(
					     &test_map, second, 1, &info),
				     true);
	failures += test_expect_u64("wire contiguous second count",
				    info.wired_count, 1);
	failures += test_expect_bool("unwire contiguous range",
				     test_map_unwire_pages(&test_map, first,
							       2),
				     true);
	failures += test_expect_bool("unwire contiguous first lookup",
				     test_map_lookup_allocation(
					     &test_map, first, 1, &info),
				     true);
	failures += test_expect_u64("unwire contiguous first count",
				    info.wired_count, 0);
	failures += test_expect_bool("unwire contiguous second lookup",
				     test_map_lookup_allocation(
					     &test_map, second, 1, &info),
				     true);
	failures += test_expect_u64("unwire contiguous second count",
				    info.wired_count, 0);
	return failures;
}

static int test_wire_pages_guarded_range_uses_user_pages(void)
{
	struct plane_vm_map_allocation_info info = {0};
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool(
		"wire guarded init",
		test_map_init(&test_map, test_entries, TEST_MAP_ENTRIES,
				  TEST_KERNEL_MAP_BASE, TEST_KERNEL_MAP_SIZE),
		true);
	failures += test_expect_bool(
		"wire guarded alloc",
		test_map_enter_pages_protected(&test_map, 2, 1,
					       PLANE_VM_PROT_DEFAULT, &vaddr),
		true);
	failures += test_expect_u64("wire guarded user addr",
				    vaddr, page_vaddr(1));
	failures += test_expect_bool(
		"wire guarded left guard absent",
		test_map_lookup_allocation(&test_map, page_vaddr(0), 1,
					       NULL),
		false);
	failures += test_expect_bool("wire guarded user pages",
				     test_map_wire_pages(&test_map, vaddr,
							     2),
				     true);
	failures += test_expect_bool(
		"wire guarded lookup",
		test_map_lookup_allocation(&test_map, vaddr, 2, &info),
		true);
	failures += test_expect_u64("wire guarded count",
				    info.wired_count, 1);
	failures += test_expect_u64("wire guarded reserved pages",
				    info.reserved_pages, 4);
	failures += test_expect_bool(
		"wire guarded left guard still absent",
		test_map_lookup_allocation(&test_map, page_vaddr(0), 1,
					       NULL),
		false);
	failures += test_expect_bool("unwire guarded user pages",
				     test_map_unwire_pages(&test_map,
							       vaddr, 2),
				     true);
	failures += test_expect_bool(
		"unwire guarded lookup",
		test_map_lookup_allocation(&test_map, vaddr, 2, &info),
		true);
	failures += test_expect_u64("unwire guarded count",
				    info.wired_count, 0);
	failures += test_expect_bool("wire guarded free",
				     test_map_free_pages(&test_map, vaddr,
							     2),
				     true);
	return failures;
}

static int test_wire_pages_rejects_hole_in_range(void)
{
	struct plane_vm_map_allocation_info info = {0};
	struct plane_vm_map_stats before;
	struct plane_vm_map_stats after;
	uint64_t first = 0;
	uint64_t second = 0;
	int failures = 0;

	failures += test_expect_bool(
		"wire hole init",
		test_map_init(&test_map, test_entries, TEST_MAP_ENTRIES,
				  TEST_KERNEL_MAP_BASE, TEST_KERNEL_MAP_SIZE),
		true);
	failures += test_expect_bool(
		"wire hole first",
		test_map_enter_fixed(page_vaddr(0), 1, 0, NULL, 0,
				     PLANE_VM_MAP_ENTER_FIXED, &first),
		true);
	failures += test_expect_bool(
		"wire hole second",
		test_map_enter_fixed(page_vaddr(2), 1, 0, NULL, 0,
				     PLANE_VM_MAP_ENTER_FIXED, &second),
		true);
	before = plane_vm_map_get_stats(&test_map);
	failures += test_expect_bool("wire hole rejected",
				     test_map_wire_pages(&test_map, first,
							     3),
				     false);
	after = plane_vm_map_get_stats(&test_map);
	failures += test_expect_u64("wire hole free unchanged",
				    after.free_pages, before.free_pages);
	failures += test_expect_u64("wire hole count unchanged",
				    after.allocation_count,
				    before.allocation_count);
	failures += test_expect_bool("wire hole first lookup",
				     test_map_lookup_allocation(
					     &test_map, first, 1, &info),
				     true);
	failures += test_expect_u64("wire hole first count",
				    info.wired_count, 0);
	failures += test_expect_bool("wire hole second lookup",
				     test_map_lookup_allocation(
					     &test_map, second, 1, &info),
				     true);
	failures += test_expect_u64("wire hole second count",
				    info.wired_count, 0);
	return failures;
}

static int test_wire_pages_rejects_exhausted_split_entries(void)
{
	struct plane_vm_map_entry small_entries[2];
	struct plane_vm_map small_map = {0};
	struct plane_vm_map_allocation_info info = {0};
	struct plane_vm_map_stats before;
	struct plane_vm_map_stats after;
	struct plane_vm_object *object = NULL;
	uint64_t vaddr = 0;
	int failures = 0;

	for (uint64_t i = 0; i < TEST_ARRAY_SIZE(small_entries); i++) {
		small_entries[i] = (struct plane_vm_map_entry){0};
	}
	failures += test_expect_bool(
		"wire split exhausted init",
		test_map_init(&small_map, small_entries,
				  TEST_ARRAY_SIZE(small_entries),
				  TEST_KERNEL_MAP_BASE, TEST_KERNEL_MAP_SIZE),
		true);
	failures += test_expect_bool("wire split exhausted alloc",
				     test_map_enter_pages(&small_map, 3,
							  &vaddr),
				     true);
	failures += test_expect_bool(
		"wire split exhausted lookup",
		test_map_lookup_allocation(&small_map, vaddr, 3, &info),
		true);
	object = info.object;
	before = plane_vm_map_get_stats(&small_map);
	failures += test_expect_bool("wire split exhausted rejected",
				     test_map_wire_pages(
					     &small_map, vaddr + PAGE_SIZE, 1),
				     false);
	after = plane_vm_map_get_stats(&small_map);
	failures += test_expect_u64("wire split free unchanged",
				    after.free_pages, before.free_pages);
	failures += test_expect_u64("wire split count unchanged",
				    after.allocation_count,
				    before.allocation_count);
	failures += test_expect_bool("wire split preserved",
				     test_map_lookup_allocation(
					     &small_map, vaddr, 3, &info),
				     true);
	failures += test_expect_u64("wire split wired unchanged",
				    info.wired_count, 0);
	failures += test_expect_u64("wire split ref unchanged",
				    plane_vm_object_ref_count(object), 1);
	return failures;
}

static int test_wire_pages_rejects_invalid_ranges(void)
{
	struct plane_vm_map_allocation_info info = {0};
	uint64_t overflow_addr = UINT64_MAX - PAGE_SIZE + 1;
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("wire reject init",
				     test_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("wire reject alloc",
				     test_map_enter_pages(&test_map, 2, &vaddr),
				     true);
	failures += test_expect_bool("wire reject zero pages",
				     test_map_wire_pages(&test_map, vaddr, 0),
				     false);
	failures += test_expect_bool("wire reject unaligned",
				     test_map_wire_pages(&test_map,
							     vaddr + 1, 1),
				     false);
	failures += test_expect_bool("wire reject overflow",
				     test_map_wire_pages(&test_map,
							     overflow_addr, 2),
				     false);
	failures += test_expect_bool("wire reject absent",
				     test_map_wire_pages(&test_map, page_vaddr(10),
							     1),
				     false);
	failures += test_expect_bool("unwire reject zero pages",
				     test_map_unwire_pages(&test_map, vaddr, 0),
				     false);
	failures += test_expect_bool("unwire reject unaligned",
				     test_map_unwire_pages(&test_map,
							       vaddr + 1, 1),
				     false);
	failures += test_expect_bool("unwire reject overflow",
				     test_map_unwire_pages(&test_map,
							       overflow_addr,
							       2),
				     false);
	failures += test_expect_bool("unwire reject unwired",
				     test_map_unwire_pages(&test_map,
							       vaddr, 2),
				     false);
	failures += test_expect_bool("unwire reject absent",
				     test_map_unwire_pages(&test_map,
							       page_vaddr(10), 1),
				     false);
	failures += test_expect_bool("wire reject lookup unchanged",
				     test_map_lookup_allocation(&test_map, vaddr,
								       2,
								       &info),
				     true);
	failures += test_expect_u64("wire reject count unchanged",
				    info.wired_count, 0);
	return failures;
}

static int test_object_allocation_records_object_and_offset(void)
{
	struct plane_vm_map_allocation_info info = {0};
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("object alloc init",
				     test_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("object init",
				     plane_vm_object_init(&test_object,
							  TEST_KERNEL_MAP_BASE +
							  TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("object alloc",
				     test_map_enter_pages_object(
					     &test_map, 2, 0, &test_object,
					     4 * PAGE_SIZE,
					     PLANE_VM_PROT_DEFAULT,
					     PLANE_VM_PROT_ALL, &vaddr),
				     true);
	failures += test_expect_u64("object alloc vaddr", vaddr,
				    TEST_KERNEL_MAP_BASE);
	failures += test_expect_u64("object alloc ref count",
				    plane_vm_object_ref_count(&test_object), 2);
	failures += test_expect_bool("object alloc lookup",
				     test_map_lookup_allocation(
					     &test_map, vaddr, 2, &info),
				     true);
	failures += test_expect_u64("object lookup ref unchanged",
				    plane_vm_object_ref_count(&test_object), 2);
	failures += test_expect_ptr("object alloc object",
				    info.object, &test_object);
	failures += test_expect_u64("object alloc offset",
				    info.object_offset, 4 * PAGE_SIZE);
	failures += test_expect_bool("object alloc free",
				     test_map_free_pages(&test_map, vaddr, 2),
				     true);
	failures += test_expect_u64("object free ref count",
				    plane_vm_object_ref_count(&test_object), 1);
	return failures;
}

static int test_object_auto_offset_uses_user_range(void)
{
	struct plane_vm_map_allocation_info info = {0};
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("object auto init",
				     test_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("object init",
				     plane_vm_object_init(&test_object,
							  TEST_KERNEL_MAP_BASE +
							  TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("object auto alloc",
				     test_map_enter_pages_object(
					     &test_map, 2, 1, &test_object,
					     PLANE_VM_MAP_OBJECT_OFFSET_AUTO,
					     PLANE_VM_PROT_DEFAULT,
					     PLANE_VM_PROT_ALL, &vaddr),
				     true);
	failures += test_expect_u64("object auto user vaddr", vaddr,
				    page_vaddr(1));
	failures += test_expect_bool("object auto lookup",
				     test_map_lookup_allocation(
					     &test_map, vaddr, 2, &info),
				     true);
	failures += test_expect_ptr("object auto object",
				    info.object, &test_object);
	failures += test_expect_u64("object auto offset",
				    info.object_offset, page_vaddr(1));
	failures += test_expect_u64("object auto reserved",
				    test_vaddr_raw(info.reserved_start),
				    TEST_KERNEL_MAP_BASE);
	failures += test_expect_u64("object auto ref count",
				    plane_vm_object_ref_count(&test_object), 2);
	return failures;
}

static int test_object_auto_offset_uses_user_va_across_maps(void)
{
	struct plane_vm_map_entry other_entries[TEST_MAP_ENTRIES];
	struct plane_vm_map other_map = {0};
	struct plane_vm_map_allocation_info first_info = {0};
	struct plane_vm_map_allocation_info second_info = {0};
	uint64_t first_vaddr = 0;
	uint64_t second_vaddr = 0;
	uint64_t other_base = TEST_KERNEL_MAP_BASE + TEST_KERNEL_MAP_SIZE;
	int failures = 0;

	for (uint64_t i = 0; i < TEST_MAP_ENTRIES; i++) {
		other_entries[i] = (struct plane_vm_map_entry){0};
	}

	failures += test_expect_bool("object first map init",
				     test_map_init(&test_map, test_entries,
						       TEST_MAP_ENTRIES,
						       TEST_KERNEL_MAP_BASE,
						       TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("object second map init",
				     test_map_init(&other_map, other_entries,
						       TEST_MAP_ENTRIES,
						       other_base,
						       TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("object init",
				     plane_vm_object_init(&test_object,
							  other_base +
							  TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("object first map alloc",
				     test_map_enter_pages_object(
					     &test_map, 1, 0, &test_object,
					     PLANE_VM_MAP_OBJECT_OFFSET_AUTO,
					     PLANE_VM_PROT_DEFAULT,
					     PLANE_VM_PROT_ALL, &first_vaddr),
				     true);
	failures += test_expect_bool("object second map alloc",
				     test_map_enter_pages_object(
					     &other_map, 1, 0, &test_object,
					     PLANE_VM_MAP_OBJECT_OFFSET_AUTO,
					     PLANE_VM_PROT_DEFAULT,
					     PLANE_VM_PROT_ALL, &second_vaddr),
				     true);
	failures += test_expect_bool("object first lookup",
				     test_map_lookup_allocation(
					     &test_map, first_vaddr, 1,
					     &first_info),
				     true);
	failures += test_expect_bool("object second lookup",
				     test_map_lookup_allocation(
					     &other_map, second_vaddr, 1,
					     &second_info),
				     true);
	failures += test_expect_u64("object first offset",
				    first_info.object_offset, first_vaddr);
	failures += test_expect_u64("object second offset",
				    second_info.object_offset, second_vaddr);
	failures += test_expect_bool("object offsets differ",
				     first_info.object_offset !=
				     second_info.object_offset,
				     true);
	failures += test_expect_u64("object two map ref count",
				    plane_vm_object_ref_count(&test_object), 3);
	failures += test_expect_bool("object first map free",
				     test_map_free_pages(&test_map,
							     first_vaddr, 1),
				     true);
	failures += test_expect_bool("object second map free",
				     test_map_free_pages(&other_map,
							     second_vaddr, 1),
				     true);
	failures += test_expect_u64("object two map ref count freed",
				    plane_vm_object_ref_count(&test_object), 1);
	return failures;
}

static int test_object_allocation_rejects_invalid_offset(void)
{
	struct plane_vm_map_stats before;
	struct plane_vm_map_stats after;
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("object reject init",
				     test_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("object init",
				     plane_vm_object_init(&test_object,
							  TEST_KERNEL_MAP_SIZE),
				     true);
	before = plane_vm_map_get_stats(&test_map);
	failures += test_expect_bool("object reject null with offset",
				     test_map_enter_pages_object(
					     &test_map, 1, 0, NULL, PAGE_SIZE,
					     PLANE_VM_PROT_DEFAULT,
					     PLANE_VM_PROT_ALL, &vaddr),
				     false);
	failures += test_expect_bool("object reject unaligned offset",
				     test_map_enter_pages_object(
					     &test_map, 1, 0, &test_object, 1,
					     PLANE_VM_PROT_DEFAULT,
					     PLANE_VM_PROT_ALL, &vaddr),
				     false);
	after = plane_vm_map_get_stats(&test_map);
	failures += test_expect_u64("object reject free unchanged",
				    after.free_pages, before.free_pages);
	failures += test_expect_u64("object reject allocations unchanged",
				    after.allocation_count,
				    before.allocation_count);
	failures += test_expect_u64("object reject ref unchanged",
				    plane_vm_object_ref_count(&test_object), 1);
	return failures;
}

static int test_object_allocation_validates_object_range(void)
{
	struct plane_vm_map_stats before;
	struct plane_vm_map_stats after;
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("object range init",
				     test_map_init(&test_map, test_entries,
						       TEST_MAP_ENTRIES,
						       TEST_KERNEL_MAP_BASE,
						       TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("object range object init",
				     plane_vm_object_init(&test_object,
							  4 * PAGE_SIZE),
				     true);
	failures += test_expect_bool("object range exact end",
				     test_map_enter_pages_object(
					     &test_map, 2, 0, &test_object,
					     2 * PAGE_SIZE,
					     PLANE_VM_PROT_DEFAULT,
					     PLANE_VM_PROT_ALL, &vaddr),
				     true);
	failures += test_expect_u64("object range exact ref count",
				    plane_vm_object_ref_count(&test_object), 2);
	failures += test_expect_bool("object range exact free",
				     test_map_free_pages(&test_map, vaddr, 2),
				     true);
	before = plane_vm_map_get_stats(&test_map);
	failures += test_expect_bool("object range past end",
				     test_map_enter_pages_object(
					     &test_map, 2, 0, &test_object,
					     3 * PAGE_SIZE,
					     PLANE_VM_PROT_DEFAULT,
					     PLANE_VM_PROT_ALL, &vaddr),
				     false);
	after = plane_vm_map_get_stats(&test_map);
	failures += test_expect_u64("object range ref unchanged",
				    plane_vm_object_ref_count(&test_object), 1);
	failures += test_expect_u64("object range free unchanged",
				    after.free_pages, before.free_pages);
	failures += test_expect_u64("object range allocations unchanged",
				    after.allocation_count,
				    before.allocation_count);
	return failures;
}

static int test_object_auto_offset_validates_object_range(void)
{
	struct plane_vm_map_entry high_entries[TEST_MAP_ENTRIES];
	struct plane_vm_map high_map = {0};
	struct plane_vm_map_stats before;
	struct plane_vm_map_stats after;
	uint64_t high_base = TEST_KERNEL_MAP_BASE;
	uint64_t vaddr = 0;
	int failures = 0;

	for (uint64_t i = 0; i < TEST_MAP_ENTRIES; i++) {
		high_entries[i] = (struct plane_vm_map_entry){0};
	}

	failures += test_expect_bool("object auto range init",
				     test_map_init(&high_map, high_entries,
						       TEST_MAP_ENTRIES,
						       high_base,
						       TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("object auto range object init",
				     plane_vm_object_init(&test_object,
							  TEST_KERNEL_MAP_SIZE),
				     true);
	before = plane_vm_map_get_stats(&high_map);
	failures += test_expect_bool("object auto range high rejected",
				     test_map_enter_pages_object(
					     &high_map, 1, 0, &test_object,
					     PLANE_VM_MAP_OBJECT_OFFSET_AUTO,
					     PLANE_VM_PROT_DEFAULT,
					     PLANE_VM_PROT_ALL, &vaddr),
				     false);
	after = plane_vm_map_get_stats(&high_map);
	failures += test_expect_u64("object auto range ref unchanged",
				    plane_vm_object_ref_count(&test_object), 1);
	failures += test_expect_u64("object auto range free unchanged",
				    after.free_pages, before.free_pages);
	failures += test_expect_u64("object auto range allocations unchanged",
				    after.allocation_count,
				    before.allocation_count);
	return failures;
}

static int test_object_allocation_rejects_offset_overflow(void)
{
	struct plane_vm_map_stats before;
	struct plane_vm_map_stats after;
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("object overflow init",
				     test_map_init(&test_map, test_entries,
						       TEST_MAP_ENTRIES,
						       TEST_KERNEL_MAP_BASE,
						       TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("object overflow object init",
				     plane_vm_object_init(&test_object,
							  UINT64_MAX & ~(PAGE_SIZE - 1)),
				     true);
	before = plane_vm_map_get_stats(&test_map);
	failures += test_expect_bool("object offset overflow rejected",
				     test_map_enter_pages_object(
					     &test_map, 2, 0, &test_object,
					     UINT64_MAX - PAGE_SIZE + 1,
					     PLANE_VM_PROT_DEFAULT,
					     PLANE_VM_PROT_ALL, &vaddr),
				     false);
	after = plane_vm_map_get_stats(&test_map);
	failures += test_expect_u64("object overflow ref unchanged",
				    plane_vm_object_ref_count(&test_object), 1);
	failures += test_expect_u64("object overflow free unchanged",
				    after.free_pages, before.free_pages);
	failures += test_expect_u64("object overflow allocations unchanged",
				    after.allocation_count,
				    before.allocation_count);
	return failures;
}

static int test_object_allocation_rejects_invalid_lifetime(void)
{
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("object lifetime map init",
				     test_map_init(&test_map, test_entries,
						       TEST_MAP_ENTRIES,
						       TEST_KERNEL_MAP_BASE,
						       TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("object lifetime uninit rejected",
				     test_map_enter_pages_object(
					     &test_map, 1, 0, &test_object, 0,
					     PLANE_VM_PROT_DEFAULT,
					     PLANE_VM_PROT_ALL, &vaddr),
				     false);
	failures += test_expect_bool("object lifetime init",
				     plane_vm_object_init(&test_object,
							  TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("object lifetime deallocate",
				     plane_vm_object_deallocate(&test_object),
				     true);
	failures += test_expect_bool("object lifetime dead rejected",
				     test_map_enter_pages_object(
					     &test_map, 1, 0, &test_object, 0,
					     PLANE_VM_PROT_DEFAULT,
					     PLANE_VM_PROT_ALL, &vaddr),
				     false);
	failures += test_expect_u64("object lifetime ref unchanged",
				    plane_vm_object_ref_count(&test_object), 0);
	return failures;
}

static int test_object_allocation_failures_keep_ref_count(void)
{
	struct plane_vm_map_entry small_entries[1];
	struct plane_vm_map small_map = {0};
	uint64_t vaddr = 0;
	int failures = 0;

	small_entries[0] = (struct plane_vm_map_entry){0};
	failures += test_expect_bool("object failure map init",
				     test_map_init(&small_map, small_entries,
						       TEST_ARRAY_SIZE(small_entries),
						       TEST_KERNEL_MAP_BASE,
						       2 * PAGE_SIZE),
				     true);
	failures += test_expect_bool("object failure init",
				     plane_vm_object_init(&test_object,
							  TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("object failure no space",
				     test_map_enter_pages_object(
					     &small_map, 3, 0, &test_object, 0,
					     PLANE_VM_PROT_DEFAULT,
					     PLANE_VM_PROT_ALL, &vaddr),
				     false);
	failures += test_expect_bool("object failure bad prot",
				     test_map_enter_pages_object(
					     &small_map, 1, 0, &test_object, 0,
					     PLANE_VM_PROT_NONE,
					     PLANE_VM_PROT_ALL, &vaddr),
				     false);
	failures += test_expect_bool("object failure first alloc",
				     test_map_enter_pages_object(
					     &small_map, 1, 0, &test_object, 0,
					     PLANE_VM_PROT_DEFAULT,
					     PLANE_VM_PROT_ALL, &vaddr),
				     true);
	failures += test_expect_u64("object failure allocated ref",
				    plane_vm_object_ref_count(&test_object), 2);
	failures += test_expect_bool("object failure exhausted entries",
				     test_map_enter_pages_object(
					     &small_map, 1, 0, &test_object,
					     PAGE_SIZE, PLANE_VM_PROT_DEFAULT,
					     PLANE_VM_PROT_ALL, &vaddr),
				     false);
	failures += test_expect_u64("object failure ref rollback",
				    plane_vm_object_ref_count(&test_object), 2);
	return failures;
}

static int test_protected_max_allocation_records_explicit_max(void)
{
	struct plane_vm_map_allocation_info info = {0};
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("max init",
				     test_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool(
		"max readonly alloc",
		test_map_enter_pages_protected_max(&test_map,
			1, 0, PLANE_VM_PROT_READ, PLANE_VM_PROT_READ, &vaddr),
		true);
	failures += test_expect_bool(
		"max readonly lookup",
		test_map_lookup_allocation(&test_map, vaddr, 1, &info),
		true);
	failures += test_expect_u32("max readonly prot",
				    info.prot, PLANE_VM_PROT_READ);
	failures += test_expect_u32("max readonly max",
				    info.max_prot, PLANE_VM_PROT_READ);
	failures += test_expect_bool(
		"max readonly protect read",
		test_map_protect_pages(&test_map, vaddr, 1, PLANE_VM_PROT_READ),
		true);
	failures += test_expect_bool(
		"max readonly reject write",
		test_map_protect_pages(&test_map, vaddr, 1, PLANE_VM_PROT_WRITE),
		false);
	failures += test_expect_bool(
		"max readonly reject rw",
		test_map_protect_pages(&test_map, vaddr, 1, PLANE_VM_PROT_DEFAULT),
		false);
	return failures;
}

static int test_protected_max_allocation_rejects_invalid_pairs(void)
{
	uint64_t vaddr = 0;
	struct plane_vm_map_stats before;
	struct plane_vm_map_stats after;
	int failures = 0;

	failures += test_expect_bool("max reject init",
				     test_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     true);
	before = plane_vm_map_get_stats(&test_map);
	failures += test_expect_bool(
		"max rejects prot none",
		test_map_enter_pages_protected_max(&test_map,
			1, 0, PLANE_VM_PROT_NONE, PLANE_VM_PROT_ALL, &vaddr),
		false);
	failures += test_expect_bool(
		"max rejects max none",
		test_map_enter_pages_protected_max(&test_map,
			1, 0, PLANE_VM_PROT_READ, PLANE_VM_PROT_NONE, &vaddr),
		false);
	failures += test_expect_bool(
		"max rejects prot outside max",
		test_map_enter_pages_protected_max(&test_map,
			1, 0, PLANE_VM_PROT_DEFAULT, PLANE_VM_PROT_READ, &vaddr),
		false);
	failures += test_expect_bool(
		"max rejects unknown prot",
		test_map_enter_pages_protected_max(&test_map,
			1, 0, BIT(8), PLANE_VM_PROT_ALL, &vaddr),
		false);
	failures += test_expect_bool(
		"max rejects unknown max",
		test_map_enter_pages_protected_max(&test_map,
			1, 0, PLANE_VM_PROT_READ, BIT(8), &vaddr),
		false);
	after = plane_vm_map_get_stats(&test_map);
	failures += test_expect_u64("max reject free unchanged",
				    after.free_pages, before.free_pages);
	failures += test_expect_u64("max reject reserved unchanged",
				    after.reserved_pages, before.reserved_pages);
	failures += test_expect_u64("max reject user unchanged",
				    after.user_pages, before.user_pages);
	failures += test_expect_u64("max reject count unchanged",
				    after.allocation_count, before.allocation_count);
	return failures;
}

int main(void)
{
	static const struct test_case cases[] = {
		TEST_CASE(test_rejects_invalid_init),
		TEST_CASE(test_init_stats),
		TEST_CASE(test_init_is_one_shot_in_production_mode),
		TEST_CASE(test_rehome_entries_rejects_invalid_inputs),
		TEST_CASE(test_rehome_entries_preserves_map_behavior),
		TEST_CASE(test_alloc_and_free_pages),
		TEST_CASE(test_anywhere_enter_allocates_anonymous_object),
		TEST_CASE(test_fixed_enter_allocates_anonymous_object),
		TEST_CASE(test_va_only_enter_reserves_without_object_backing),
		TEST_CASE(test_lookup_page_uses_user_page_semantics),
		TEST_CASE(test_lookup_page_tracks_split_object_offsets),
		TEST_CASE(test_free_releases_anonymous_object_slot),
		TEST_CASE(test_delete_releases_anonymous_object_slot),
		TEST_CASE(test_overwrite_releases_old_anonymous_object_slot),
		TEST_CASE(test_anonymous_allocation_failure_keeps_map_state),
		TEST_CASE(test_rejects_invalid_alloc_and_free),
		TEST_CASE(test_partial_free_clips_object_offsets_and_refs),
		TEST_CASE(test_partial_free_rejects_exhausted_split_entries),
		TEST_CASE(test_rejects_exhausted_vaddr_space),
		TEST_CASE(test_rejects_exhausted_entries),
		TEST_CASE(test_first_fit_reuses_lowest_hole),
		TEST_CASE(test_holes_merge_after_entry_removal),
		TEST_CASE(test_delete_range_removes_single_entry),
		TEST_CASE(test_delete_range_removes_multiple_entries),
		TEST_CASE(test_delete_range_removes_guarded_entry),
		TEST_CASE(test_delete_range_empty_hole_is_noop),
		TEST_CASE(test_delete_range_rejects_invalid_ranges),
		TEST_CASE(test_delete_range_clips_partial_overlap),
		TEST_CASE(test_delete_range_rejects_guarded_partial_overlap),
		TEST_CASE(test_delete_range_rejects_wired_entry),
		TEST_CASE(test_delete_range_rejects_object_ref_release_failure),
		TEST_CASE(test_delete_range_releases_object_reference),
		TEST_CASE(test_fixed_overwrite_reuses_zapped_entry_slot),
		TEST_CASE(test_fixed_enter_succeeds_in_empty_hole),
		TEST_CASE(test_fixed_enter_rejects_invalid_ranges),
		TEST_CASE(test_fixed_enter_rejects_overlap_without_overwrite),
		TEST_CASE(test_fixed_overwrite_replaces_single_entry),
		TEST_CASE(test_fixed_overwrite_replaces_multiple_entries),
		TEST_CASE(test_fixed_overwrite_rejects_partial_overlap),
		TEST_CASE(test_fixed_overwrite_rejects_wired_entry),
		TEST_CASE(test_fixed_overwrite_rejects_object_ref_release_failure),
		TEST_CASE(test_guarded_alloc_reserves_unmapped_sentinels),
		TEST_CASE(test_guarded_alloc_rejects_invalid_ranges),
		TEST_CASE(test_protected_alloc_rejects_invalid_protection),
		TEST_CASE(test_protected_alloc_accepts_write_only_protection),
		TEST_CASE(test_protected_guarded_alloc_keeps_user_range_semantics),
		TEST_CASE(test_protect_pages_updates_exact_allocation),
		TEST_CASE(test_protect_pages_clips_middle_fragment),
		TEST_CASE(test_protect_pages_rejects_hole_in_range),
		TEST_CASE(test_protect_pages_rejects_exhausted_split_entries),
		TEST_CASE(test_protect_pages_rejects_invalid_ranges),
		TEST_CASE(test_protect_max_pages_updates_exact_allocation),
		TEST_CASE(test_protect_max_pages_clips_object_offsets_and_refs),
		TEST_CASE(test_protect_max_pages_updates_contiguous_entries),
		TEST_CASE(test_protect_max_pages_rejects_hole_in_range),
		TEST_CASE(test_protect_max_pages_rejects_exhausted_split_entries),
		TEST_CASE(test_protect_max_pages_rejects_invalid_ranges),
		TEST_CASE(test_wire_pages_updates_exact_allocation),
		TEST_CASE(test_wire_pages_clips_middle_fragment),
		TEST_CASE(test_unwire_pages_clips_middle_fragment),
		TEST_CASE(test_wire_pages_updates_contiguous_entries),
		TEST_CASE(test_wire_pages_guarded_range_uses_user_pages),
		TEST_CASE(test_wire_pages_rejects_hole_in_range),
		TEST_CASE(test_wire_pages_rejects_exhausted_split_entries),
		TEST_CASE(test_wire_pages_rejects_invalid_ranges),
		TEST_CASE(test_object_allocation_records_object_and_offset),
		TEST_CASE(test_object_auto_offset_uses_user_range),
		TEST_CASE(test_object_auto_offset_uses_user_va_across_maps),
		TEST_CASE(test_object_allocation_rejects_invalid_offset),
		TEST_CASE(test_object_allocation_validates_object_range),
		TEST_CASE(test_object_auto_offset_validates_object_range),
		TEST_CASE(test_object_allocation_rejects_offset_overflow),
		TEST_CASE(test_object_allocation_rejects_invalid_lifetime),
		TEST_CASE(test_object_allocation_failures_keep_ref_count),
		TEST_CASE(test_protected_max_allocation_records_explicit_max),
		TEST_CASE(test_protected_max_allocation_rejects_invalid_pairs),
	};

	return test_run_cases_with_fixture("vm_map_test", cases,
					   TEST_ARRAY_SIZE(cases),
					   reset_vm_map_test,
					   NULL);
}
