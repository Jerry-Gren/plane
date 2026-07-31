#include <stdint.h>

#include <plane/compiler.h>
#include <plane/mm.h>

#include "support/test.h"
#include "../kernel/mm/vm_zone_internal.h"

#define TEST_ZONE_COUNT 4
#define TEST_ZONE_EXTRA_COUNT 2

struct test_zone_elem {
	uint64_t value;
	uint64_t other;
};

static struct plane_vm_zone test_zone;
static struct plane_vm_zone_segment test_segment;
static struct plane_vm_zone_segment test_extra_segment;
static struct test_zone_elem test_storage[TEST_ZONE_COUNT];
static struct test_zone_elem test_extra_storage[TEST_ZONE_EXTRA_COUNT];
static uint8_t test_unaligned_storage[sizeof(test_storage) + 1] __aligned(sizeof(void *));
static struct test_zone_elem foreign_elem;

static void reset_vm_zone_test(void)
{
	test_zone = (struct plane_vm_zone){0};
	test_segment = (struct plane_vm_zone_segment){0};
	test_extra_segment = (struct plane_vm_zone_segment){0};
	for (uint64_t i = 0; i < TEST_ZONE_COUNT; i++) {
		test_storage[i] = (struct test_zone_elem){0};
	}
	for (uint64_t i = 0; i < TEST_ZONE_EXTRA_COUNT; i++) {
		test_extra_storage[i] = (struct test_zone_elem){0};
	}
	for (uint64_t i = 0; i < sizeof(test_unaligned_storage); i++) {
		test_unaligned_storage[i] = 0;
	}
	foreign_elem = (struct test_zone_elem){0};
}

static int test_init_rejects_invalid_inputs(void)
{
	int failures = 0;

	failures += test_expect_bool("zone init null zone",
				     plane_vm_zone_init(NULL,
							sizeof(test_storage[0]),
							test_storage,
							TEST_ZONE_COUNT,
							&test_segment),
				     false);
	failures += test_expect_bool("zone init tiny element",
				     plane_vm_zone_init(&test_zone,
							sizeof(void *) - 1,
							test_storage,
							TEST_ZONE_COUNT,
							&test_segment),
				     false);
	failures += test_expect_bool("zone init unaligned element",
				     plane_vm_zone_init(&test_zone,
							sizeof(void *) + 1,
							test_storage,
							TEST_ZONE_COUNT,
							&test_segment),
				     false);
	failures += test_expect_bool("zone init null storage",
				     plane_vm_zone_init(&test_zone,
							sizeof(test_storage[0]),
							NULL,
							TEST_ZONE_COUNT,
							&test_segment),
				     false);
	failures += test_expect_bool("zone init zero count",
				     plane_vm_zone_init(&test_zone,
							sizeof(test_storage[0]),
							test_storage, 0,
							&test_segment),
				     false);
	failures += test_expect_bool("zone init null segment",
				     plane_vm_zone_init(&test_zone,
							sizeof(test_storage[0]),
							test_storage,
							TEST_ZONE_COUNT,
							NULL),
				     false);
	failures += test_expect_bool("zone init valid",
				     plane_vm_zone_init(&test_zone,
							sizeof(test_storage[0]),
							test_storage,
							TEST_ZONE_COUNT,
							&test_segment),
				     true);
	failures += test_expect_bool("zone init repeated",
				     plane_vm_zone_init(&test_zone,
							sizeof(test_extra_storage[0]),
							test_extra_storage,
							TEST_ZONE_EXTRA_COUNT,
							&test_extra_segment),
				     false);
	reset_vm_zone_test();
	test_segment.storage = test_storage;
	failures += test_expect_bool("zone init used segment",
				     plane_vm_zone_init(&test_zone,
							sizeof(test_storage[0]),
							test_storage,
							TEST_ZONE_COUNT,
							&test_segment),
				     false);
	failures += test_expect_u64("zone failed init capacity",
				    plane_vm_zone_capacity(&test_zone), 0);
	return failures;
}

static int test_alloc_returns_zeroed_elements(void)
{
	struct test_zone_elem *elem;
	int failures = 0;

	failures += test_expect_bool("zone init",
				     plane_vm_zone_init(&test_zone,
							sizeof(test_storage[0]),
							test_storage,
							TEST_ZONE_COUNT,
							&test_segment),
				     true);
	elem = plane_vm_zone_alloc(&test_zone);
	failures += test_expect_not_null("zone alloc", elem);
	elem->value = 0x1234;
	elem->other = 0x5678;
	failures += test_expect_bool("zone free",
				     plane_vm_zone_free(&test_zone, elem), true);
	elem = plane_vm_zone_alloc(&test_zone);
	failures += test_expect_not_null("zone realloc", elem);
	failures += test_expect_u64("zone realloc value zero", elem->value, 0);
	failures += test_expect_u64("zone realloc other zero", elem->other, 0);
	return failures;
}

static int test_exhaustion_and_reuse(void)
{
	void *elems[TEST_ZONE_COUNT];
	int failures = 0;

	failures += test_expect_bool("zone init",
				     plane_vm_zone_init(&test_zone,
							sizeof(test_storage[0]),
							test_storage,
							TEST_ZONE_COUNT,
							&test_segment),
				     true);
	for (uint64_t i = 0; i < TEST_ZONE_COUNT; i++) {
		elems[i] = plane_vm_zone_alloc(&test_zone);
		failures += test_expect_not_null("zone alloc", elems[i]);
	}
	failures += test_expect_null("zone exhausted",
				     plane_vm_zone_alloc(&test_zone));
	failures += test_expect_bool("zone free",
				     plane_vm_zone_free(&test_zone, elems[1]),
				     true);
	failures += test_expect_ptr("zone reuse",
				    plane_vm_zone_alloc(&test_zone), elems[1]);
	return failures;
}

static int test_add_storage_expands_capacity(void)
{
	void *elems[TEST_ZONE_COUNT + TEST_ZONE_EXTRA_COUNT];
	int failures = 0;

	failures += test_expect_bool("zone init",
				     plane_vm_zone_init(&test_zone,
							sizeof(test_storage[0]),
							test_storage,
							TEST_ZONE_COUNT,
							&test_segment),
				     true);
	failures += test_expect_bool("zone add storage",
				     plane_vm_zone_add_storage(
					     &test_zone, test_extra_storage,
					     TEST_ZONE_EXTRA_COUNT,
					     &test_extra_segment),
				     true);
	failures += test_expect_u64("zone expanded capacity",
				    plane_vm_zone_capacity(&test_zone),
				    TEST_ZONE_COUNT + TEST_ZONE_EXTRA_COUNT);

	for (uint64_t i = 0; i < TEST_ARRAY_SIZE(elems); i++) {
		elems[i] = plane_vm_zone_alloc(&test_zone);
		failures += test_expect_not_null("zone expanded alloc",
						 elems[i]);
	}
	failures += test_expect_null("zone expanded exhausted",
				     plane_vm_zone_alloc(&test_zone));
	return failures;
}

static int test_add_storage_rejects_invalid_segments(void)
{
	int failures = 0;

	failures += test_expect_bool("zone init",
				     plane_vm_zone_init(&test_zone,
							sizeof(test_storage[0]),
							test_storage,
							TEST_ZONE_COUNT,
							&test_segment),
				     true);
	failures += test_expect_bool("zone add overlapping storage",
				     plane_vm_zone_add_storage(
					     &test_zone, &test_storage[1],
					     TEST_ZONE_EXTRA_COUNT,
					     &test_extra_segment),
				     false);
	failures += test_expect_bool("zone add unaligned storage",
				     plane_vm_zone_add_storage(
					     &test_zone,
					     &test_unaligned_storage[1],
					     TEST_ZONE_EXTRA_COUNT,
					     &test_extra_segment),
				     false);
	test_extra_segment.storage = test_extra_storage;
	failures += test_expect_bool("zone add used segment",
				     plane_vm_zone_add_storage(
					     &test_zone, test_extra_storage,
					     TEST_ZONE_EXTRA_COUNT,
					     &test_extra_segment),
				     false);
	failures += test_expect_u64("zone invalid add capacity",
				    plane_vm_zone_capacity(&test_zone),
				    TEST_ZONE_COUNT);
	return failures;
}

static int test_free_rejects_foreign_and_double_free(void)
{
	struct test_zone_elem *elem;
	int failures = 0;

	failures += test_expect_bool("zone init",
				     plane_vm_zone_init(&test_zone,
							sizeof(test_storage[0]),
							test_storage,
							TEST_ZONE_COUNT,
							&test_segment),
				     true);
	elem = plane_vm_zone_alloc(&test_zone);
	failures += test_expect_not_null("zone alloc", elem);
	failures += test_expect_bool("zone foreign free",
				     plane_vm_zone_free(&test_zone,
							&foreign_elem),
				     false);
	failures += test_expect_bool("zone free",
				     plane_vm_zone_free(&test_zone, elem), true);
	failures += test_expect_bool("zone double free",
				     plane_vm_zone_free(&test_zone, elem), false);
	return failures;
}

int main(void)
{
	const struct test_case cases[] = {
		TEST_CASE(test_init_rejects_invalid_inputs),
		TEST_CASE(test_alloc_returns_zeroed_elements),
		TEST_CASE(test_exhaustion_and_reuse),
		TEST_CASE(test_add_storage_expands_capacity),
		TEST_CASE(test_add_storage_rejects_invalid_segments),
		TEST_CASE(test_free_rejects_foreign_and_double_free),
	};

	return test_run_cases_with_fixture("vm_zone_test", cases,
					   TEST_ARRAY_SIZE(cases),
					   reset_vm_zone_test, NULL);
}
