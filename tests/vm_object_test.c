#include <stdint.h>

#include <plane/mm.h>
#include <plane/vm_object.h>

#include "support/test.h"

#define TEST_OBJECT_PAGES 8
#define TEST_OBJECT_SIZE (TEST_OBJECT_PAGES * PAGE_SIZE)

struct plane_page {
	struct plane_vm_object *object;
	uint64_t object_offset;
	enum plane_page_state state;
};

static struct plane_vm_object_page test_pages[TEST_OBJECT_PAGES];
static struct plane_vm_object_page second_pages[TEST_OBJECT_PAGES];
static struct plane_vm_object test_object;
static struct plane_vm_object second_object;
static struct plane_page allocated_page;
static struct plane_page free_page;

enum plane_page_state plane_page_state(const struct plane_page *page)
{
	if (page == NULL) {
		return PLANE_PAGE_INVALID;
	}

	return page->state;
}

struct plane_vm_object *plane_page_vm_object(const struct plane_page *page)
{
	if (page == NULL) {
		return NULL;
	}

	return page->object;
}

bool plane_page_vm_object_offset(const struct plane_page *page, uint64_t *offset)
{
	if (page == NULL ||
	    page->object == NULL ||
	    offset == NULL) {
		return false;
	}

	*offset = page->object_offset;
	return true;
}

bool plane_page_attach_vm_object(struct plane_page *page,
				 struct plane_vm_object *object,
				 uint64_t offset)
{
	if (page == NULL ||
	    object == NULL ||
	    page->object != NULL ||
	    page->state != PLANE_PAGE_ALLOCATED) {
		return false;
	}

	page->object = object;
	page->object_offset = offset;
	return true;
}

bool plane_page_detach_vm_object(struct plane_page *page,
				 struct plane_vm_object *object,
				 uint64_t offset)
{
	if (page == NULL ||
	    object == NULL ||
	    page->object != object ||
	    page->object_offset != offset ||
	    page->state != PLANE_PAGE_ALLOCATED) {
		return false;
	}

	page->object = NULL;
	page->object_offset = 0;
	return true;
}

static void reset_vm_object_test(void)
{
	test_object = (struct plane_vm_object){0};
	second_object = (struct plane_vm_object){0};
	for (uint64_t i = 0; i < TEST_OBJECT_PAGES; i++) {
		test_pages[i] = (struct plane_vm_object_page){0};
		second_pages[i] = (struct plane_vm_object_page){0};
	}
	allocated_page = (struct plane_page){0};
	free_page = (struct plane_page){0};
	allocated_page.state = PLANE_PAGE_ALLOCATED;
	free_page.state = PLANE_PAGE_FREE;
}

static int test_init_rejects_invalid_inputs(void)
{
	int failures = 0;

	failures += test_expect_bool("object init null object",
				     plane_vm_object_init(NULL, test_pages,
							  TEST_OBJECT_PAGES,
							  TEST_OBJECT_SIZE),
				     false);
	failures += test_expect_bool("object init null pages",
				     plane_vm_object_init(&test_object, NULL,
							  TEST_OBJECT_PAGES,
							  TEST_OBJECT_SIZE),
				     false);
	failures += test_expect_bool("object init empty storage",
				     plane_vm_object_init(&test_object,
							  test_pages, 0,
							  TEST_OBJECT_SIZE),
				     false);
	failures += test_expect_bool("object init zero size",
				     plane_vm_object_init(&test_object,
							  test_pages,
							  TEST_OBJECT_PAGES, 0),
				     false);
	failures += test_expect_bool("object init unaligned size",
				     plane_vm_object_init(&test_object,
							  test_pages,
							  TEST_OBJECT_PAGES,
							  TEST_OBJECT_SIZE - 1),
				     false);
	return failures;
}

static int test_init_is_one_shot(void)
{
	int failures = 0;

	failures += test_expect_bool("object init",
				     plane_vm_object_init(&test_object,
							  test_pages,
							  TEST_OBJECT_PAGES,
							  TEST_OBJECT_SIZE),
				     true);
	failures += test_expect_bool("object repeat init",
				     plane_vm_object_init(&test_object,
							  test_pages,
							  TEST_OBJECT_PAGES,
							  TEST_OBJECT_SIZE),
				     false);
	return failures;
}

static int test_insert_lookup_and_remove_page(void)
{
	struct plane_page *page;
	uint64_t offset = 0;
	int failures = 0;

	failures += test_expect_bool("object init",
				     plane_vm_object_init(&test_object,
							  test_pages,
							  TEST_OBJECT_PAGES,
							  TEST_OBJECT_SIZE),
				     true);
	failures += test_expect_bool("object insert",
				     plane_vm_object_insert_page(
					     &test_object, PAGE_SIZE,
					     &allocated_page),
				     true);
	failures += test_expect_ptr("page object",
				    plane_page_vm_object(&allocated_page),
				    &test_object);
	failures += test_expect_bool("page object offset query",
				     plane_page_vm_object_offset(&allocated_page,
								 &offset),
				     true);
	failures += test_expect_u64("page object offset", offset, PAGE_SIZE);
	page = plane_vm_object_lookup_page(&test_object, PAGE_SIZE);
	failures += test_expect_ptr("object lookup", page, &allocated_page);
	page = plane_vm_object_remove_page(&test_object, PAGE_SIZE);
	failures += test_expect_ptr("object remove", page, &allocated_page);
	failures += test_expect_null("page object cleared",
				     plane_page_vm_object(&allocated_page));
	failures += test_expect_bool("page object offset cleared",
				     plane_page_vm_object_offset(&allocated_page,
								 &offset),
				     false);
	failures += test_expect_null("object lookup removed",
				     plane_vm_object_lookup_page(&test_object,
								 PAGE_SIZE));
	return failures;
}

static int test_insert_rejects_invalid_page_or_offset(void)
{
	int failures = 0;

	failures += test_expect_bool("object init",
				     plane_vm_object_init(&test_object,
							  test_pages,
							  TEST_OBJECT_PAGES,
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
							  test_pages,
							  TEST_OBJECT_PAGES,
							  TEST_OBJECT_SIZE),
				     true);
	failures += test_expect_bool("object insert",
				     plane_vm_object_insert_page(
					     &test_object, 0, &allocated_page),
				     true);
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
							  second_pages,
							  TEST_OBJECT_PAGES,
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
	return failures;
}

int main(void)
{
	const struct test_case cases[] = {
		TEST_CASE(test_init_rejects_invalid_inputs),
		TEST_CASE(test_init_is_one_shot),
		TEST_CASE(test_insert_lookup_and_remove_page),
		TEST_CASE(test_insert_rejects_invalid_page_or_offset),
		TEST_CASE(test_rejects_duplicate_and_missing_remove),
	};

	return test_run_cases_with_fixture("vm_object_test", cases,
					   TEST_ARRAY_SIZE(cases),
					   reset_vm_object_test, NULL);
}
