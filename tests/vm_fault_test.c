#include <stdbool.h>
#include <stdint.h>

#include <hal/mmu.h>
#include <plane/mm.h>
#include <plane/vm_fault.h>
#include <plane/vm_map.h>
#include <plane/vm_object.h>
#include <plane/vm_page.h>

#include "support/test.h"
#include "../kernel/mm/vm_object_internal.h"
#include "../kernel/mm/vm_page_internal.h"

#define TEST_MAP_BASE 0xffff900000000000ull
#define TEST_MAP_PAGES 32
#define TEST_MAP_SIZE (TEST_MAP_PAGES * PAGE_SIZE)
#define TEST_MAP_ENTRIES 32
#define TEST_PAGE_COUNT 16
#define TEST_MAPPING_COUNT 16

struct plane_page {
	uint64_t phys_addr;
	uint64_t wire_count;
	struct plane_vm_object *object;
	uint64_t object_offset;
	struct plane_page *object_prev;
	struct plane_page *object_next;
	struct plane_page *object_hash_next;
	bool object_tabled;
	bool object_hashed;
	bool allocated;
};

struct test_mapping {
	uint64_t vaddr;
	uint64_t phys_addr;
	uint32_t flags;
	bool used;
};

static struct plane_vm_map_entry test_entries[TEST_MAP_ENTRIES];
static struct plane_vm_map test_map;
static struct plane_vm_object test_object;
static struct plane_page test_pages[TEST_PAGE_COUNT];
static struct test_mapping test_mappings[TEST_MAPPING_COUNT];
static uint32_t last_grab_flags;
static bool grab_force_fail;
static bool attach_force_fail;
static bool map_force_fail;
static bool protect_force_fail;
static uint64_t wire_call_count;
static uint64_t wire_fail_after;
static uint64_t map_call_count;
static uint64_t protect_call_count;

static uint64_t page_vaddr(uint64_t page)
{
	return TEST_MAP_BASE + page * PAGE_SIZE;
}

static plane_vaddr_t test_vaddr(uint64_t raw)
{
	return plane_vaddr_make(raw);
}

static uint64_t test_paddr_raw(plane_paddr_t addr)
{
	return plane_paddr_raw(addr);
}

static uint64_t test_page_phys(uint64_t page)
{
	return 0x100000 + page * PAGE_SIZE;
}

static bool is_test_page(const struct plane_page *page)
{
	return page != NULL &&
	       page >= &test_pages[0] &&
	       page < &test_pages[TEST_PAGE_COUNT];
}

static uint64_t allocated_page_count(void)
{
	uint64_t count = 0;

	for (uint64_t i = 0; i < TEST_PAGE_COUNT; i++) {
		if (test_pages[i].allocated) {
			count++;
		}
	}

	return count;
}

static uint64_t wired_page_count(void)
{
	uint64_t count = 0;

	for (uint64_t i = 0; i < TEST_PAGE_COUNT; i++) {
		if (test_pages[i].wire_count != 0) {
			count++;
		}
	}

	return count;
}

static uint64_t mapping_count(void)
{
	uint64_t count = 0;

	for (uint64_t i = 0; i < TEST_MAPPING_COUNT; i++) {
		if (test_mappings[i].used) {
			count++;
		}
	}

	return count;
}

static struct test_mapping *find_mapping(uint64_t vaddr)
{
	for (uint64_t i = 0; i < TEST_MAPPING_COUNT; i++) {
		if (test_mappings[i].used && test_mappings[i].vaddr == vaddr) {
			return &test_mappings[i];
		}
	}

	return NULL;
}

static void reset_vm_fault_test(void)
{
	plane_vm_object_reset_bootstrap_for_tests();
	test_map = (struct plane_vm_map){0};
	test_object = (struct plane_vm_object){0};
	for (uint64_t i = 0; i < TEST_MAP_ENTRIES; i++) {
		test_entries[i] = (struct plane_vm_map_entry){0};
	}
	for (uint64_t i = 0; i < TEST_PAGE_COUNT; i++) {
		test_pages[i] = (struct plane_page){
			.phys_addr = test_page_phys(i),
		};
	}
	for (uint64_t i = 0; i < TEST_MAPPING_COUNT; i++) {
		test_mappings[i] = (struct test_mapping){0};
	}
	last_grab_flags = 0;
	grab_force_fail = false;
	attach_force_fail = false;
	map_force_fail = false;
	protect_force_fail = false;
	wire_call_count = 0;
	wire_fail_after = UINT64_MAX;
	map_call_count = 0;
	protect_call_count = 0;
}

static bool init_map_and_object(uint64_t object_size)
{
	return plane_vm_object_init(&test_object, object_size) &&
	       plane_vm_map_init(&test_map, test_entries, TEST_MAP_ENTRIES,
				 test_vaddr(TEST_MAP_BASE), TEST_MAP_SIZE);
}

static bool enter_object(uint64_t address,
			 uint64_t page_count,
			 uint64_t object_offset,
			 uint32_t prot,
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
			.object = &test_object,
			.object_offset = object_offset,
			.prot = prot,
			.max_prot = PLANE_VM_PROT_ALL,
			.flags = PLANE_VM_MAP_ENTER_FIXED,
		},
		&out);
	if (ok) {
		*vaddr = plane_vaddr_raw(out);
	}
	return ok;
}

static bool test_map_wire_pages(struct plane_vm_map *map,
				uint64_t vaddr,
				uint64_t page_count)
{
	return plane_vm_map_wire_pages(map, test_vaddr(vaddr), page_count);
}

static bool test_fault_page(struct plane_vm_map *map,
			    uint64_t vaddr,
			    uint32_t fault_type)
{
	return plane_vm_fault_page(map, test_vaddr(vaddr), fault_type);
}

static bool test_fault_pages(struct plane_vm_map *map,
			     uint64_t vaddr,
			     uint64_t page_count,
			     uint32_t fault_type)
{
	return plane_vm_fault_pages(map, test_vaddr(vaddr), page_count,
				    fault_type);
}

static bool test_fault_wire_pages(struct plane_vm_map *map,
				  uint64_t vaddr,
				  uint64_t page_count,
				  uint32_t fault_type)
{
	return plane_vm_fault_wire_pages(map, test_vaddr(vaddr), page_count,
					 fault_type);
}

static bool test_fault_unwire_pages(struct plane_vm_map *map,
				    uint64_t vaddr,
				    uint64_t page_count)
{
	return plane_vm_fault_unwire_pages(map, test_vaddr(vaddr), page_count);
}

static uint64_t lookup_page_wired_count(uint64_t vaddr)
{
	struct plane_vm_map_page_info info;

	if (!plane_vm_map_lookup_page(&test_map, test_vaddr(vaddr), &info)) {
		return UINT64_MAX;
	}

	return info.wired_count;
}

static int expect_page_wire_count(const char *name,
				  struct plane_page *page,
				  uint64_t expected)
{
	uint64_t wire_count = UINT64_MAX;
	int failures = 0;

	failures += test_expect_bool(name,
				     plane_vm_page_wire_count(page,
							      &wire_count),
				     true);
	failures += test_expect_u64(name, wire_count, expected);
	return failures;
}

bool hal_mmu_map_kernel_page(plane_vaddr_t vaddr,
			     plane_paddr_t phys_addr,
			     uint32_t flags)
{
	uint64_t raw_vaddr = plane_vaddr_raw(vaddr);
	uint64_t raw_phys = plane_paddr_raw(phys_addr);

	if (map_force_fail ||
	    (flags & ~HAL_MMU_MAP_WRITE) != 0 ||
	    find_mapping(raw_vaddr) != NULL) {
		return false;
	}

	for (uint64_t i = 0; i < TEST_MAPPING_COUNT; i++) {
		if (!test_mappings[i].used) {
			test_mappings[i] = (struct test_mapping){
				.vaddr = raw_vaddr,
				.phys_addr = raw_phys,
				.flags = flags,
				.used = true,
			};
			map_call_count++;
			return true;
		}
	}

	return false;
}

bool hal_mmu_translate_kernel_page(plane_vaddr_t vaddr,
				   plane_paddr_t *phys_addr)
{
	struct test_mapping *mapping;

	if (phys_addr == NULL) {
		return false;
	}

	mapping = find_mapping(plane_vaddr_raw(vaddr));
	if (mapping == NULL) {
		return false;
	}

	*phys_addr = plane_paddr_make(mapping->phys_addr);
	return true;
}

bool hal_mmu_protect_kernel_page(plane_vaddr_t vaddr, uint32_t flags)
{
	struct test_mapping *mapping;

	if (protect_force_fail || (flags & ~HAL_MMU_MAP_WRITE) != 0) {
		return false;
	}

	mapping = find_mapping(plane_vaddr_raw(vaddr));
	if (mapping == NULL) {
		return false;
	}

	mapping->flags = flags;
	protect_call_count++;
	return true;
}

bool plane_vm_page_grab(uint32_t flags, struct plane_page **page)
{
	if (page == NULL || (flags & ~PLANE_VM_PAGE_GRAB_ZERO) != 0) {
		return false;
	}
	last_grab_flags = flags;
	if (grab_force_fail) {
		return false;
	}

	for (uint64_t i = 0; i < TEST_PAGE_COUNT; i++) {
		if (!test_pages[i].allocated) {
			test_pages[i].allocated = true;
			*page = &test_pages[i];
			return true;
		}
	}

	return false;
}

bool plane_vm_page_release(struct plane_page *page)
{
	if (!is_test_page(page) ||
	    !page->allocated ||
	    page->wire_count != 0 ||
	    page->object != NULL) {
		return false;
	}

	page->allocated = false;
	return true;
}

plane_paddr_t plane_vm_page_phys(const struct plane_page *page)
{
	if (!is_test_page(page) || !page->allocated) {
		return PLANE_VM_PAGE_NO_PHYS;
	}

	return plane_paddr_make(page->phys_addr);
}

enum plane_vm_page_state plane_vm_page_state(const struct plane_page *page)
{
	if (!is_test_page(page) || !page->allocated) {
		return PLANE_VM_PAGE_INVALID;
	}

	return PLANE_VM_PAGE_ALLOCATED;
}

bool plane_vm_page_wire(struct plane_page *page)
{
	if (wire_call_count >= wire_fail_after ||
	    !is_test_page(page) ||
	    !page->allocated ||
	    page->wire_count == UINT64_MAX) {
		return false;
	}

	if (page->object != NULL &&
	    page->wire_count == 0 &&
	    !plane_vm_object_page_became_wired(page->object)) {
		return false;
	}

	page->wire_count++;
	wire_call_count++;
	return true;
}

bool plane_vm_page_unwire(struct plane_page *page)
{
	if (!is_test_page(page) ||
	    !page->allocated ||
	    page->wire_count == 0) {
		return false;
	}

	if (page->object != NULL &&
	    page->wire_count == 1 &&
	    !plane_vm_object_page_became_unwired(page->object)) {
		return false;
	}

	page->wire_count--;
	return true;
}

bool plane_vm_page_wire_count(const struct plane_page *page,
			      uint64_t *wire_count)
{
	if (wire_count == NULL || !is_test_page(page) || !page->allocated) {
		return false;
	}

	*wire_count = page->wire_count;
	return true;
}

struct plane_vm_object *plane_vm_page_object(const struct plane_page *page)
{
	if (!is_test_page(page) || !page->allocated) {
		return NULL;
	}

	return page->object;
}

bool plane_vm_page_object_offset(const struct plane_page *page,
				 uint64_t *offset)
{
	if (offset == NULL ||
	    !is_test_page(page) ||
	    !page->allocated ||
	    page->object == NULL) {
		return false;
	}

	*offset = page->object_offset;
	return true;
}

bool plane_vm_page_attach_object(struct plane_page *page,
				 struct plane_vm_object *object,
				 uint64_t offset)
{
	if (attach_force_fail ||
	    !is_test_page(page) ||
	    !page->allocated ||
	    object == NULL ||
	    page->object != NULL) {
		return false;
	}

	page->object = object;
	page->object_offset = offset;
	page->object_prev = NULL;
	page->object_next = NULL;
	page->object_hash_next = NULL;
	page->object_tabled = false;
	page->object_hashed = false;
	return true;
}

bool plane_vm_page_detach_object(struct plane_page *page,
				 struct plane_vm_object *object,
				 uint64_t offset)
{
	if (!is_test_page(page) ||
	    !page->allocated ||
	    page->object != object ||
	    page->object_offset != offset) {
		return false;
	}

	page->object = NULL;
	page->object_offset = 0;
	page->object_prev = NULL;
	page->object_next = NULL;
	page->object_hash_next = NULL;
	page->object_tabled = false;
	page->object_hashed = false;
	return true;
}

struct plane_page *plane_vm_page_object_prev(const struct plane_page *page)
{
	return is_test_page(page) ? page->object_prev : NULL;
}

struct plane_page *plane_vm_page_object_next(const struct plane_page *page)
{
	return is_test_page(page) ? page->object_next : NULL;
}

struct plane_page *plane_vm_page_object_hash_next(const struct plane_page *page)
{
	return is_test_page(page) ? page->object_hash_next : NULL;
}

bool plane_vm_page_object_tabled(const struct plane_page *page)
{
	return is_test_page(page) && page->object_tabled;
}

bool plane_vm_page_object_hashed(const struct plane_page *page)
{
	return is_test_page(page) && page->object_hashed;
}

bool plane_vm_page_set_object_prev(struct plane_page *page,
				   struct plane_page *prev)
{
	if (!is_test_page(page) || (prev != NULL && !is_test_page(prev))) {
		return false;
	}

	page->object_prev = prev;
	return true;
}

bool plane_vm_page_set_object_next(struct plane_page *page,
				   struct plane_page *next)
{
	if (!is_test_page(page) || (next != NULL && !is_test_page(next))) {
		return false;
	}

	page->object_next = next;
	return true;
}

bool plane_vm_page_set_object_hash_next(struct plane_page *page,
					struct plane_page *next)
{
	if (!is_test_page(page) || (next != NULL && !is_test_page(next))) {
		return false;
	}

	page->object_hash_next = next;
	return true;
}

bool plane_vm_page_set_object_tabled(struct plane_page *page, bool tabled)
{
	if (!is_test_page(page)) {
		return false;
	}

	page->object_tabled = tabled;
	return true;
}

bool plane_vm_page_set_object_hashed(struct plane_page *page, bool hashed)
{
	if (!is_test_page(page)) {
		return false;
	}

	page->object_hashed = hashed;
	return true;
}

static int test_fault_rejects_invalid_or_unmapped_access(void)
{
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("fault invalid init",
				     init_map_and_object(2 * PAGE_SIZE), true);
	failures += test_expect_bool("fault invalid enter",
				     enter_object(page_vaddr(1), 1, 0,
						  PLANE_VM_PROT_READ,
						  &vaddr),
				     true);
	failures += test_expect_bool("fault null map",
				     test_fault_page(NULL, vaddr,
							 PLANE_VM_PROT_READ),
				     false);
	failures += test_expect_bool("fault none",
				     test_fault_page(&test_map, vaddr,
							 PLANE_VM_PROT_NONE),
				     false);
	failures += test_expect_bool("fault unknown prot",
				     test_fault_page(&test_map, vaddr,
							 BIT(7)),
				     false);
	failures += test_expect_bool("fault hole",
				     test_fault_page(&test_map,
							 page_vaddr(3),
							 PLANE_VM_PROT_READ),
				     false);
	failures += test_expect_bool("fault write denied",
				     test_fault_page(&test_map, vaddr,
							 PLANE_VM_PROT_WRITE),
				     false);
	failures += test_expect_u64("fault invalid allocated",
				    allocated_page_count(), 0);
	failures += test_expect_u64("fault invalid resident",
				    plane_vm_object_resident_page_count(
					    &test_object),
				    0);
	failures += test_expect_u64("fault invalid mappings",
				    mapping_count(), 0);
	return failures;
}

static int test_fault_miss_allocates_zero_page_and_maps(void)
{
	struct test_mapping *mapping;
	struct plane_page *page;
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("fault miss init",
				     init_map_and_object(2 * PAGE_SIZE), true);
	failures += test_expect_bool("fault miss enter",
				     enter_object(page_vaddr(2), 2, 0,
						  PLANE_VM_PROT_DEFAULT,
						  &vaddr),
				     true);
	failures += test_expect_bool("fault miss page",
				     test_fault_page(&test_map,
							 vaddr + 123,
							 PLANE_VM_PROT_READ),
				     true);
	page = plane_vm_object_lookup_page(&test_object, 0);
	mapping = find_mapping(vaddr);
	failures += test_expect_not_null("fault miss resident", page);
	failures += test_expect_not_null("fault miss mapping", mapping);
	failures += test_expect_u64("fault miss allocated",
				    allocated_page_count(), 1);
	failures += test_expect_u64("fault miss resident count",
				    plane_vm_object_resident_page_count(
					    &test_object),
				    1);
	failures += test_expect_u32("fault miss zero flag",
				    last_grab_flags, PLANE_VM_PAGE_GRAB_ZERO);
	if (page != NULL && mapping != NULL) {
		failures += test_expect_u64("fault miss mapped phys",
					    mapping->phys_addr,
					    test_paddr_raw(
						    plane_vm_page_phys(page)));
		failures += test_expect_u32("fault miss map flags",
					    mapping->flags,
					    HAL_MMU_MAP_WRITE);
	}
	return failures;
}

static int test_fault_unaligned_address_uses_page_object_offset(void)
{
	struct test_mapping *mapping;
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("fault offset init",
				     init_map_and_object(4 * PAGE_SIZE), true);
	failures += test_expect_bool("fault offset enter",
				     enter_object(page_vaddr(4), 2,
						  PAGE_SIZE,
						  PLANE_VM_PROT_DEFAULT,
						  &vaddr),
				     true);
	failures += test_expect_bool("fault offset page",
				     test_fault_page(
					     &test_map, vaddr + PAGE_SIZE + 19,
					     PLANE_VM_PROT_WRITE),
				     true);
	mapping = find_mapping(vaddr + PAGE_SIZE);
	failures += test_expect_not_null("fault offset mapping", mapping);
	failures += test_expect_not_null(
		"fault offset resident",
		plane_vm_object_lookup_page(&test_object, 2 * PAGE_SIZE));
	failures += test_expect_null(
		"fault offset wrong resident",
		plane_vm_object_lookup_page(&test_object, PAGE_SIZE));
	return failures;
}

static int test_fault_resident_hit_repairs_absent_pmap(void)
{
	struct plane_page *page;
	struct test_mapping *mapping;
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("fault hit init",
				     init_map_and_object(PAGE_SIZE), true);
	failures += test_expect_bool("fault hit enter",
				     enter_object(page_vaddr(1), 1, 0,
						  PLANE_VM_PROT_READ,
						  &vaddr),
				     true);
	failures += test_expect_bool("fault hit grab",
				     plane_vm_page_grab(0, &page), true);
	failures += test_expect_bool("fault hit insert",
				     plane_vm_object_insert_page(
					     &test_object, 0, page),
				     true);
	last_grab_flags = 0;
	failures += test_expect_bool("fault hit page",
				     test_fault_page(&test_map, vaddr,
							 PLANE_VM_PROT_READ),
				     true);
	mapping = find_mapping(vaddr);
	failures += test_expect_not_null("fault hit mapping", mapping);
	failures += test_expect_u64("fault hit allocated unchanged",
				    allocated_page_count(), 1);
	failures += test_expect_u32("fault hit no new grab",
				    last_grab_flags, 0);
	failures += test_expect_u64("fault hit map calls", map_call_count, 1);
	if (mapping != NULL) {
		failures += test_expect_u32("fault hit readonly flags",
					    mapping->flags, 0);
	}
	return failures;
}

static int test_fault_resident_hit_protects_existing_pmap(void)
{
	struct plane_page *page;
	struct test_mapping *mapping;
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("fault protect init",
				     init_map_and_object(PAGE_SIZE), true);
	failures += test_expect_bool("fault protect enter",
				     enter_object(page_vaddr(1), 1, 0,
						  PLANE_VM_PROT_READ,
						  &vaddr),
				     true);
	failures += test_expect_bool("fault protect grab",
				     plane_vm_page_grab(0, &page), true);
	failures += test_expect_bool("fault protect insert",
				     plane_vm_object_insert_page(
					     &test_object, 0, page),
				     true);
	failures += test_expect_bool("fault protect premap",
				     hal_mmu_map_kernel_page(
					     test_vaddr(vaddr),
					     plane_vm_page_phys(page),
					     HAL_MMU_MAP_WRITE),
				     true);
	failures += test_expect_bool("fault protect page",
				     test_fault_page(&test_map, vaddr,
							 PLANE_VM_PROT_READ),
				     true);
	mapping = find_mapping(vaddr);
	failures += test_expect_u64("fault protect map calls",
				    map_call_count, 1);
	failures += test_expect_u64("fault protect calls",
				    protect_call_count, 1);
	if (mapping != NULL) {
		failures += test_expect_u32("fault protect flags",
					    mapping->flags, 0);
	}
	return failures;
}

static int test_fault_resident_hit_rolls_back_protect_failure(void)
{
	struct plane_page *page;
	struct test_mapping *mapping;
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("fault protect fail init",
				     init_map_and_object(PAGE_SIZE), true);
	failures += test_expect_bool("fault protect fail enter",
				     enter_object(page_vaddr(1), 1, 0,
						  PLANE_VM_PROT_READ,
						  &vaddr),
				     true);
	failures += test_expect_bool("fault protect fail grab",
				     plane_vm_page_grab(0, &page), true);
	failures += test_expect_bool("fault protect fail insert",
				     plane_vm_object_insert_page(
					     &test_object, 0, page),
				     true);
	failures += test_expect_bool("fault protect fail premap",
				     hal_mmu_map_kernel_page(
					     test_vaddr(vaddr),
					     plane_vm_page_phys(page),
					     HAL_MMU_MAP_WRITE),
				     true);
	protect_force_fail = true;
	failures += test_expect_bool("fault protect fail page",
				     test_fault_page(&test_map, vaddr,
							 PLANE_VM_PROT_READ),
				     false);
	mapping = find_mapping(vaddr);
	failures += test_expect_not_null("fault protect fail mapping",
					 mapping);
	if (mapping != NULL) {
		failures += test_expect_u32("fault protect fail flags",
					    mapping->flags,
					    HAL_MMU_MAP_WRITE);
	}
	failures += test_expect_u64("fault protect fail resident",
				    plane_vm_object_resident_page_count(
					    &test_object),
				    1);
	failures += test_expect_u64("fault protect fail allocated",
				    allocated_page_count(), 1);
	failures += test_expect_u64("fault protect fail calls",
				    protect_call_count, 0);
	return failures;
}

static int test_fault_resident_hit_rejects_wrong_pmap_phys(void)
{
	struct plane_page *page;
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("fault wrong init",
				     init_map_and_object(PAGE_SIZE), true);
	failures += test_expect_bool("fault wrong enter",
				     enter_object(page_vaddr(1), 1, 0,
						  PLANE_VM_PROT_READ,
						  &vaddr),
				     true);
	failures += test_expect_bool("fault wrong grab",
				     plane_vm_page_grab(0, &page), true);
	failures += test_expect_bool("fault wrong insert",
				     plane_vm_object_insert_page(
					     &test_object, 0, page),
				     true);
	test_mappings[0] = (struct test_mapping){
		.vaddr = vaddr,
		.phys_addr = test_paddr_raw(plane_vm_page_phys(page)) +
			     PAGE_SIZE,
		.used = true,
	};
	failures += test_expect_bool("fault wrong phys",
				     test_fault_page(&test_map, vaddr,
							 PLANE_VM_PROT_READ),
				     false);
	failures += test_expect_u64("fault wrong resident unchanged",
				    plane_vm_object_resident_page_count(
					    &test_object),
				    1);
	failures += test_expect_u64("fault wrong protect calls",
				    protect_call_count, 0);
	return failures;
}

static int test_fault_resident_hit_rejects_invalid_phys(void)
{
	struct plane_page *page;
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("fault invalid phys init",
				     init_map_and_object(PAGE_SIZE), true);
	failures += test_expect_bool("fault invalid phys enter",
				     enter_object(page_vaddr(1), 1, 0,
						  PLANE_VM_PROT_READ,
						  &vaddr),
				     true);
	failures += test_expect_bool("fault invalid phys grab",
				     plane_vm_page_grab(0, &page), true);
	page->phys_addr = PLANE_VM_PAGE_NO_PHYS_RAW;
	failures += test_expect_bool("fault invalid phys insert",
				     plane_vm_object_insert_page(
					     &test_object, 0, page),
				     true);
	failures += test_expect_bool("fault invalid phys page",
				     test_fault_page(&test_map, vaddr,
							 PLANE_VM_PROT_READ),
				     false);
	failures += test_expect_u64("fault invalid phys resident",
				    plane_vm_object_resident_page_count(
					    &test_object),
				    1);
	failures += test_expect_u64("fault invalid phys allocated",
				    allocated_page_count(), 1);
	failures += test_expect_u64("fault invalid phys map calls",
				    map_call_count, 0);
	failures += test_expect_u64("fault invalid phys protect calls",
				    protect_call_count, 0);
	failures += test_expect_u64("fault invalid phys mappings",
				    mapping_count(), 0);
	return failures;
}

static int test_fault_miss_rejects_stale_pmap_mapping(void)
{
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("fault stale init",
				     init_map_and_object(PAGE_SIZE), true);
	failures += test_expect_bool("fault stale enter",
				     enter_object(page_vaddr(1), 1, 0,
						  PLANE_VM_PROT_DEFAULT,
						  &vaddr),
				     true);
	test_mappings[0] = (struct test_mapping){
		.vaddr = vaddr,
		.phys_addr = test_page_phys(0),
		.flags = HAL_MMU_MAP_WRITE,
		.used = true,
	};
	failures += test_expect_bool("fault stale page",
				     test_fault_page(&test_map, vaddr,
							 PLANE_VM_PROT_READ),
				     false);
	failures += test_expect_u64("fault stale allocated",
				    allocated_page_count(), 0);
	failures += test_expect_u64("fault stale resident",
				    plane_vm_object_resident_page_count(
					    &test_object),
				    0);
	failures += test_expect_u32("fault stale no grab", last_grab_flags, 0);
	failures += test_expect_u64("fault stale map calls", map_call_count, 0);
	failures += test_expect_u64("fault stale protect calls",
				    protect_call_count, 0);
	failures += test_expect_u64("fault stale mapping kept",
				    mapping_count(), 1);
	return failures;
}

static int test_fault_wired_entry_wires_new_page(void)
{
	struct plane_page *page;
	uint64_t wire_count = 0;
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("fault wire init",
				     init_map_and_object(PAGE_SIZE), true);
	failures += test_expect_bool("fault wire enter",
				     enter_object(page_vaddr(1), 1, 0,
						  PLANE_VM_PROT_DEFAULT,
						  &vaddr),
				     true);
	failures += test_expect_bool("fault wire entry",
				     test_map_wire_pages(&test_map,
							     vaddr, 1),
				     true);
	failures += test_expect_bool("fault wire page",
				     test_fault_page(&test_map, vaddr,
							 PLANE_VM_PROT_READ),
				     true);
	page = plane_vm_object_lookup_page(&test_object, 0);
	failures += test_expect_not_null("fault wire resident", page);
	failures += test_expect_bool("fault wire count query",
				     plane_vm_page_wire_count(page,
							      &wire_count),
				     true);
	failures += test_expect_u64("fault wire count", wire_count, 1);
	failures += test_expect_u64("fault wire allocated wired",
				    wired_page_count(), 1);
	failures += test_expect_u64("fault wire object wired",
				    plane_vm_object_wired_page_count(
					    &test_object),
				    1);
	return failures;
}

static int test_fault_multi_wired_entry_wires_new_page(void)
{
	struct plane_page *page;
	uint64_t wire_count = 0;
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("fault multiwire init",
				     init_map_and_object(PAGE_SIZE), true);
	failures += test_expect_bool("fault multiwire enter",
				     enter_object(page_vaddr(1), 1, 0,
						  PLANE_VM_PROT_DEFAULT,
						  &vaddr),
				     true);
	failures += test_expect_bool("fault multiwire entry first",
				     test_map_wire_pages(&test_map,
							     vaddr, 1),
				     true);
	failures += test_expect_bool("fault multiwire entry second",
				     test_map_wire_pages(&test_map,
							     vaddr, 1),
				     true);
	failures += test_expect_bool("fault multiwire page",
				     test_fault_page(&test_map, vaddr,
							 PLANE_VM_PROT_READ),
				     true);
	page = plane_vm_object_lookup_page(&test_object, 0);
	failures += test_expect_not_null("fault multiwire resident", page);
	failures += test_expect_bool("fault multiwire count query",
				     plane_vm_page_wire_count(page,
							      &wire_count),
				     true);
	failures += test_expect_u64("fault multiwire count", wire_count, 2);
	failures += test_expect_u64("fault multiwire allocated wired",
				    wired_page_count(), 1);
	failures += test_expect_u64("fault multiwire object wired",
				    plane_vm_object_wired_page_count(
					    &test_object),
				    1);
	return failures;
}

static int test_fault_multi_wire_failure_rolls_back_all_wires(void)
{
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("fault multiwire fail init",
				     init_map_and_object(PAGE_SIZE), true);
	failures += test_expect_bool("fault multiwire fail enter",
				     enter_object(page_vaddr(1), 1, 0,
						  PLANE_VM_PROT_DEFAULT,
						  &vaddr),
				     true);
	failures += test_expect_bool("fault multiwire fail entry first",
				     test_map_wire_pages(&test_map,
							     vaddr, 1),
				     true);
	failures += test_expect_bool("fault multiwire fail entry second",
				     test_map_wire_pages(&test_map,
							     vaddr, 1),
				     true);
	wire_fail_after = 1;
	failures += test_expect_bool("fault multiwire fail page",
				     test_fault_page(&test_map, vaddr,
							 PLANE_VM_PROT_READ),
				     false);
	failures += test_expect_u64("fault multiwire fail allocated",
				    allocated_page_count(), 0);
	failures += test_expect_u64("fault multiwire fail wired",
				    wired_page_count(), 0);
	failures += test_expect_u64("fault multiwire fail resident",
				    plane_vm_object_resident_page_count(
					    &test_object),
				    0);
	failures += test_expect_u64("fault multiwire fail object wired",
				    plane_vm_object_wired_page_count(
					    &test_object),
				    0);
	failures += test_expect_u64("fault multiwire fail mappings",
				    mapping_count(), 0);
	return failures;
}

static int test_fault_rolls_back_allocation_failures(void)
{
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("fault rollback init",
				     init_map_and_object(PAGE_SIZE), true);
	failures += test_expect_bool("fault rollback enter",
				     enter_object(page_vaddr(1), 1, 0,
						  PLANE_VM_PROT_DEFAULT,
						  &vaddr),
				     true);
	failures += test_expect_bool("fault rollback wire entry",
				     test_map_wire_pages(&test_map,
							     vaddr, 1),
				     true);

	grab_force_fail = true;
	failures += test_expect_bool("fault grab fail",
				     test_fault_page(&test_map, vaddr,
							 PLANE_VM_PROT_READ),
				     false);
	failures += test_expect_u64("fault grab fail allocated",
				    allocated_page_count(), 0);
	grab_force_fail = false;

	wire_fail_after = 0;
	failures += test_expect_bool("fault wire fail",
				     test_fault_page(&test_map, vaddr,
							 PLANE_VM_PROT_READ),
				     false);
	failures += test_expect_u64("fault wire fail allocated",
				    allocated_page_count(), 0);
	failures += test_expect_u64("fault wire fail wired",
				    wired_page_count(), 0);
	wire_fail_after = UINT64_MAX;

	attach_force_fail = true;
	failures += test_expect_bool("fault insert fail",
				     test_fault_page(&test_map, vaddr,
							 PLANE_VM_PROT_READ),
				     false);
	failures += test_expect_u64("fault insert fail allocated",
				    allocated_page_count(), 0);
	failures += test_expect_u64("fault insert fail resident",
				    plane_vm_object_resident_page_count(
					    &test_object),
				    0);
	attach_force_fail = false;

	map_force_fail = true;
	failures += test_expect_bool("fault map fail",
				     test_fault_page(&test_map, vaddr,
							 PLANE_VM_PROT_READ),
				     false);
	failures += test_expect_u64("fault map fail allocated",
				    allocated_page_count(), 0);
	failures += test_expect_u64("fault map fail wired",
				    wired_page_count(), 0);
	failures += test_expect_u64("fault map fail resident",
				    plane_vm_object_resident_page_count(
					    &test_object),
				    0);
	failures += test_expect_u64("fault map fail object wired",
				    plane_vm_object_wired_page_count(
					    &test_object),
				    0);
	failures += test_expect_u64("fault map fail mappings",
				    mapping_count(), 0);
	return failures;
}

static int test_fault_pages_faults_contiguous_range(void)
{
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("range fault init",
				     init_map_and_object(3 * PAGE_SIZE), true);
	failures += test_expect_bool("range fault enter",
				     enter_object(page_vaddr(1), 3, 0,
						  PLANE_VM_PROT_DEFAULT,
						  &vaddr),
				     true);
	failures += test_expect_bool("range fault pages",
				     test_fault_pages(&test_map, vaddr, 3,
						      PLANE_VM_PROT_READ),
				     true);
	failures += test_expect_u64("range fault allocated",
				    allocated_page_count(), 3);
	failures += test_expect_u64("range fault resident",
				    plane_vm_object_resident_page_count(
					    &test_object),
				    3);
	failures += test_expect_u64("range fault mappings",
				    mapping_count(), 3);
	failures += test_expect_not_null(
		"range fault first resident",
		plane_vm_object_lookup_page(&test_object, 0));
	failures += test_expect_not_null(
		"range fault second resident",
		plane_vm_object_lookup_page(&test_object, PAGE_SIZE));
	failures += test_expect_not_null(
		"range fault third resident",
		plane_vm_object_lookup_page(&test_object, 2 * PAGE_SIZE));
	failures += test_expect_not_null("range fault first mapping",
					 find_mapping(vaddr));
	failures += test_expect_not_null("range fault second mapping",
					 find_mapping(vaddr + PAGE_SIZE));
	failures += test_expect_not_null("range fault third mapping",
					 find_mapping(vaddr + 2 * PAGE_SIZE));
	return failures;
}

static int test_fault_pages_rejects_invalid_inputs(void)
{
	uint64_t vaddr = 0;
	uint64_t overflow_base = UINT64_MAX & ~(PAGE_SIZE - 1);
	int failures = 0;

	failures += test_expect_bool("range invalid init",
				     init_map_and_object(PAGE_SIZE), true);
	failures += test_expect_bool("range invalid enter",
				     enter_object(page_vaddr(1), 1, 0,
						  PLANE_VM_PROT_DEFAULT,
						  &vaddr),
				     true);
	failures += test_expect_bool("range null map",
				     test_fault_pages(NULL, vaddr, 1,
						      PLANE_VM_PROT_READ),
				     false);
	failures += test_expect_bool("range null addr",
				     test_fault_pages(&test_map, 0, 1,
						      PLANE_VM_PROT_READ),
				     false);
	failures += test_expect_bool("range unaligned",
				     test_fault_pages(&test_map, vaddr + 1, 1,
						      PLANE_VM_PROT_READ),
				     false);
	failures += test_expect_bool("range zero pages",
				     test_fault_pages(&test_map, vaddr, 0,
						      PLANE_VM_PROT_READ),
				     false);
	failures += test_expect_bool("range none",
				     test_fault_pages(&test_map, vaddr, 1,
						      PLANE_VM_PROT_NONE),
				     false);
	failures += test_expect_bool("range unknown",
				     test_fault_pages(&test_map, vaddr, 1,
						      BIT(7)),
				     false);
	failures += test_expect_bool("range overflow",
				     test_fault_pages(&test_map, overflow_base,
						      2, PLANE_VM_PROT_READ),
				     false);
	failures += test_expect_u64("range invalid allocated",
				    allocated_page_count(), 0);
	failures += test_expect_u64("range invalid resident",
				    plane_vm_object_resident_page_count(
					    &test_object),
				    0);
	failures += test_expect_u64("range invalid mappings",
				    mapping_count(), 0);
	return failures;
}

static int test_fault_pages_keeps_prefix_before_hole(void)
{
	uint64_t first = 0;
	uint64_t second = 0;
	int failures = 0;

	failures += test_expect_bool("range hole init",
				     init_map_and_object(4 * PAGE_SIZE), true);
	failures += test_expect_bool("range hole first enter",
				     enter_object(page_vaddr(1), 1, 0,
						  PLANE_VM_PROT_DEFAULT,
						  &first),
				     true);
	failures += test_expect_bool("range hole second enter",
				     enter_object(page_vaddr(3), 1,
						  2 * PAGE_SIZE,
						  PLANE_VM_PROT_DEFAULT,
						  &second),
				     true);
	failures += test_expect_bool("range hole pages",
				     test_fault_pages(&test_map, first, 3,
						      PLANE_VM_PROT_READ),
				     false);
	failures += test_expect_u64("range hole allocated",
				    allocated_page_count(), 1);
	failures += test_expect_u64("range hole resident",
				    plane_vm_object_resident_page_count(
					    &test_object),
				    1);
	failures += test_expect_not_null("range hole prefix resident",
					 plane_vm_object_lookup_page(
						 &test_object, 0));
	failures += test_expect_null("range hole suffix untouched",
				     plane_vm_object_lookup_page(
					     &test_object, 2 * PAGE_SIZE));
	failures += test_expect_not_null("range hole prefix mapping",
					 find_mapping(first));
	failures += test_expect_null("range hole suffix mapping",
				     find_mapping(second));
	return failures;
}

static int test_fault_pages_keeps_prefix_before_protection_failure(void)
{
	uint64_t first = 0;
	uint64_t second = 0;
	int failures = 0;

	failures += test_expect_bool("range prot init",
				     init_map_and_object(2 * PAGE_SIZE), true);
	failures += test_expect_bool("range prot first enter",
				     enter_object(page_vaddr(1), 1, 0,
						  PLANE_VM_PROT_DEFAULT,
						  &first),
				     true);
	failures += test_expect_bool("range prot second enter",
				     enter_object(page_vaddr(2), 1,
						  PAGE_SIZE,
						  PLANE_VM_PROT_READ,
						  &second),
				     true);
	failures += test_expect_bool("range prot pages",
				     test_fault_pages(&test_map, first, 2,
						      PLANE_VM_PROT_READ |
							      PLANE_VM_PROT_WRITE),
				     false);
	failures += test_expect_u64("range prot allocated",
				    allocated_page_count(), 1);
	failures += test_expect_u64("range prot resident",
				    plane_vm_object_resident_page_count(
					    &test_object),
				    1);
	failures += test_expect_not_null("range prot prefix resident",
					 plane_vm_object_lookup_page(
						 &test_object, 0));
	failures += test_expect_null("range prot denied resident",
				     plane_vm_object_lookup_page(
					     &test_object, PAGE_SIZE));
	failures += test_expect_not_null("range prot prefix mapping",
					 find_mapping(first));
	failures += test_expect_null("range prot denied mapping",
				     find_mapping(second));
	return failures;
}

static int test_fault_pages_repairs_and_protects_resident_pages(void)
{
	struct plane_page *first_page;
	struct plane_page *second_page;
	struct test_mapping *first_mapping;
	struct test_mapping *second_mapping;
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("range repair init",
				     init_map_and_object(2 * PAGE_SIZE), true);
	failures += test_expect_bool("range repair enter",
				     enter_object(page_vaddr(1), 2, 0,
						  PLANE_VM_PROT_READ,
						  &vaddr),
				     true);
	failures += test_expect_bool("range repair first grab",
				     plane_vm_page_grab(0, &first_page), true);
	failures += test_expect_bool("range repair second grab",
				     plane_vm_page_grab(0, &second_page), true);
	failures += test_expect_bool("range repair first insert",
				     plane_vm_object_insert_page(
					     &test_object, 0, first_page),
				     true);
	failures += test_expect_bool("range repair second insert",
				     plane_vm_object_insert_page(
					     &test_object, PAGE_SIZE,
					     second_page),
				     true);
	failures += test_expect_bool("range repair second premap",
				     hal_mmu_map_kernel_page(
					     test_vaddr(vaddr + PAGE_SIZE),
					     plane_vm_page_phys(second_page),
					     HAL_MMU_MAP_WRITE),
				     true);
	failures += test_expect_bool("range repair pages",
				     test_fault_pages(&test_map, vaddr, 2,
						      PLANE_VM_PROT_READ),
				     true);
	first_mapping = find_mapping(vaddr);
	second_mapping = find_mapping(vaddr + PAGE_SIZE);
	failures += test_expect_not_null("range repair first mapping",
					 first_mapping);
	failures += test_expect_not_null("range repair second mapping",
					 second_mapping);
	failures += test_expect_u64("range repair map calls",
				    map_call_count, 2);
	failures += test_expect_u64("range repair protect calls",
				    protect_call_count, 1);
	if (first_mapping != NULL) {
		failures += test_expect_u32("range repair first flags",
					    first_mapping->flags, 0);
	}
	if (second_mapping != NULL) {
		failures += test_expect_u32("range repair second flags",
					    second_mapping->flags, 0);
	}
	return failures;
}

static int test_fault_pages_keeps_prefix_on_map_failure(void)
{
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("range map fail init",
				     init_map_and_object(2 * PAGE_SIZE), true);
	failures += test_expect_bool("range map fail enter",
				     enter_object(page_vaddr(1), 2, 0,
						  PLANE_VM_PROT_DEFAULT,
						  &vaddr),
				     true);
	failures += test_expect_bool("range map fail prefix",
				     test_fault_page(&test_map, vaddr,
						     PLANE_VM_PROT_READ),
				     true);
	map_force_fail = true;
	failures += test_expect_bool("range map fail pages",
				     test_fault_pages(&test_map, vaddr, 2,
						      PLANE_VM_PROT_READ),
				     false);
	failures += test_expect_u64("range map fail allocated",
				    allocated_page_count(), 1);
	failures += test_expect_u64("range map fail resident",
				    plane_vm_object_resident_page_count(
					    &test_object),
				    1);
	failures += test_expect_not_null("range map fail prefix resident",
					 plane_vm_object_lookup_page(
						 &test_object, 0));
	failures += test_expect_null("range map fail failed resident",
				     plane_vm_object_lookup_page(
					     &test_object, PAGE_SIZE));
	failures += test_expect_not_null("range map fail prefix mapping",
					 find_mapping(vaddr));
	failures += test_expect_null("range map fail failed mapping",
				     find_mapping(vaddr + PAGE_SIZE));
	return failures;
}

static int test_fault_wire_pages_faults_and_wires_lazy_range(void)
{
	struct plane_page *first_page;
	struct plane_page *second_page;
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("fault wire range init",
				     init_map_and_object(2 * PAGE_SIZE), true);
	failures += test_expect_bool("fault wire range enter",
				     enter_object(page_vaddr(1), 2, 0,
						  PLANE_VM_PROT_DEFAULT,
						  &vaddr),
				     true);
	failures += test_expect_bool("fault wire range",
				     test_fault_wire_pages(&test_map, vaddr, 2,
							   PLANE_VM_PROT_READ),
				     true);
	first_page = plane_vm_object_lookup_page(&test_object, 0);
	second_page = plane_vm_object_lookup_page(&test_object, PAGE_SIZE);
	failures += test_expect_not_null("fault wire range first resident",
					 first_page);
	failures += test_expect_not_null("fault wire range second resident",
					 second_page);
	failures += expect_page_wire_count("fault wire range first wire",
					   first_page, 1);
	failures += expect_page_wire_count("fault wire range second wire",
					   second_page, 1);
	failures += test_expect_u64("fault wire range map first",
				    lookup_page_wired_count(vaddr), 1);
	failures += test_expect_u64("fault wire range map second",
				    lookup_page_wired_count(vaddr + PAGE_SIZE),
				    1);
	failures += test_expect_u64("fault wire range allocated",
				    allocated_page_count(), 2);
	failures += test_expect_u64("fault wire range resident",
				    plane_vm_object_resident_page_count(
					    &test_object),
				    2);
	failures += test_expect_u64("fault wire range object wired",
				    plane_vm_object_wired_page_count(
					    &test_object),
				    2);
	failures += test_expect_u64("fault wire range mappings",
				    mapping_count(), 2);
	return failures;
}

static int test_fault_wire_pages_wires_resident_hits_and_repairs_pmap(void)
{
	struct plane_page *first_page;
	struct plane_page *second_page;
	struct test_mapping *first_mapping;
	struct test_mapping *second_mapping;
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("fault wire hit init",
				     init_map_and_object(2 * PAGE_SIZE), true);
	failures += test_expect_bool("fault wire hit enter",
				     enter_object(page_vaddr(1), 2, 0,
						  PLANE_VM_PROT_READ,
						  &vaddr),
				     true);
	failures += test_expect_bool("fault wire hit first grab",
				     plane_vm_page_grab(0, &first_page), true);
	failures += test_expect_bool("fault wire hit second grab",
				     plane_vm_page_grab(0, &second_page), true);
	failures += test_expect_bool("fault wire hit first insert",
				     plane_vm_object_insert_page(
					     &test_object, 0, first_page),
				     true);
	failures += test_expect_bool("fault wire hit second insert",
				     plane_vm_object_insert_page(
					     &test_object, PAGE_SIZE,
					     second_page),
				     true);
	failures += test_expect_bool("fault wire hit premap",
				     hal_mmu_map_kernel_page(
					     test_vaddr(vaddr + PAGE_SIZE),
					     plane_vm_page_phys(second_page),
					     HAL_MMU_MAP_WRITE),
				     true);
	failures += test_expect_bool("fault wire hit range",
				     test_fault_wire_pages(&test_map, vaddr, 2,
							   PLANE_VM_PROT_READ),
				     true);
	first_mapping = find_mapping(vaddr);
	second_mapping = find_mapping(vaddr + PAGE_SIZE);
	failures += test_expect_not_null("fault wire hit first mapping",
					 first_mapping);
	failures += test_expect_not_null("fault wire hit second mapping",
					 second_mapping);
	failures += expect_page_wire_count("fault wire hit first wire",
					   first_page, 1);
	failures += expect_page_wire_count("fault wire hit second wire",
					   second_page, 1);
	failures += test_expect_u64("fault wire hit protect calls",
				    protect_call_count, 1);
	if (first_mapping != NULL) {
		failures += test_expect_u32("fault wire hit first flags",
					    first_mapping->flags, 0);
	}
	if (second_mapping != NULL) {
		failures += test_expect_u32("fault wire hit second flags",
					    second_mapping->flags, 0);
	}
	return failures;
}

static int test_fault_wire_pages_rolls_back_wiring_on_failure(void)
{
	struct plane_page *first_page;
	struct plane_page *second_page;
	struct test_mapping *second_mapping;
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("fault wire fail init",
				     init_map_and_object(2 * PAGE_SIZE), true);
	failures += test_expect_bool("fault wire fail enter",
				     enter_object(page_vaddr(1), 2, 0,
						  PLANE_VM_PROT_READ,
						  &vaddr),
				     true);
	failures += test_expect_bool("fault wire fail first grab",
				     plane_vm_page_grab(0, &first_page), true);
	failures += test_expect_bool("fault wire fail second grab",
				     plane_vm_page_grab(0, &second_page), true);
	failures += test_expect_bool("fault wire fail first insert",
				     plane_vm_object_insert_page(
					     &test_object, 0, first_page),
				     true);
	failures += test_expect_bool("fault wire fail second insert",
				     plane_vm_object_insert_page(
					     &test_object, PAGE_SIZE,
					     second_page),
				     true);
	failures += test_expect_bool("fault wire fail premap",
				     hal_mmu_map_kernel_page(
					     test_vaddr(vaddr + PAGE_SIZE),
					     plane_vm_page_phys(second_page),
					     HAL_MMU_MAP_WRITE),
				     true);
	protect_force_fail = true;
	failures += test_expect_bool("fault wire fail range",
				     test_fault_wire_pages(&test_map, vaddr, 2,
							   PLANE_VM_PROT_READ),
				     false);
	second_mapping = find_mapping(vaddr + PAGE_SIZE);
	failures += expect_page_wire_count("fault wire fail first wire",
					   first_page, 0);
	failures += expect_page_wire_count("fault wire fail second wire",
					   second_page, 0);
	failures += test_expect_u64("fault wire fail map first",
				    lookup_page_wired_count(vaddr), 0);
	failures += test_expect_u64("fault wire fail map second",
				    lookup_page_wired_count(vaddr + PAGE_SIZE),
				    0);
	failures += test_expect_not_null("fault wire fail prefix mapping",
					 find_mapping(vaddr));
	failures += test_expect_not_null("fault wire fail second mapping",
					 second_mapping);
	if (second_mapping != NULL) {
		failures += test_expect_u32("fault wire fail second flags",
					    second_mapping->flags,
					    HAL_MMU_MAP_WRITE);
	}
	failures += test_expect_u64("fault wire fail resident kept",
				    plane_vm_object_resident_page_count(
					    &test_object),
				    2);
	failures += test_expect_u64("fault wire fail object wired",
				    plane_vm_object_wired_page_count(
					    &test_object),
				    0);
	return failures;
}

static int test_fault_wire_pages_rejects_hole_without_mutation(void)
{
	uint64_t first = 0;
	uint64_t second = 0;
	int failures = 0;

	failures += test_expect_bool("fault wire hole init",
				     init_map_and_object(4 * PAGE_SIZE), true);
	failures += test_expect_bool("fault wire hole first enter",
				     enter_object(page_vaddr(1), 1, 0,
						  PLANE_VM_PROT_DEFAULT,
						  &first),
				     true);
	failures += test_expect_bool("fault wire hole second enter",
				     enter_object(page_vaddr(3), 1,
						  2 * PAGE_SIZE,
						  PLANE_VM_PROT_DEFAULT,
						  &second),
				     true);
	failures += test_expect_bool("fault wire hole range",
				     test_fault_wire_pages(&test_map, first, 3,
							   PLANE_VM_PROT_READ),
				     false);
	failures += test_expect_u64("fault wire hole first map",
				    lookup_page_wired_count(first), 0);
	failures += test_expect_u64("fault wire hole second map",
				    lookup_page_wired_count(second), 0);
	failures += test_expect_u64("fault wire hole allocated",
				    allocated_page_count(), 0);
	failures += test_expect_u64("fault wire hole mappings",
				    mapping_count(), 0);
	return failures;
}

static int test_fault_wire_unwire_rejects_invalid_inputs(void)
{
	uint64_t overflow_base = UINT64_MAX & ~(PAGE_SIZE - 1);
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("fault wire invalid init",
				     init_map_and_object(PAGE_SIZE), true);
	failures += test_expect_bool("fault wire invalid enter",
				     enter_object(page_vaddr(1), 1, 0,
						  PLANE_VM_PROT_DEFAULT,
						  &vaddr),
				     true);
	failures += test_expect_bool("fault wire null map",
				     test_fault_wire_pages(NULL, vaddr, 1,
							   PLANE_VM_PROT_READ),
				     false);
	failures += test_expect_bool("fault wire null addr",
				     test_fault_wire_pages(&test_map, 0, 1,
							   PLANE_VM_PROT_READ),
				     false);
	failures += test_expect_bool("fault wire unaligned",
				     test_fault_wire_pages(&test_map,
							   vaddr + 1, 1,
							   PLANE_VM_PROT_READ),
				     false);
	failures += test_expect_bool("fault wire zero pages",
				     test_fault_wire_pages(&test_map, vaddr, 0,
							   PLANE_VM_PROT_READ),
				     false);
	failures += test_expect_bool("fault wire none",
				     test_fault_wire_pages(&test_map, vaddr, 1,
							   PLANE_VM_PROT_NONE),
				     false);
	failures += test_expect_bool("fault wire unknown",
				     test_fault_wire_pages(&test_map, vaddr, 1,
							   BIT(7)),
				     false);
	failures += test_expect_bool("fault wire overflow",
				     test_fault_wire_pages(&test_map,
							   overflow_base, 2,
							   PLANE_VM_PROT_READ),
				     false);
	failures += test_expect_bool("fault unwire null map",
				     test_fault_unwire_pages(NULL, vaddr, 1),
				     false);
	failures += test_expect_bool("fault unwire null addr",
				     test_fault_unwire_pages(&test_map, 0, 1),
				     false);
	failures += test_expect_bool("fault unwire unaligned",
				     test_fault_unwire_pages(&test_map,
							     vaddr + 1, 1),
				     false);
	failures += test_expect_bool("fault unwire zero pages",
				     test_fault_unwire_pages(&test_map,
							     vaddr, 0),
				     false);
	failures += test_expect_bool("fault unwire overflow",
				     test_fault_unwire_pages(&test_map,
							     overflow_base, 2),
				     false);
	failures += test_expect_u64("fault wire invalid map count",
				    lookup_page_wired_count(vaddr), 0);
	failures += test_expect_u64("fault wire invalid allocated",
				    allocated_page_count(), 0);
	failures += test_expect_u64("fault wire invalid mappings",
				    mapping_count(), 0);
	return failures;
}

static int test_fault_unwire_pages_unwires_resident_without_freeing(void)
{
	struct plane_page *first_page;
	struct plane_page *second_page;
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("fault unwire init",
				     init_map_and_object(2 * PAGE_SIZE), true);
	failures += test_expect_bool("fault unwire enter",
				     enter_object(page_vaddr(1), 2, 0,
						  PLANE_VM_PROT_DEFAULT,
						  &vaddr),
				     true);
	failures += test_expect_bool("fault unwire wire",
				     test_fault_wire_pages(&test_map, vaddr, 2,
							   PLANE_VM_PROT_READ),
				     true);
	first_page = plane_vm_object_lookup_page(&test_object, 0);
	second_page = plane_vm_object_lookup_page(&test_object, PAGE_SIZE);
	failures += test_expect_bool("fault unwire pages",
				     test_fault_unwire_pages(&test_map,
							     vaddr, 2),
				     true);
	failures += expect_page_wire_count("fault unwire first wire",
					   first_page, 0);
	failures += expect_page_wire_count("fault unwire second wire",
					   second_page, 0);
	failures += test_expect_u64("fault unwire map first",
				    lookup_page_wired_count(vaddr), 0);
	failures += test_expect_u64("fault unwire map second",
				    lookup_page_wired_count(vaddr + PAGE_SIZE),
				    0);
	failures += test_expect_u64("fault unwire allocated kept",
				    allocated_page_count(), 2);
	failures += test_expect_u64("fault unwire resident kept",
				    plane_vm_object_resident_page_count(
					    &test_object),
				    2);
	failures += test_expect_u64("fault unwire mappings kept",
				    mapping_count(), 2);
	failures += test_expect_u64("fault unwire object wired",
				    plane_vm_object_wired_page_count(
					    &test_object),
				    0);
	return failures;
}

static int test_fault_unwire_pages_allows_absent_lazy_pages(void)
{
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("fault unwire absent init",
				     init_map_and_object(2 * PAGE_SIZE), true);
	failures += test_expect_bool("fault unwire absent enter",
				     enter_object(page_vaddr(1), 2, 0,
						  PLANE_VM_PROT_DEFAULT,
						  &vaddr),
				     true);
	failures += test_expect_bool("fault unwire absent metadata",
				     test_map_wire_pages(&test_map, vaddr, 2),
				     true);
	failures += test_expect_bool("fault unwire absent pages",
				     test_fault_unwire_pages(&test_map,
							     vaddr, 2),
				     true);
	failures += test_expect_u64("fault unwire absent map first",
				    lookup_page_wired_count(vaddr), 0);
	failures += test_expect_u64("fault unwire absent map second",
				    lookup_page_wired_count(vaddr + PAGE_SIZE),
				    0);
	failures += test_expect_u64("fault unwire absent allocated",
				    allocated_page_count(), 0);
	failures += test_expect_u64("fault unwire absent mappings",
				    mapping_count(), 0);
	return failures;
}

static int test_fault_unwire_pages_rejects_unwired_resident_page(void)
{
	struct plane_page *page;
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("fault unwire bad init",
				     init_map_and_object(PAGE_SIZE), true);
	failures += test_expect_bool("fault unwire bad enter",
				     enter_object(page_vaddr(1), 1, 0,
						  PLANE_VM_PROT_DEFAULT,
						  &vaddr),
				     true);
	failures += test_expect_bool("fault unwire bad metadata",
				     test_map_wire_pages(&test_map, vaddr, 1),
				     true);
	failures += test_expect_bool("fault unwire bad grab",
				     plane_vm_page_grab(0, &page), true);
	failures += test_expect_bool("fault unwire bad insert",
				     plane_vm_object_insert_page(
					     &test_object, 0, page),
				     true);
	failures += test_expect_bool("fault unwire bad pages",
				     test_fault_unwire_pages(&test_map,
							     vaddr, 1),
				     false);
	failures += test_expect_u64("fault unwire bad map kept",
				    lookup_page_wired_count(vaddr), 1);
	failures += test_expect_u64("fault unwire bad object wired",
				    plane_vm_object_wired_page_count(
					    &test_object),
				    0);
	return failures;
}

int main(void)
{
	static const struct test_case cases[] = {
		TEST_CASE(test_fault_rejects_invalid_or_unmapped_access),
		TEST_CASE(test_fault_miss_allocates_zero_page_and_maps),
		TEST_CASE(test_fault_unaligned_address_uses_page_object_offset),
		TEST_CASE(test_fault_resident_hit_repairs_absent_pmap),
		TEST_CASE(test_fault_resident_hit_protects_existing_pmap),
		TEST_CASE(test_fault_resident_hit_rolls_back_protect_failure),
		TEST_CASE(test_fault_resident_hit_rejects_wrong_pmap_phys),
		TEST_CASE(test_fault_resident_hit_rejects_invalid_phys),
		TEST_CASE(test_fault_miss_rejects_stale_pmap_mapping),
		TEST_CASE(test_fault_wired_entry_wires_new_page),
		TEST_CASE(test_fault_multi_wired_entry_wires_new_page),
		TEST_CASE(test_fault_multi_wire_failure_rolls_back_all_wires),
		TEST_CASE(test_fault_rolls_back_allocation_failures),
		TEST_CASE(test_fault_pages_faults_contiguous_range),
		TEST_CASE(test_fault_pages_rejects_invalid_inputs),
		TEST_CASE(test_fault_pages_keeps_prefix_before_hole),
		TEST_CASE(test_fault_pages_keeps_prefix_before_protection_failure),
		TEST_CASE(test_fault_pages_repairs_and_protects_resident_pages),
		TEST_CASE(test_fault_pages_keeps_prefix_on_map_failure),
		TEST_CASE(test_fault_wire_pages_faults_and_wires_lazy_range),
		TEST_CASE(test_fault_wire_pages_wires_resident_hits_and_repairs_pmap),
		TEST_CASE(test_fault_wire_pages_rolls_back_wiring_on_failure),
		TEST_CASE(test_fault_wire_pages_rejects_hole_without_mutation),
		TEST_CASE(test_fault_wire_unwire_rejects_invalid_inputs),
		TEST_CASE(test_fault_unwire_pages_unwires_resident_without_freeing),
		TEST_CASE(test_fault_unwire_pages_allows_absent_lazy_pages),
		TEST_CASE(test_fault_unwire_pages_rejects_unwired_resident_page),
	};

	return test_run_cases_with_fixture("vm_fault_test", cases,
					   TEST_ARRAY_SIZE(cases),
					   reset_vm_fault_test,
					   NULL);
}
