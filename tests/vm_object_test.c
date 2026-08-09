#include <stddef.h>
#include <stdint.h>

#include <plane/mm.h>
#include <plane/vm_page.h>
#include <plane/vm_object.h>

#include "support/test.h"
#include "../kernel/mm/vm_object_internal.h"
#include "../kernel/mm/vm_page_internal.h"
#include "../kernel/mm/vm_zone_internal.h"

#define TEST_OBJECT_PAGES 16
#define TEST_OBJECT_SIZE (TEST_OBJECT_PAGES * PAGE_SIZE)
#define TEST_HASH_PAGE_COUNT 12
#define TEST_BOOTSTRAP_OBJECT_POOL_SIZE 256
#define TEST_OBJECT_EXTRA_POOL_SIZE 4
#define TEST_REHASH_BUCKETS 512


static struct plane_vm_object test_object;
static struct plane_vm_object second_object;
static struct plane_page allocated_page;
static struct plane_page second_allocated_page;
static struct plane_page third_allocated_page;
static struct plane_page hash_pages[TEST_HASH_PAGE_COUNT];
static struct plane_page guard_page;
static struct plane_page free_page;
static struct plane_vm_object extra_object_pool[TEST_OBJECT_EXTRA_POOL_SIZE];
static struct plane_vm_zone_segment extra_object_segment;
static struct plane_page *rehash_buckets[TEST_REHASH_BUCKETS + 1];

enum plane_vm_page_state plane_vm_page_state(const struct plane_page *page)
{
	if (page == NULL) {
		return PLANE_VM_PAGE_INVALID;
	}

	return page->state;
}

bool plane_vm_page_is_guard(const struct plane_page *page)
{
	return plane_vm_page_state(page) == PLANE_VM_PAGE_GUARD;
}

struct plane_vm_object *plane_vm_page_object(const struct plane_page *page)
{
	if (page == NULL) {
		return NULL;
	}

	return page->vm_object;
}

bool plane_vm_page_object_offset(const struct plane_page *page, uint64_t *offset)
{
	if (page == NULL ||
	    page->vm_object == NULL ||
	    offset == NULL) {
		return false;
	}

	*offset = page->vm_object_offset;
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
	    page->vm_object != NULL ||
	    (page->state != PLANE_VM_PAGE_ALLOCATED &&
	     page->state != PLANE_VM_PAGE_GUARD)) {
		return false;
	}

	page->vm_object = object;
	page->vm_object_offset = offset;
	return true;
}

bool plane_vm_page_detach_object(struct plane_page *page,
				 struct plane_vm_object *object,
				 uint64_t offset)
{
	if (page == NULL ||
	    object == NULL ||
	    page->vm_object != object ||
	    page->vm_object_offset != offset ||
	    (page->state != PLANE_VM_PAGE_ALLOCATED &&
	     page->state != PLANE_VM_PAGE_GUARD)) {
		return false;
	}

	page->vm_object = NULL;
	page->vm_object_offset = 0;
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

bool plane_vm_page_object_is_tabled(const struct plane_page *page)
{
	if (page == NULL) {
		return false;
	}

	return page->object_tabled;
}

bool plane_vm_page_object_is_hashed(const struct plane_page *page)
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

static struct plane_page *resident_head(const struct plane_vm_object *object)
{
	return object->resident_head;
}

static void cleanup_vm_object(struct plane_vm_object *object)
{
	while (object->initialized && resident_head(object) != NULL) {
		struct plane_page *page = resident_head(object);
		struct plane_page *removed;

		removed = plane_vm_object_remove_page(object,
						      page->vm_object_offset);
		if (removed == NULL) {
			break;
		}
	}
}

static void clear_resident_hint(struct plane_vm_object *object)
{
	object->resident_hint = NULL;
}

static struct plane_page *resident_hint(const struct plane_vm_object *object)
{
	return object->resident_hint;
}

static void reset_vm_object_test(void)
{
	cleanup_vm_object(&test_object);
	cleanup_vm_object(&second_object);
	plane_vm_object_reset_bootstrap_for_tests();
	test_object = (struct plane_vm_object){0};
	second_object = (struct plane_vm_object){0};
	allocated_page = (struct plane_page){0};
	second_allocated_page = (struct plane_page){0};
	third_allocated_page = (struct plane_page){0};
	guard_page = (struct plane_page){0};
	for (size_t i = 0; i < TEST_HASH_PAGE_COUNT; i++) {
		hash_pages[i] = (struct plane_page){0};
		hash_pages[i].state = PLANE_VM_PAGE_ALLOCATED;
	}
	for (uint64_t i = 0; i < TEST_OBJECT_EXTRA_POOL_SIZE; i++) {
		extra_object_pool[i] = (struct plane_vm_object){0};
	}
	extra_object_segment = (struct plane_vm_zone_segment){0};
	for (uint64_t i = 0; i < TEST_ARRAY_SIZE(rehash_buckets); i++) {
		rehash_buckets[i] = NULL;
	}
	free_page = (struct plane_page){0};
	allocated_page.state = PLANE_VM_PAGE_ALLOCATED;
	second_allocated_page.state = PLANE_VM_PAGE_ALLOCATED;
	third_allocated_page.state = PLANE_VM_PAGE_ALLOCATED;
	guard_page.state = PLANE_VM_PAGE_GUARD;
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
	failures += test_expect_u64("invalid ref count",
				    plane_vm_object_ref_count(NULL), 0);
	failures += test_expect_bool("invalid alive",
				     plane_vm_object_is_alive(NULL), false);
	failures += test_expect_u64("invalid offset limit",
				    plane_vm_object_offset_limit(NULL), 0);
	return failures;
}

static int test_init_is_one_shot(void)
{
	int failures = 0;

	failures += test_expect_bool("object init",
				     plane_vm_object_init(&test_object,
							  TEST_OBJECT_SIZE),
				     true);
	failures += test_expect_u64("object init ref count",
				    plane_vm_object_ref_count(&test_object), 1);
	failures += test_expect_bool("object init alive",
				     plane_vm_object_is_alive(&test_object),
				     true);
	failures += test_expect_bool("object init internal",
				     test_object.internal, true);
	failures += test_expect_bool("object init caller owned",
				     test_object.allocated, false);
	failures += test_expect_u64("object init offset limit",
				    plane_vm_object_offset_limit(&test_object),
				    TEST_OBJECT_SIZE);
	failures += test_expect_bool("object repeat init",
				     plane_vm_object_init(&test_object,
							  TEST_OBJECT_SIZE),
				     false);
	return failures;
}

static int test_allocate_rejects_invalid_inputs(void)
{
	struct plane_vm_object *object = &test_object;
	int failures = 0;

	failures += test_expect_bool("object allocate null out",
				     plane_vm_object_allocate(TEST_OBJECT_SIZE,
							      NULL),
				     false);
	failures += test_expect_bool("object allocate zero size",
				     plane_vm_object_allocate(0, &object),
				     false);
	failures += test_expect_bool("object allocate unaligned size",
				     plane_vm_object_allocate(TEST_OBJECT_SIZE - 1,
							      &object),
				     false);
	failures += test_expect_ptr("object allocate invalid out unchanged",
				    object, &test_object);
	return failures;
}

static int test_allocate_initializes_internal_object(void)
{
	struct plane_vm_object *object = NULL;
	int failures = 0;

	failures += test_expect_bool("object allocate",
				     plane_vm_object_allocate(TEST_OBJECT_SIZE,
							      &object),
				     true);
	failures += test_expect_not_null("object allocated pointer", object);
	failures += test_expect_u64("object allocated ref count",
				    plane_vm_object_ref_count(object), 1);
	failures += test_expect_bool("object allocated alive",
				     plane_vm_object_is_alive(object), true);
	failures += test_expect_bool("object allocated internal",
				     object->internal, true);
	failures += test_expect_bool("object allocated storage",
				     object->allocated, true);
	failures += test_expect_u64("object allocated offset limit",
				    plane_vm_object_offset_limit(object),
				    TEST_OBJECT_SIZE);
	failures += test_expect_bool("object allocated deallocate",
				     plane_vm_object_deallocate(object), true);
	return failures;
}

static int test_allocate_final_deallocate_releases_pool_slot(void)
{
	struct plane_vm_object *first = NULL;
	struct plane_vm_object *second = NULL;
	int failures = 0;

	failures += test_expect_bool("object allocate first",
				     plane_vm_object_allocate(TEST_OBJECT_SIZE,
							      &first),
				     true);
	failures += test_expect_bool("object allocate first release",
				     plane_vm_object_deallocate(first), true);
	failures += test_expect_bool("object allocate second",
				     plane_vm_object_allocate(TEST_OBJECT_SIZE,
							      &second),
				     true);
	failures += test_expect_ptr("object allocate reused slot",
				    second, first);
	failures += test_expect_bool("object allocate second alive",
				     plane_vm_object_is_alive(second), true);
	failures += test_expect_bool("object allocate second storage",
				     second->allocated, true);
	failures += test_expect_bool("object allocate second release",
				     plane_vm_object_deallocate(second), true);
	return failures;
}

static int test_allocate_uses_added_zone_storage_after_bootstrap_pool(void)
{
	struct plane_vm_object *objects[TEST_BOOTSTRAP_OBJECT_POOL_SIZE +
				       TEST_OBJECT_EXTRA_POOL_SIZE];
	int failures = 0;

	for (uint64_t i = 0; i < TEST_ARRAY_SIZE(objects); i++) {
		objects[i] = NULL;
	}

	for (uint64_t i = 0; i < TEST_BOOTSTRAP_OBJECT_POOL_SIZE; i++) {
		failures += test_expect_bool(
			"bootstrap object allocate",
			plane_vm_object_allocate(TEST_OBJECT_SIZE, &objects[i]),
			true);
	}
	failures += test_expect_bool("bootstrap pool exhausted",
				     plane_vm_object_allocate(
					     TEST_OBJECT_SIZE,
					     &objects[TEST_BOOTSTRAP_OBJECT_POOL_SIZE]),
				     false);
	failures += test_expect_bool("add object zone storage",
				     plane_vm_object_add_zone_storage(
					     extra_object_pool,
					     TEST_OBJECT_EXTRA_POOL_SIZE,
					     &extra_object_segment),
				     true);
	for (uint64_t i = TEST_BOOTSTRAP_OBJECT_POOL_SIZE;
	     i < TEST_ARRAY_SIZE(objects);
	     i++) {
		failures += test_expect_bool(
			"expanded object allocate",
			plane_vm_object_allocate(TEST_OBJECT_SIZE, &objects[i]),
			true);
		failures += test_expect_bool("expanded object allocated flag",
					     objects[i]->allocated, true);
	}
	failures += test_expect_bool("expanded object pool exhausted",
				     plane_vm_object_allocate(
					     TEST_OBJECT_SIZE,
					     &objects[0]),
				     false);

	for (uint64_t i = 0; i < TEST_ARRAY_SIZE(objects); i++) {
		if (objects[i] != NULL) {
			failures += test_expect_bool("object deallocate",
						     plane_vm_object_deallocate(
							     objects[i]),
						     true);
		}
	}

	return failures;
}

static int test_allocate_deallocate_nonfinal_reference(void)
{
	struct plane_vm_object *object = NULL;
	int failures = 0;

	failures += test_expect_bool("object allocate",
				     plane_vm_object_allocate(TEST_OBJECT_SIZE,
							      &object),
				     true);
	failures += test_expect_bool("object allocated reference",
				     plane_vm_object_reference(object), true);
	failures += test_expect_bool("object allocated deallocate nonfinal",
				     plane_vm_object_deallocate(object), true);
	failures += test_expect_u64("object allocated nonfinal ref",
				    plane_vm_object_ref_count(object), 1);
	failures += test_expect_bool("object allocated nonfinal alive",
				     plane_vm_object_is_alive(object), true);
	failures += test_expect_bool("object allocated deallocate final",
				     plane_vm_object_deallocate(object), true);
	return failures;
}

static int test_allocate_final_deallocate_rejects_resident_pages(void)
{
	struct plane_vm_object *object = NULL;
	int failures = 0;

	failures += test_expect_bool("object allocate",
				     plane_vm_object_allocate(TEST_OBJECT_SIZE,
							      &object),
				     true);
	failures += test_expect_bool("object allocated insert",
				     plane_vm_object_insert_page(object, 0,
								 &allocated_page),
				     true);
	failures += test_expect_bool("object allocated deallocate resident",
				     plane_vm_object_deallocate(object), false);
	failures += test_expect_bool("object allocated resident alive",
				     plane_vm_object_is_alive(object), true);
	failures += test_expect_bool("object allocated resident storage",
				     object->allocated, true);
	failures += test_expect_u64("object allocated resident ref",
				    plane_vm_object_ref_count(object), 1);
	failures += test_expect_ptr("object allocated resident remove",
				    plane_vm_object_remove_page(object, 0),
				    &allocated_page);
	allocated_page.wire_count = 1;
	failures += test_expect_bool("object allocated insert wired",
				     plane_vm_object_insert_page(object, 0,
								 &allocated_page),
				     true);
	failures += test_expect_bool("object allocated deallocate wired",
				     plane_vm_object_deallocate(object), false);
	failures += test_expect_u64("object allocated wired count",
				    plane_vm_object_wired_page_count(object), 1);
	failures += test_expect_ptr("object allocated wired remove",
				    plane_vm_object_remove_page(object, 0),
				    &allocated_page);
	allocated_page.wire_count = 0;
	failures += test_expect_bool("object allocated deallocate final",
				     plane_vm_object_deallocate(object), true);
	return failures;
}

static int test_reference_rejects_invalid_objects(void)
{
	int failures = 0;

	failures += test_expect_bool("reference null",
				     plane_vm_object_reference(NULL), false);
	failures += test_expect_bool("object init",
				     plane_vm_object_init(&test_object,
							  TEST_OBJECT_SIZE),
				     true);
	failures += test_expect_bool("object reference",
				     plane_vm_object_reference(&test_object),
				     true);
	failures += test_expect_u64("object ref count incremented",
				    plane_vm_object_ref_count(&test_object), 2);
	test_object.ref_count = UINT64_MAX;
	failures += test_expect_bool("object reference overflow",
				     plane_vm_object_reference(&test_object),
				     false);
	failures += test_expect_u64("object overflow ref unchanged",
				    plane_vm_object_ref_count(&test_object),
				    UINT64_MAX);
	return failures;
}

static int test_deallocate_nonfinal_reference(void)
{
	int failures = 0;

	failures += test_expect_bool("object init",
				     plane_vm_object_init(&test_object,
							  TEST_OBJECT_SIZE),
				     true);
	failures += test_expect_bool("object reference",
				     plane_vm_object_reference(&test_object),
				     true);
	failures += test_expect_bool("object deallocate nonfinal",
				     plane_vm_object_deallocate(&test_object),
				     true);
	failures += test_expect_u64("object nonfinal ref count",
				    plane_vm_object_ref_count(&test_object), 1);
	failures += test_expect_bool("object nonfinal alive",
				     plane_vm_object_is_alive(&test_object),
				     true);
	return failures;
}

static int test_deallocate_final_empty_object(void)
{
	int failures = 0;

	failures += test_expect_bool("object init",
				     plane_vm_object_init(&test_object,
							  TEST_OBJECT_SIZE),
				     true);
	failures += test_expect_bool("object deallocate final",
				     plane_vm_object_deallocate(&test_object),
				     true);
	failures += test_expect_u64("object final ref count",
				    plane_vm_object_ref_count(&test_object), 0);
	failures += test_expect_bool("object final dead",
				     plane_vm_object_is_alive(&test_object),
				     false);
	failures += test_expect_u64("object final offset limit hidden",
				    plane_vm_object_offset_limit(&test_object), 0);
	failures += test_expect_bool("object final repeat deallocate",
				     plane_vm_object_deallocate(&test_object),
				     false);
	failures += test_expect_bool("object final reference rejected",
				     plane_vm_object_reference(&test_object),
				     false);
	failures += test_expect_bool("object final reinit rejected",
				     plane_vm_object_init(&test_object,
							  TEST_OBJECT_SIZE),
				     false);
	failures += test_expect_bool("object final insert rejected",
				     plane_vm_object_insert_page(
					     &test_object, 0, &allocated_page),
				     false);
	failures += test_expect_null("object final lookup rejected",
				     plane_vm_object_lookup_page(&test_object, 0));
	failures += test_expect_null("object final remove rejected",
				     plane_vm_object_remove_page(&test_object, 0));
	failures += test_expect_bool("object final wire accounting rejected",
				     plane_vm_object_account_page_wired(
					     &test_object),
				     false);
	failures += test_expect_bool("object final unwire accounting rejected",
				     plane_vm_object_account_page_unwired(
					     &test_object),
				     false);
	return failures;
}

static int test_deallocate_final_rejects_resident_pages(void)
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
	failures += test_expect_bool("object deallocate resident rejected",
				     plane_vm_object_deallocate(&test_object),
				     false);
	failures += test_expect_u64("object resident ref unchanged",
				    plane_vm_object_ref_count(&test_object), 1);
	failures += test_expect_bool("object resident alive unchanged",
				     plane_vm_object_is_alive(&test_object),
				     true);
	failures += test_expect_ptr("object resident still lookup",
				    plane_vm_object_lookup_page(&test_object, 0),
				    &allocated_page);
	failures += test_expect_ptr("object resident remove",
				    plane_vm_object_remove_page(&test_object, 0),
				    &allocated_page);
	allocated_page.wire_count = 1;
	failures += test_expect_bool("object insert wired",
				     plane_vm_object_insert_page(
					     &test_object, 0, &allocated_page),
				     true);
	failures += test_expect_bool("object deallocate wired rejected",
				     plane_vm_object_deallocate(&test_object),
				     false);
	failures += test_expect_u64("object wired ref unchanged",
				    plane_vm_object_ref_count(&test_object), 1);
	failures += test_expect_u64("object wired count unchanged",
				    plane_vm_object_wired_page_count(
					    &test_object),
				    1);
	return failures;
}

static int test_can_deallocate_reports_lifetime_preflight(void)
{
	int failures = 0;

	failures += test_expect_bool("can deallocate null",
				     plane_vm_object_can_deallocate(NULL),
				     false);
	failures += test_expect_bool("object init",
				     plane_vm_object_init(&test_object,
							  TEST_OBJECT_SIZE),
				     true);
	failures += test_expect_bool("can deallocate final empty",
				     plane_vm_object_can_deallocate(
					     &test_object),
				     true);
	failures += test_expect_bool("object insert resident",
				     plane_vm_object_insert_page(
					     &test_object, 0, &allocated_page),
				     true);
	failures += test_expect_bool("cannot deallocate final resident",
				     plane_vm_object_can_deallocate(
					     &test_object),
				     false);
	failures += test_expect_bool("object reference resident",
				     plane_vm_object_reference(&test_object),
				     true);
	failures += test_expect_bool("can deallocate nonfinal resident",
				     plane_vm_object_can_deallocate(
					     &test_object),
				     true);
	failures += test_expect_ptr("object remove resident",
				    plane_vm_object_remove_page(&test_object, 0),
				    &allocated_page);
	failures += test_expect_bool("object deallocate nonfinal",
				     plane_vm_object_deallocate(&test_object),
				     true);
	failures += test_expect_bool("object deallocate final",
				     plane_vm_object_deallocate(&test_object),
				     true);
	failures += test_expect_bool("cannot deallocate dead",
				     plane_vm_object_can_deallocate(
					     &test_object),
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
	clear_resident_hint(&test_object);
	failures += test_expect_ptr("small object list lookup",
				    plane_vm_object_lookup_page(&test_object,
								2 * PAGE_SIZE),
				    &third_allocated_page);
	failures += test_expect_ptr("small object updates hint",
				    resident_hint(&test_object),
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
	clear_resident_hint(&test_object);
	failures += test_expect_ptr("large object hash lookup",
				    plane_vm_object_lookup_page(
					    &test_object,
					    (TEST_HASH_PAGE_COUNT - 1) *
					    PAGE_SIZE),
				    &hash_pages[TEST_HASH_PAGE_COUNT - 1]);
	failures += test_expect_ptr("large object updates hint",
				    resident_hint(&test_object),
				    &hash_pages[TEST_HASH_PAGE_COUNT - 1]);
	return failures;
}

static int test_resident_hash_rehome_preserves_existing_pages(void)
{
	int failures = 0;

	failures += test_expect_bool("object init",
				     plane_vm_object_init(&test_object,
							  TEST_OBJECT_SIZE),
				     true);
	failures += test_expect_bool("insert first page",
				     plane_vm_object_insert_page(
					     &test_object, 0,
					     &allocated_page),
				     true);
	failures += test_expect_bool("insert second page",
				     plane_vm_object_insert_page(
					     &test_object, PAGE_SIZE,
					     &second_allocated_page),
				     true);
	failures += test_expect_bool("rehome resident hash",
				     plane_vm_object_rehome_resident_hash(
					     rehash_buckets,
					     TEST_REHASH_BUCKETS),
				     true);
	failures += test_expect_ptr("lookup first after rehash",
				    plane_vm_object_lookup_page(&test_object, 0),
				    &allocated_page);
	failures += test_expect_ptr("lookup second after rehash",
				    plane_vm_object_lookup_page(&test_object,
								PAGE_SIZE),
				    &second_allocated_page);
	failures += test_expect_bool("insert third after rehash",
				     plane_vm_object_insert_page(
					     &test_object, 2 * PAGE_SIZE,
					     &third_allocated_page),
				     true);
	failures += test_expect_ptr("lookup third after rehash",
				    plane_vm_object_lookup_page(&test_object,
								2 * PAGE_SIZE),
				    &third_allocated_page);
	failures += test_expect_ptr("remove first after rehash",
				    plane_vm_object_remove_page(&test_object, 0),
				    &allocated_page);
	failures += test_expect_ptr("remove third after rehash",
				    plane_vm_object_remove_page(&test_object,
								2 * PAGE_SIZE),
				    &third_allocated_page);
	return failures;
}

static int test_resident_hash_rehome_rejects_invalid_buckets(void)
{
	int failures = 0;

	failures += test_expect_bool("rehome null buckets",
				     plane_vm_object_rehome_resident_hash(
					     NULL, TEST_REHASH_BUCKETS),
				     false);
	failures += test_expect_bool("rehome zero buckets",
				     plane_vm_object_rehome_resident_hash(
					     rehash_buckets, 0),
				     false);
	failures += test_expect_bool("rehome non power two buckets",
				     plane_vm_object_rehome_resident_hash(
					     rehash_buckets,
					     TEST_REHASH_BUCKETS - 1),
				     false);
	failures += test_expect_bool("object init",
				     plane_vm_object_init(&test_object,
							  TEST_OBJECT_SIZE),
				     true);
	failures += test_expect_bool("insert page before overlap",
				     plane_vm_object_insert_page(
					     &test_object, 0,
					     &allocated_page),
				     true);
	failures += test_expect_bool("rehome valid buckets",
				     plane_vm_object_rehome_resident_hash(
					     rehash_buckets,
					     TEST_REHASH_BUCKETS),
				     true);
	failures += test_expect_bool("rehome overlapping buckets",
				     plane_vm_object_rehome_resident_hash(
					     &rehash_buckets[1],
					     TEST_REHASH_BUCKETS / 2),
				     false);
	failures += test_expect_ptr("overlap rejection preserved lookup",
				    plane_vm_object_lookup_page(&test_object, 0),
				    &allocated_page);
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

static int test_guard_page_insert_lookup_and_remove(void)
{
	struct plane_page *page;
	int failures = 0;

	failures += test_expect_bool("guard object init",
				     plane_vm_object_init(&test_object,
							  TEST_OBJECT_SIZE),
				     true);
	failures += test_expect_bool("guard page state",
				     plane_vm_page_is_guard(&guard_page),
				     true);
	failures += test_expect_bool("guard insert",
				     plane_vm_object_insert_page(
					     &test_object, PAGE_SIZE,
					     &guard_page),
				     true);
	failures += test_expect_bool("guard tabled after insert",
				     guard_page.object_tabled, true);
	failures += test_expect_bool("guard hashed after insert",
				     guard_page.object_hashed, true);
	failures += test_expect_u64("guard resident count",
				    plane_vm_object_resident_page_count(
					    &test_object),
				    1);
	failures += test_expect_u64("guard wired count",
				    plane_vm_object_wired_page_count(
					    &test_object),
				    0);
	page = plane_vm_object_lookup_page(&test_object, PAGE_SIZE);
	failures += test_expect_ptr("guard lookup", page, &guard_page);
	page = plane_vm_object_remove_page(&test_object, PAGE_SIZE);
	failures += test_expect_ptr("guard remove", page, &guard_page);
	failures += test_expect_bool("guard untabled after remove",
				     guard_page.object_tabled, false);
	failures += test_expect_bool("guard unhashed after remove",
				     guard_page.object_hashed, false);
	failures += test_expect_u64("guard resident removed",
				    plane_vm_object_resident_page_count(
					    &test_object),
				    0);
	failures += test_expect_u64("guard wired removed",
				    plane_vm_object_wired_page_count(
					    &test_object),
				    0);
	failures += test_expect_null("guard object cleared",
				     plane_vm_page_object(&guard_page));
	return failures;
}

static int test_guard_page_rejects_wire_backed_assumptions(void)
{
	int failures = 0;

	failures += test_expect_bool("guard reject init",
				     plane_vm_object_init(&test_object,
							  TEST_OBJECT_SIZE),
				     true);
	guard_page.wire_count = 1;
	failures += test_expect_bool("guard reject wired insert",
				     plane_vm_object_insert_page(
					     &test_object, 0, &guard_page),
				     false);
	failures += test_expect_u64("guard reject resident count",
				    plane_vm_object_resident_page_count(
					    &test_object),
				    0);
	failures += test_expect_u64("guard reject wired count",
				    plane_vm_object_wired_page_count(
					    &test_object),
				    0);
	guard_page.wire_count = 0;
	failures += test_expect_bool("guard insert after clear",
				     plane_vm_object_insert_page(
					     &test_object, 0, &guard_page),
				     true);
	failures += test_expect_bool("guard duplicate rejected",
				     plane_vm_object_insert_page(
					     &test_object, PAGE_SIZE,
					     &guard_page),
				     false);
	failures += test_expect_null("guard missing remove",
				     plane_vm_object_remove_page(&test_object,
								 PAGE_SIZE));
	failures += test_expect_ptr("guard remove existing",
				    plane_vm_object_remove_page(&test_object, 0),
				    &guard_page);
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
		TEST_CASE(test_allocate_rejects_invalid_inputs),
		TEST_CASE(test_allocate_initializes_internal_object),
		TEST_CASE(test_allocate_final_deallocate_releases_pool_slot),
		TEST_CASE(test_allocate_uses_added_zone_storage_after_bootstrap_pool),
		TEST_CASE(test_allocate_deallocate_nonfinal_reference),
		TEST_CASE(test_allocate_final_deallocate_rejects_resident_pages),
		TEST_CASE(test_reference_rejects_invalid_objects),
		TEST_CASE(test_deallocate_nonfinal_reference),
		TEST_CASE(test_deallocate_final_empty_object),
		TEST_CASE(test_deallocate_final_rejects_resident_pages),
		TEST_CASE(test_can_deallocate_reports_lifetime_preflight),
		TEST_CASE(test_lookup_empty_object_returns_null),
		TEST_CASE(test_insert_lookup_and_remove_page),
		TEST_CASE(test_lookup_small_object_scans_resident_list),
		TEST_CASE(test_lookup_large_object_uses_hash),
		TEST_CASE(test_resident_hash_rehome_preserves_existing_pages),
		TEST_CASE(test_resident_hash_rehome_rejects_invalid_buckets),
		TEST_CASE(test_insert_tracks_wired_page_count),
		TEST_CASE(test_guard_page_insert_lookup_and_remove),
		TEST_CASE(test_guard_page_rejects_wire_backed_assumptions),
		TEST_CASE(test_insert_rejects_invalid_page_or_offset),
		TEST_CASE(test_rejects_duplicate_and_missing_remove),
		TEST_CASE(test_multiple_pages_update_counts),
		TEST_CASE(test_reinsert_page_uses_new_offset),
	};

	return test_run_cases_with_fixture("vm_object_test", cases,
					   TEST_ARRAY_SIZE(cases),
					   reset_vm_object_test, NULL);
}
