#include <stddef.h>
#include <stdint.h>

#include <plane/mm.h>
#include <plane/vm_page.h>
#include <plane/vm_object.h>

#include "support/test.h"
#include "../kernel/mm/vm_page_internal.h"

#define TEST_OBJECT_PAGES 16
#define TEST_OBJECT_SIZE (TEST_OBJECT_PAGES * PAGE_SIZE)
#define TEST_HASH_PAGE_COUNT 12

struct plane_page {
	struct plane_vm_object *object;
	uint64_t object_offset;
	uint64_t wire_count;
	struct plane_page *object_prev;
	struct plane_page *object_next;
	struct plane_page *object_hash_next;
	bool object_tabled;
	bool object_hashed;
	enum plane_vm_page_state state;
};

static struct plane_vm_object test_object;
static struct plane_vm_object second_object;
static struct plane_page allocated_page;
static struct plane_page second_allocated_page;
static struct plane_page third_allocated_page;
static struct plane_page hash_pages[TEST_HASH_PAGE_COUNT];
static struct plane_page free_page;

enum plane_vm_page_state plane_vm_page_state(const struct plane_page *page)
{
	if (page == NULL) {
		return PLANE_VM_PAGE_INVALID;
	}

	return page->state;
}

struct plane_vm_object *plane_vm_page_object(const struct plane_page *page)
{
	if (page == NULL) {
		return NULL;
	}

	return page->object;
}

bool plane_vm_page_object_offset(const struct plane_page *page, uint64_t *offset)
{
	if (page == NULL ||
	    page->object == NULL ||
	    offset == NULL) {
		return false;
	}

	*offset = page->object_offset;
	return true;
}

bool plane_vm_page_wire_count(const struct plane_page *page, uint64_t *wire_count)
{
	if (page == NULL || wire_count == NULL) {
		return false;
	}

	*wire_count = page->wire_count;
	return true;
}

bool plane_vm_page_attach_object(struct plane_page *page,
				 struct plane_vm_object *object,
				 uint64_t offset)
{
	if (page == NULL ||
	    object == NULL ||
	    page->object != NULL ||
	    page->state != PLANE_VM_PAGE_ALLOCATED) {
		return false;
	}

	page->object = object;
	page->object_offset = offset;
	return true;
}

bool plane_vm_page_detach_object(struct plane_page *page,
				 struct plane_vm_object *object,
				 uint64_t offset)
{
	if (page == NULL ||
	    object == NULL ||
	    page->object != object ||
	    page->object_offset != offset ||
	    page->state != PLANE_VM_PAGE_ALLOCATED) {
		return false;
	}

	page->object = NULL;
	page->object_offset = 0;
	return true;
}

struct plane_page *plane_vm_page_object_prev(const struct plane_page *page)
{
	if (page == NULL) {
		return NULL;
	}

	return page->object_prev;
}

struct plane_page *plane_vm_page_object_next(const struct plane_page *page)
{
	if (page == NULL) {
		return NULL;
	}

	return page->object_next;
}

struct plane_page *plane_vm_page_object_hash_next(const struct plane_page *page)
{
	if (page == NULL) {
		return NULL;
	}

	return page->object_hash_next;
}

bool plane_vm_page_object_tabled(const struct plane_page *page)
{
	if (page == NULL) {
		return false;
	}

	return page->object_tabled;
}

bool plane_vm_page_object_hashed(const struct plane_page *page)
{
	if (page == NULL) {
		return false;
	}

	return page->object_hashed;
}

bool plane_vm_page_set_object_prev(struct plane_page *page,
				   struct plane_page *prev)
{
	if (page == NULL) {
		return false;
	}

	page->object_prev = prev;
	return true;
}

bool plane_vm_page_set_object_next(struct plane_page *page,
				   struct plane_page *next)
{
	if (page == NULL) {
		return false;
	}

	page->object_next = next;
	return true;
}

bool plane_vm_page_set_object_hash_next(struct plane_page *page,
					struct plane_page *next)
{
	if (page == NULL) {
		return false;
	}

	page->object_hash_next = next;
	return true;
}

bool plane_vm_page_set_object_tabled(struct plane_page *page, bool tabled)
{
	if (page == NULL) {
		return false;
	}

	page->object_tabled = tabled;
	return true;
}

bool plane_vm_page_set_object_hashed(struct plane_page *page, bool hashed)
{
	if (page == NULL) {
		return false;
	}

	page->object_hashed = hashed;
	return true;
}

static void cleanup_vm_object(struct plane_vm_object *object)
{
	while (object->initialized && object->resident_head != NULL) {
		struct plane_page *page = object->resident_head;
		struct plane_page *removed;

		removed = plane_vm_object_remove_page(object,
						      page->object_offset);
		if (removed == NULL) {
			break;
		}
	}
}

static void reset_vm_object_test(void)
{
	cleanup_vm_object(&test_object);
	cleanup_vm_object(&second_object);
	test_object = (struct plane_vm_object){0};
	second_object = (struct plane_vm_object){0};
	allocated_page = (struct plane_page){0};
	second_allocated_page = (struct plane_page){0};
	third_allocated_page = (struct plane_page){0};
	for (size_t i = 0; i < TEST_HASH_PAGE_COUNT; i++) {
		hash_pages[i] = (struct plane_page){0};
		hash_pages[i].state = PLANE_VM_PAGE_ALLOCATED;
	}
	free_page = (struct plane_page){0};
	allocated_page.state = PLANE_VM_PAGE_ALLOCATED;
	second_allocated_page.state = PLANE_VM_PAGE_ALLOCATED;
	third_allocated_page.state = PLANE_VM_PAGE_ALLOCATED;
	free_page.state = PLANE_VM_PAGE_FREE;
}

static int test_init_rejects_invalid_inputs(void)
{
	int failures = 0;

	failures += test_expect_bool("object init null object",
				     plane_vm_object_init(NULL,
							  TEST_OBJECT_SIZE),
				     false);
	failures += test_expect_bool("object init zero size",
				     plane_vm_object_init(&test_object, 0),
				     false);
	failures += test_expect_bool("object init unaligned size",
				     plane_vm_object_init(&test_object,
							  TEST_OBJECT_SIZE - 1),
				     false);
	failures += test_expect_u64("invalid resident count",
				    plane_vm_object_resident_page_count(NULL),
				    0);
	failures += test_expect_u64("invalid wired count",
				    plane_vm_object_wired_page_count(NULL), 0);
	return failures;
}

static int test_init_is_one_shot(void)
{
	int failures = 0;

	failures += test_expect_bool("object init",
				     plane_vm_object_init(&test_object,
							  TEST_OBJECT_SIZE),
				     true);
	failures += test_expect_bool("object repeat init",
				     plane_vm_object_init(&test_object,
							  TEST_OBJECT_SIZE),
				     false);
	return failures;
}

static int test_lookup_empty_object_returns_null(void)
{
	int failures = 0;

	failures += test_expect_bool("object init",
				     plane_vm_object_init(&test_object,
							  TEST_OBJECT_SIZE),
				     true);
	failures += test_expect_null("empty lookup",
				     plane_vm_object_lookup_page(&test_object,
								 0));
	return failures;
}

static int test_insert_lookup_and_remove_page(void)
{
	struct plane_page *page;
	uint64_t offset = 0;
	int failures = 0;

	failures += test_expect_bool("object init",
				     plane_vm_object_init(&test_object,
							  TEST_OBJECT_SIZE),
				     true);
	failures += test_expect_u64("object initial resident count",
				    plane_vm_object_resident_page_count(
					    &test_object),
				    0);
	failures += test_expect_u64("object initial wired count",
				    plane_vm_object_wired_page_count(
					    &test_object),
				    0);
	failures += test_expect_bool("object insert",
				     plane_vm_object_insert_page(
					     &test_object, PAGE_SIZE,
					     &allocated_page),
				     true);
	failures += test_expect_bool("page tabled after insert",
				     allocated_page.object_tabled, true);
	failures += test_expect_bool("page hashed after insert",
				     allocated_page.object_hashed, true);
	failures += test_expect_u64("object resident count",
				    plane_vm_object_resident_page_count(
					    &test_object),
				    1);
	failures += test_expect_u64("object wired count",
				    plane_vm_object_wired_page_count(
					    &test_object),
				    0);
	failures += test_expect_ptr("page object",
				    plane_vm_page_object(&allocated_page),
				    &test_object);
	failures += test_expect_bool("page object offset query",
				     plane_vm_page_object_offset(&allocated_page,
								 &offset),
				     true);
	failures += test_expect_u64("page object offset", offset, PAGE_SIZE);
	page = plane_vm_object_lookup_page(&test_object, PAGE_SIZE);
	failures += test_expect_ptr("object lookup", page, &allocated_page);
	page = plane_vm_object_remove_page(&test_object, PAGE_SIZE);
	failures += test_expect_ptr("object remove", page, &allocated_page);
	failures += test_expect_bool("page untabled after remove",
				     allocated_page.object_tabled, false);
	failures += test_expect_bool("page unhashed after remove",
				     allocated_page.object_hashed, false);
	failures += test_expect_u64("object resident count removed",
				    plane_vm_object_resident_page_count(
					    &test_object),
				    0);
	failures += test_expect_u64("object wired count removed",
				    plane_vm_object_wired_page_count(
					    &test_object),
				    0);
	failures += test_expect_null("page object cleared",
				     plane_vm_page_object(&allocated_page));
	failures += test_expect_bool("page object offset cleared",
				     plane_vm_page_object_offset(&allocated_page,
								 &offset),
				     false);
	failures += test_expect_null("object lookup removed",
				     plane_vm_object_lookup_page(&test_object,
								 PAGE_SIZE));
	return failures;
}

static int test_lookup_small_object_scans_resident_list(void)
{
	int failures = 0;

	failures += test_expect_bool("object init",
				     plane_vm_object_init(&test_object,
							  TEST_OBJECT_SIZE),
				     true);
	failures += test_expect_bool("object insert first",
				     plane_vm_object_insert_page(
					     &test_object, 0,
					     &allocated_page),
				     true);
	failures += test_expect_bool("object insert second",
				     plane_vm_object_insert_page(
					     &test_object, PAGE_SIZE,
					     &second_allocated_page),
				     true);
	failures += test_expect_bool("object insert third",
				     plane_vm_object_insert_page(
					     &test_object, 2 * PAGE_SIZE,
					     &third_allocated_page),
				     true);
	test_object.resident_hint = NULL;
	failures += test_expect_ptr("small object list lookup",
				    plane_vm_object_lookup_page(&test_object,
								2 * PAGE_SIZE),
				    &third_allocated_page);
	failures += test_expect_ptr("small object updates hint",
				    test_object.resident_hint,
				    &third_allocated_page);
	return failures;
}

static int test_lookup_large_object_uses_hash(void)
{
	int failures = 0;

	failures += test_expect_bool("object init",
				     plane_vm_object_init(&test_object,
							  TEST_OBJECT_SIZE),
				     true);
	for (size_t i = 0; i < TEST_HASH_PAGE_COUNT; i++) {
		failures += test_expect_bool("object insert hash page",
					     plane_vm_object_insert_page(
						     &test_object,
						     i * PAGE_SIZE,
						     &hash_pages[i]),
					     true);
	}
	test_object.resident_hint = NULL;
	failures += test_expect_ptr("large object hash lookup",
				    plane_vm_object_lookup_page(
					    &test_object,
					    (TEST_HASH_PAGE_COUNT - 1) *
					    PAGE_SIZE),
				    &hash_pages[TEST_HASH_PAGE_COUNT - 1]);
	failures += test_expect_ptr("large object updates hint",
				    test_object.resident_hint,
				    &hash_pages[TEST_HASH_PAGE_COUNT - 1]);
	return failures;
}

static int test_insert_tracks_wired_page_count(void)
{
	int failures = 0;

	failures += test_expect_bool("object init",
				     plane_vm_object_init(&test_object,
							  TEST_OBJECT_SIZE),
				     true);
	allocated_page.wire_count = 2;
	failures += test_expect_bool("object insert wired",
				     plane_vm_object_insert_page(
					     &test_object, 0,
					     &allocated_page),
				     true);
	failures += test_expect_u64("object resident wired page",
				    plane_vm_object_resident_page_count(
					    &test_object),
				    1);
	failures += test_expect_u64("object wired page count",
				    plane_vm_object_wired_page_count(
					    &test_object),
				    1);
	failures += test_expect_ptr("object remove wired",
				    plane_vm_object_remove_page(&test_object, 0),
				    &allocated_page);
	failures += test_expect_u64("object resident wired removed",
				    plane_vm_object_resident_page_count(
					    &test_object),
				    0);
	failures += test_expect_u64("object wired removed",
				    plane_vm_object_wired_page_count(
					    &test_object),
				    0);
	return failures;
}

static int test_insert_rejects_invalid_page_or_offset(void)
{
	int failures = 0;

	failures += test_expect_bool("object init",
				     plane_vm_object_init(&test_object,
							  TEST_OBJECT_SIZE),
				     true);
	failures += test_expect_bool("object reject unaligned offset",
				     plane_vm_object_insert_page(
					     &test_object, 1, &allocated_page),
				     false);
	failures += test_expect_bool("object reject out of range",
				     plane_vm_object_insert_page(
					     &test_object, TEST_OBJECT_SIZE,
					     &allocated_page),
				     false);
	failures += test_expect_bool("object reject null page",
				     plane_vm_object_insert_page(
					     &test_object, 0, NULL),
				     false);
	failures += test_expect_bool("object reject free page",
				     plane_vm_object_insert_page(
					     &test_object, 0, &free_page),
				     false);
	return failures;
}

static int test_rejects_duplicate_and_missing_remove(void)
{
	int failures = 0;

	failures += test_expect_bool("object init",
				     plane_vm_object_init(&test_object,
							  TEST_OBJECT_SIZE),
				     true);
	failures += test_expect_bool("object insert",
				     plane_vm_object_insert_page(
					     &test_object, 0, &allocated_page),
				     true);
	failures += test_expect_u64("object duplicate resident stable",
				    plane_vm_object_resident_page_count(
					    &test_object),
				    1);
	failures += test_expect_bool("object reject duplicate",
				     plane_vm_object_insert_page(
					     &test_object, 0, &allocated_page),
				     false);
	failures += test_expect_bool("object reject duplicate page",
				     plane_vm_object_insert_page(
					     &test_object, PAGE_SIZE,
					     &allocated_page),
				     false);
	failures += test_expect_bool("second object init",
				     plane_vm_object_init(&second_object,
							  TEST_OBJECT_SIZE),
				     true);
	failures += test_expect_bool("object reject page in other object",
				     plane_vm_object_insert_page(
					     &second_object, PAGE_SIZE,
					     &allocated_page),
				     false);
	failures += test_expect_null("object reject missing remove",
				     plane_vm_object_remove_page(&test_object,
								 PAGE_SIZE));
	failures += test_expect_null("object reject unaligned remove",
				     plane_vm_object_remove_page(&test_object,
								 1));
	failures += test_expect_u64("object duplicate resident unchanged",
				    plane_vm_object_resident_page_count(
					    &test_object),
				    1);
	failures += test_expect_u64("object duplicate wired unchanged",
				    plane_vm_object_wired_page_count(
					    &test_object),
				    0);
	return failures;
}

static int test_multiple_pages_update_counts(void)
{
	int failures = 0;

	failures += test_expect_bool("object init",
				     plane_vm_object_init(&test_object,
							  TEST_OBJECT_SIZE),
				     true);
	second_allocated_page.wire_count = 1;
	failures += test_expect_bool("object insert first",
				     plane_vm_object_insert_page(
					     &test_object, 0,
					     &allocated_page),
				     true);
	failures += test_expect_bool("object insert second",
				     plane_vm_object_insert_page(
					     &test_object, PAGE_SIZE,
					     &second_allocated_page),
				     true);
	failures += test_expect_bool("object insert third",
				     plane_vm_object_insert_page(
					     &test_object, 2 * PAGE_SIZE,
					     &third_allocated_page),
				     true);
	failures += test_expect_u64("object multi resident count",
				    plane_vm_object_resident_page_count(
					    &test_object),
				    3);
	failures += test_expect_u64("object multi wired count",
				    plane_vm_object_wired_page_count(
					    &test_object),
				    1);
	failures += test_expect_ptr("object remove middle",
				    plane_vm_object_remove_page(&test_object,
								PAGE_SIZE),
				    &second_allocated_page);
	failures += test_expect_u64("object multi resident after middle",
				    plane_vm_object_resident_page_count(
					    &test_object),
				    2);
	failures += test_expect_u64("object multi wired after middle",
				    plane_vm_object_wired_page_count(
					    &test_object),
				    0);
	failures += test_expect_ptr("object lookup head",
				    plane_vm_object_lookup_page(&test_object, 0),
				    &allocated_page);
	failures += test_expect_null("object lookup removed middle",
				     plane_vm_object_lookup_page(&test_object,
								 PAGE_SIZE));
	failures += test_expect_ptr("object lookup tail",
				    plane_vm_object_lookup_page(&test_object,
								2 * PAGE_SIZE),
				    &third_allocated_page);
	failures += test_expect_ptr("object remove head",
				    plane_vm_object_remove_page(&test_object, 0),
				    &allocated_page);
	failures += test_expect_ptr("object remove tail",
				    plane_vm_object_remove_page(&test_object,
								2 * PAGE_SIZE),
				    &third_allocated_page);
	failures += test_expect_u64("object multi resident empty",
				    plane_vm_object_resident_page_count(
					    &test_object),
				    0);
	failures += test_expect_u64("object multi wired empty",
				    plane_vm_object_wired_page_count(
					    &test_object),
				    0);
	return failures;
}

static int test_reinsert_page_uses_new_offset(void)
{
	int failures = 0;

	failures += test_expect_bool("object init",
				     plane_vm_object_init(&test_object,
							  TEST_OBJECT_SIZE),
				     true);
	failures += test_expect_bool("object insert first offset",
				     plane_vm_object_insert_page(
					     &test_object, 0,
					     &allocated_page),
				     true);
	failures += test_expect_ptr("object remove first offset",
				    plane_vm_object_remove_page(&test_object, 0),
				    &allocated_page);
	failures += test_expect_bool("object untabled before reinsert",
				     allocated_page.object_tabled, false);
	failures += test_expect_bool("object unhashed before reinsert",
				     allocated_page.object_hashed, false);
	failures += test_expect_bool("object reinsert second offset",
				     plane_vm_object_insert_page(
					     &test_object, 3 * PAGE_SIZE,
					     &allocated_page),
				     true);
	failures += test_expect_bool("object tabled after reinsert",
				     allocated_page.object_tabled, true);
	failures += test_expect_bool("object hashed after reinsert",
				     allocated_page.object_hashed, true);
	failures += test_expect_null("object old offset gone",
				     plane_vm_object_lookup_page(&test_object, 0));
	failures += test_expect_ptr("object new offset lookup",
				    plane_vm_object_lookup_page(&test_object,
								3 * PAGE_SIZE),
				    &allocated_page);
	failures += test_expect_u64("object reinsert resident count",
				    plane_vm_object_resident_page_count(
					    &test_object),
				    1);
	return failures;
}

int main(void)
{
	const struct test_case cases[] = {
		TEST_CASE(test_init_rejects_invalid_inputs),
		TEST_CASE(test_init_is_one_shot),
		TEST_CASE(test_lookup_empty_object_returns_null),
		TEST_CASE(test_insert_lookup_and_remove_page),
		TEST_CASE(test_lookup_small_object_scans_resident_list),
		TEST_CASE(test_lookup_large_object_uses_hash),
		TEST_CASE(test_insert_tracks_wired_page_count),
		TEST_CASE(test_insert_rejects_invalid_page_or_offset),
		TEST_CASE(test_rejects_duplicate_and_missing_remove),
		TEST_CASE(test_multiple_pages_update_counts),
		TEST_CASE(test_reinsert_page_uses_new_offset),
	};

	return test_run_cases_with_fixture("vm_object_test", cases,
					   TEST_ARRAY_SIZE(cases),
					   reset_vm_object_test, NULL);
}
