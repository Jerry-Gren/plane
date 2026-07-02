#ifndef TEST_SUPPORT_MEMMAP_H
#define TEST_SUPPORT_MEMMAP_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <plane/memmap.h>

#include "test.h"

struct test_memmap_sanitize_case {
	const char *name;
	struct plane_mem_region input[PLANE_MAX_MEMMAP_ENTRIES];
	uint64_t input_count;
	struct plane_mem_region expected[PLANE_MAX_MEMMAP_ENTRIES];
	uint64_t expected_count;
};

struct test_memmap_reserve_case {
	const char *name;
	struct plane_mem_region input[PLANE_MAX_MEMMAP_ENTRIES];
	uint64_t input_count;
	uint64_t reserve_base;
	uint64_t reserve_length;
	uint32_t reserve_type;
	int expected_ret;
	struct plane_mem_region expected[PLANE_MAX_MEMMAP_ENTRIES];
	uint64_t expected_count;
};

static inline const char *test_mem_type_name(uint32_t type)
{
	switch (type) {
	case PLANE_MEM_INVALID:
		return "INVALID";
	case PLANE_MEM_USABLE:
		return "USABLE";
	case PLANE_MEM_RESERVED:
		return "RESERVED";
	case PLANE_MEM_ACPI_RECLAIMABLE:
		return "ACPI_RECLAIMABLE";
	case PLANE_MEM_ACPI_NVS:
		return "ACPI_NVS";
	case PLANE_MEM_BAD_MEMORY:
		return "BAD_MEMORY";
	case PLANE_MEM_BOOTLOADER_RECLAIMABLE:
		return "BOOTLOADER_RECLAIMABLE";
	case PLANE_MEM_EXECUTABLE_AND_MODULES:
		return "EXECUTABLE_AND_MODULES";
	case PLANE_MEM_FRAMEBUFFER:
		return "FRAMEBUFFER";
	case PLANE_MEM_RESERVED_MAPPED:
		return "RESERVED_MAPPED";
	default:
		return "OTHER";
	}
}

static inline void test_dump_memmap(const char *label,
				    const struct plane_mem_info *mem)
{
	printf("%s (%llu entries):\n", label,
	       (unsigned long long)mem->entry_count);
	for (uint64_t i = 0; i < mem->entry_count; i++) {
		printf("  [%llu] base=0x%llx length=0x%llx type=%s(%u)\n",
		       (unsigned long long)i,
		       (unsigned long long)mem->map[i].base,
		       (unsigned long long)mem->map[i].length,
		       test_mem_type_name(mem->map[i].type),
		       mem->map[i].type);
	}
}

static inline int test_memmaps_equal(const struct plane_mem_info *actual,
				     const struct plane_mem_info *expected)
{
	if (actual->entry_count != expected->entry_count) {
		return 0;
	}

	return memcmp(actual->map, expected->map,
		      actual->entry_count * sizeof(actual->map[0])) == 0;
}

static inline int test_report_memmap_failure(const char *name,
					     int expected_ret,
					     int actual_ret,
					     const struct plane_mem_info *expected,
					     const struct plane_mem_info *actual)
{
	test_fail("%s", name);
	printf("ret: expected=%d actual=%d\n", expected_ret, actual_ret);
	if (expected != NULL) {
		test_dump_memmap("expected", expected);
	}
	if (actual != NULL) {
		test_dump_memmap("actual", actual);
	}
	return 1;
}

static inline int test_run_memmap_sanitize_case(
	const struct test_memmap_sanitize_case *tc)
{
	struct plane_mem_info actual = {0};
	struct plane_mem_info expected = {0};
	int ret;

	actual.entry_count = tc->input_count;
	memcpy(actual.map, tc->input, tc->input_count * sizeof(tc->input[0]));

	expected.entry_count = tc->expected_count;
	memcpy(expected.map, tc->expected,
	       tc->expected_count * sizeof(tc->expected[0]));

	ret = plane_sanitize_memory_map(&actual);
	if (ret == 1 && test_memmaps_equal(&actual, &expected)) {
		return 0;
	}

	return test_report_memmap_failure(tc->name, 1, ret, &expected, &actual);
}

static inline int test_run_memmap_reserve_case(
	const struct test_memmap_reserve_case *tc)
{
	struct plane_mem_info actual = {0};
	struct plane_mem_info expected = {0};
	int ret;

	actual.entry_count = tc->input_count;
	memcpy(actual.map, tc->input, tc->input_count * sizeof(tc->input[0]));

	expected.entry_count = tc->expected_count;
	memcpy(expected.map, tc->expected,
	       tc->expected_count * sizeof(tc->expected[0]));

	ret = plane_memmap_reserve(&actual, tc->reserve_base,
				   tc->reserve_length, tc->reserve_type);
	if (ret == tc->expected_ret && test_memmaps_equal(&actual, &expected)) {
		return 0;
	}

	return test_report_memmap_failure(tc->name, tc->expected_ret, ret,
					  &expected, &actual);
}

static inline int test_run_memmap_sanitize_cases(
	const struct test_memmap_sanitize_case *cases,
	size_t count)
{
	int failures = 0;

	for (size_t i = 0; i < count; i++) {
		failures += test_run_memmap_sanitize_case(&cases[i]);
	}

	return failures;
}

static inline int test_run_memmap_reserve_cases(
	const struct test_memmap_reserve_case *cases,
	size_t count)
{
	int failures = 0;

	for (size_t i = 0; i < count; i++) {
		failures += test_run_memmap_reserve_case(&cases[i]);
	}

	return failures;
}

#endif /* TEST_SUPPORT_MEMMAP_H */
