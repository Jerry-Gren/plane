#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <plane/memmap.h>

#include "support/test.h"

struct test_case {
	const char *name;
	struct plane_mem_region input[PLANE_MAX_MEMMAP_ENTRIES];
	uint64_t input_count;
	struct plane_mem_region expected[PLANE_MAX_MEMMAP_ENTRIES];
	uint64_t expected_count;
};

static const char *mem_type_name(uint32_t type) {
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

static void dump_map(const char *label, const struct plane_mem_info *mem) {
	printf("%s (%llu entries):\n", label, (unsigned long long)mem->entry_count);
	for (uint64_t i = 0; i < mem->entry_count; i++) {
		printf("  [%llu] base=0x%llx length=0x%llx type=%s(%u)\n",
		       (unsigned long long)i,
		       (unsigned long long)mem->map[i].base,
		       (unsigned long long)mem->map[i].length,
		       mem_type_name(mem->map[i].type),
		       mem->map[i].type);
	}
}

static int maps_equal(const struct plane_mem_info *actual,
		      const struct plane_mem_info *expected) {
	if (actual->entry_count != expected->entry_count) {
		return 0;
	}

	return memcmp(actual->map, expected->map,
		      actual->entry_count * sizeof(actual->map[0])) == 0;
}

static int run_case(const struct test_case *tc) {
	struct plane_mem_info actual = {0};
	struct plane_mem_info expected = {0};

	actual.entry_count = tc->input_count;
	memcpy(actual.map, tc->input, tc->input_count * sizeof(tc->input[0]));

	expected.entry_count = tc->expected_count;
	memcpy(expected.map, tc->expected,
	       tc->expected_count * sizeof(tc->expected[0]));

	int ret = plane_sanitize_memory_map(&actual);
	if (ret == 1 && maps_equal(&actual, &expected)) {
		return 0;
	}

	printf("FAIL: %s\n", tc->name);
	printf("expected ret=1 actual ret=%d\n", ret);
	dump_map("expected", &expected);
	dump_map("actual", &actual);
	return 1;
}

struct reserve_test_case {
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

static int run_reserve_case(const struct reserve_test_case *tc) {
	struct plane_mem_info actual = {0};
	struct plane_mem_info expected = {0};

	actual.entry_count = tc->input_count;
	memcpy(actual.map, tc->input, tc->input_count * sizeof(tc->input[0]));

	expected.entry_count = tc->expected_count;
	memcpy(expected.map, tc->expected,
	       tc->expected_count * sizeof(tc->expected[0]));

	int ret = plane_memmap_reserve(&actual, tc->reserve_base,
				       tc->reserve_length,
				       tc->reserve_type);
	if (ret == tc->expected_ret && maps_equal(&actual, &expected)) {
		return 0;
	}

	printf("FAIL: %s\n", tc->name);
	printf("expected ret=%d actual ret=%d\n", tc->expected_ret, ret);
	dump_map("expected", &expected);
	dump_map("actual", &actual);
	return 1;
}

static int run_full_map_reserve_failure_case(void) {
	struct plane_mem_info actual = {0};
	struct plane_mem_info expected = {0};

	actual.entry_count = PLANE_MAX_MEMMAP_ENTRIES;
	for (uint64_t i = 0; i < PLANE_MAX_MEMMAP_ENTRIES; i++) {
		actual.map[i].base = i * 0x2000;
		actual.map[i].length = 0x1000;
		actual.map[i].type = PLANE_MEM_USABLE;
	}
	expected = actual;

	int ret = plane_memmap_reserve(&actual, 0x1000, 0x1000,
				       PLANE_MEM_RESERVED);
	if (ret == 0 && maps_equal(&actual, &expected)) {
		return 0;
	}

	printf("FAIL: full map reserve fails without modifying map\n");
	printf("expected ret=0 actual ret=%d\n", ret);
	dump_map("expected", &expected);
	dump_map("actual", &actual);
	return 1;
}

static int run_sanitize_too_many_regions_failure_case(void) {
	struct plane_mem_info actual = {0};
	const uint64_t reservation_count = PLANE_MAX_MEMMAP_ENTRIES / 2;

	actual.entry_count = reservation_count + 1;
	actual.map[0].base = 0;
	actual.map[0].length = (reservation_count * 2 + 2) * 0x1000;
	actual.map[0].type = PLANE_MEM_USABLE;

	for (uint64_t i = 0; i < reservation_count; i++) {
		actual.map[i + 1].base = (i * 2 + 1) * 0x1000;
		actual.map[i + 1].length = 0x1000;
		actual.map[i + 1].type = PLANE_MEM_RESERVED;
	}

	int ret = plane_sanitize_memory_map(&actual);
	if (ret == 0) {
		return 0;
	}

	printf("FAIL: sanitize reports too many output regions\n");
	printf("expected ret=0 actual ret=%d\n", ret);
	dump_map("actual", &actual);
	return 1;
}

static int run_sanitize_overflow_failure_case(void) {
	struct plane_mem_info actual = {0};

	actual.entry_count = 1;
	actual.map[0].base = UINT64_MAX - 0x1000;
	actual.map[0].length = 0x2000;
	actual.map[0].type = PLANE_MEM_USABLE;

	int ret = plane_sanitize_memory_map(&actual);
	if (ret == 0) {
		return 0;
	}

	printf("FAIL: sanitize rejects overflowing region\n");
	printf("expected ret=0 actual ret=%d\n", ret);
	dump_map("actual", &actual);
	return 1;
}

static int run_reserve_overflow_failure_case(void) {
	struct plane_mem_info actual = {0};
	struct plane_mem_info expected = {0};

	actual.entry_count = 1;
	actual.map[0].base = 0x1000;
	actual.map[0].length = 0x2000;
	actual.map[0].type = PLANE_MEM_USABLE;
	expected = actual;

	int ret = plane_memmap_reserve(&actual, UINT64_MAX - 0x1000, 0x2000,
				       PLANE_MEM_RESERVED);
	if (ret == 0 && maps_equal(&actual, &expected)) {
		return 0;
	}

	printf("FAIL: overflowing reserve fails without modifying map\n");
	printf("expected ret=0 actual ret=%d\n", ret);
	dump_map("expected", &expected);
	dump_map("actual", &actual);
	return 1;
}

int main(void) {
	const struct test_case cases[] = {
		{
			.name = "drops zero-length entries and aligns usable regions",
			.input = {
				{ .base = 0x1003, .length = 0x2ffc, .type = PLANE_MEM_USABLE },
				{ .base = 0x8000, .length = 0x0, .type = PLANE_MEM_RESERVED },
			},
			.input_count = 2,
			.expected = {
				{ .base = 0x2000, .length = 0x1000, .type = PLANE_MEM_USABLE },
			},
			.expected_count = 1,
		},
		{
			.name = "sorts entries and merges overlapping entries with the same type",
			.input = {
				{ .base = 0x3000, .length = 0x3000, .type = PLANE_MEM_RESERVED },
				{ .base = 0x1000, .length = 0x3000, .type = PLANE_MEM_RESERVED },
			},
			.input_count = 2,
			.expected = {
				{ .base = 0x1000, .length = 0x5000, .type = PLANE_MEM_RESERVED },
			},
			.expected_count = 1,
		},
		{
			.name = "reserved region splits a usable region",
			.input = {
				{ .base = 0x1000, .length = 0x9000, .type = PLANE_MEM_USABLE },
				{ .base = 0x3000, .length = 0x2000, .type = PLANE_MEM_RESERVED },
			},
			.input_count = 2,
			.expected = {
				{ .base = 0x1000, .length = 0x2000, .type = PLANE_MEM_USABLE },
				{ .base = 0x3000, .length = 0x2000, .type = PLANE_MEM_RESERVED },
				{ .base = 0x5000, .length = 0x5000, .type = PLANE_MEM_USABLE },
			},
			.expected_count = 3,
		},
		{
			.name = "reserved region truncates the head of a usable region",
			.input = {
				{ .base = 0x1000, .length = 0x3000, .type = PLANE_MEM_RESERVED },
				{ .base = 0x2000, .length = 0x5000, .type = PLANE_MEM_USABLE },
			},
			.input_count = 2,
			.expected = {
				{ .base = 0x1000, .length = 0x3000, .type = PLANE_MEM_RESERVED },
				{ .base = 0x4000, .length = 0x3000, .type = PLANE_MEM_USABLE },
			},
			.expected_count = 2,
		},
		{
			.name = "reserved region truncates the tail of a usable region",
			.input = {
				{ .base = 0x1000, .length = 0x5000, .type = PLANE_MEM_USABLE },
				{ .base = 0x4000, .length = 0x3000, .type = PLANE_MEM_RESERVED },
			},
			.input_count = 2,
			.expected = {
				{ .base = 0x1000, .length = 0x3000, .type = PLANE_MEM_USABLE },
				{ .base = 0x4000, .length = 0x3000, .type = PLANE_MEM_RESERVED },
			},
			.expected_count = 2,
		},
		{
			.name = "same-base reserved region preserves usable tail type",
			.input = {
				{ .base = 0x1000, .length = 0x9000, .type = PLANE_MEM_USABLE },
				{ .base = 0x1000, .length = 0x2000, .type = PLANE_MEM_RESERVED },
			},
			.input_count = 2,
			.expected = {
				{ .base = 0x1000, .length = 0x2000, .type = PLANE_MEM_RESERVED },
				{ .base = 0x3000, .length = 0x7000, .type = PLANE_MEM_USABLE },
			},
			.expected_count = 2,
		},
		{
			.name = "same-rank specific types degrade only the overlap to reserved",
			.input = {
				{ .base = 0x1000, .length = 0x3000, .type = PLANE_MEM_ACPI_NVS },
				{ .base = 0x2000, .length = 0x3000, .type = PLANE_MEM_BAD_MEMORY },
			},
			.input_count = 2,
			.expected = {
				{ .base = 0x1000, .length = 0x1000, .type = PLANE_MEM_ACPI_NVS },
				{ .base = 0x2000, .length = 0x2000, .type = PLANE_MEM_RESERVED },
				{ .base = 0x4000, .length = 0x1000, .type = PLANE_MEM_BAD_MEMORY },
			},
			.expected_count = 3,
		},
	};
	const struct reserve_test_case reserve_cases[] = {
		{
			.name = "reserve splits a usable region",
			.input = {
				{ .base = 0x1000, .length = 0x9000, .type = PLANE_MEM_USABLE },
			},
			.input_count = 1,
			.reserve_base = 0x3000,
			.reserve_length = 0x2000,
			.reserve_type = PLANE_MEM_FRAMEBUFFER,
			.expected_ret = 1,
			.expected = {
				{ .base = 0x1000, .length = 0x2000, .type = PLANE_MEM_USABLE },
				{ .base = 0x3000, .length = 0x2000, .type = PLANE_MEM_FRAMEBUFFER },
				{ .base = 0x5000, .length = 0x5000, .type = PLANE_MEM_USABLE },
			},
			.expected_count = 3,
		},
		{
			.name = "reserve truncates the head of a usable region",
			.input = {
				{ .base = 0x1000, .length = 0x6000, .type = PLANE_MEM_USABLE },
			},
			.input_count = 1,
			.reserve_base = 0x1000,
			.reserve_length = 0x2000,
			.reserve_type = PLANE_MEM_EXECUTABLE_AND_MODULES,
			.expected_ret = 1,
			.expected = {
				{ .base = 0x1000, .length = 0x2000, .type = PLANE_MEM_EXECUTABLE_AND_MODULES },
				{ .base = 0x3000, .length = 0x4000, .type = PLANE_MEM_USABLE },
			},
			.expected_count = 2,
		},
		{
			.name = "reserve truncates the tail of a usable region",
			.input = {
				{ .base = 0x1000, .length = 0x6000, .type = PLANE_MEM_USABLE },
			},
			.input_count = 1,
			.reserve_base = 0x5000,
			.reserve_length = 0x2000,
			.reserve_type = PLANE_MEM_BOOTLOADER_RECLAIMABLE,
			.expected_ret = 1,
			.expected = {
				{ .base = 0x1000, .length = 0x4000, .type = PLANE_MEM_USABLE },
				{ .base = 0x5000, .length = 0x2000, .type = PLANE_MEM_BOOTLOADER_RECLAIMABLE },
			},
			.expected_count = 2,
		},
		{
			.name = "reserve overlaps an existing framebuffer reservation",
			.input = {
				{ .base = 0x1000, .length = 0x2000, .type = PLANE_MEM_USABLE },
				{ .base = 0x3000, .length = 0x2000, .type = PLANE_MEM_FRAMEBUFFER },
				{ .base = 0x5000, .length = 0x2000, .type = PLANE_MEM_USABLE },
			},
			.input_count = 3,
			.reserve_base = 0x3000,
			.reserve_length = 0x2000,
			.reserve_type = PLANE_MEM_FRAMEBUFFER,
			.expected_ret = 1,
			.expected = {
				{ .base = 0x1000, .length = 0x2000, .type = PLANE_MEM_USABLE },
				{ .base = 0x3000, .length = 0x2000, .type = PLANE_MEM_FRAMEBUFFER },
				{ .base = 0x5000, .length = 0x2000, .type = PLANE_MEM_USABLE },
			},
			.expected_count = 3,
		},
		{
			.name = "specific reserve overrides an existing generic reserved region",
			.input = {
				{ .base = 0x1000, .length = 0x2000, .type = PLANE_MEM_RESERVED },
			},
			.input_count = 1,
			.reserve_base = 0x1000,
			.reserve_length = 0x2000,
			.reserve_type = PLANE_MEM_FRAMEBUFFER,
			.expected_ret = 1,
			.expected = {
				{ .base = 0x1000, .length = 0x2000, .type = PLANE_MEM_FRAMEBUFFER },
			},
			.expected_count = 1,
		},
		{
			.name = "executable reserve splits an existing generic reserved region",
			.input = {
				{ .base = 0x1000, .length = 0x4000, .type = PLANE_MEM_RESERVED },
			},
			.input_count = 1,
			.reserve_base = 0x2000,
			.reserve_length = 0x1000,
			.reserve_type = PLANE_MEM_EXECUTABLE_AND_MODULES,
			.expected_ret = 1,
			.expected = {
				{ .base = 0x1000, .length = 0x1000, .type = PLANE_MEM_RESERVED },
				{ .base = 0x2000, .length = 0x1000, .type = PLANE_MEM_EXECUTABLE_AND_MODULES },
				{ .base = 0x3000, .length = 0x2000, .type = PLANE_MEM_RESERVED },
			},
			.expected_count = 3,
		},
		{
			.name = "bootloader reserve splits an existing generic reserved region",
			.input = {
				{ .base = 0x1000, .length = 0x4000, .type = PLANE_MEM_RESERVED },
			},
			.input_count = 1,
			.reserve_base = 0x2000,
			.reserve_length = 0x1000,
			.reserve_type = PLANE_MEM_BOOTLOADER_RECLAIMABLE,
			.expected_ret = 1,
			.expected = {
				{ .base = 0x1000, .length = 0x1000, .type = PLANE_MEM_RESERVED },
				{ .base = 0x2000, .length = 0x1000, .type = PLANE_MEM_BOOTLOADER_RECLAIMABLE },
				{ .base = 0x3000, .length = 0x2000, .type = PLANE_MEM_RESERVED },
			},
			.expected_count = 3,
		},
		{
			.name = "bootloader reserve preserves handoff data inside executable region",
			.input = {
				{ .base = 0x1000, .length = 0x4000, .type = PLANE_MEM_EXECUTABLE_AND_MODULES },
			},
			.input_count = 1,
			.reserve_base = 0x2000,
			.reserve_length = 0x1000,
			.reserve_type = PLANE_MEM_BOOTLOADER_RECLAIMABLE,
			.expected_ret = 1,
			.expected = {
				{ .base = 0x1000, .length = 0x1000, .type = PLANE_MEM_EXECUTABLE_AND_MODULES },
				{ .base = 0x2000, .length = 0x1000, .type = PLANE_MEM_BOOTLOADER_RECLAIMABLE },
				{ .base = 0x3000, .length = 0x2000, .type = PLANE_MEM_EXECUTABLE_AND_MODULES },
			},
			.expected_count = 3,
		},
		{
			.name = "zero-length reserve is a no-op",
			.input = {
				{ .base = 0x1000, .length = 0x2000, .type = PLANE_MEM_USABLE },
			},
			.input_count = 1,
			.reserve_base = 0x1800,
			.reserve_length = 0,
			.reserve_type = PLANE_MEM_RESERVED,
			.expected_ret = 1,
			.expected = {
				{ .base = 0x1000, .length = 0x2000, .type = PLANE_MEM_USABLE },
			},
			.expected_count = 1,
		},
		{
			.name = "page-internal reserve expands to a full page",
			.input = {
				{ .base = 0x1000, .length = 0x3000, .type = PLANE_MEM_USABLE },
			},
			.input_count = 1,
			.reserve_base = 0x1800,
			.reserve_length = 0x20,
			.reserve_type = PLANE_MEM_RESERVED,
			.expected_ret = 1,
			.expected = {
				{ .base = 0x1000, .length = 0x1000, .type = PLANE_MEM_RESERVED },
				{ .base = 0x2000, .length = 0x2000, .type = PLANE_MEM_USABLE },
			},
			.expected_count = 2,
		},
	};

	int failures = 0;
	int sanitize_count = (int)(sizeof(cases) / sizeof(cases[0]));
	int reserve_count = (int)(sizeof(reserve_cases) / sizeof(reserve_cases[0]));

	for (int i = 0; i < sanitize_count; i++) {
		failures += run_case(&cases[i]);
	}
	for (int i = 0; i < reserve_count; i++) {
		failures += run_reserve_case(&reserve_cases[i]);
	}
	TEST_RUN(failures, run_full_map_reserve_failure_case);
	TEST_RUN(failures, run_sanitize_too_many_regions_failure_case);
	TEST_RUN(failures, run_sanitize_overflow_failure_case);
	TEST_RUN(failures, run_reserve_overflow_failure_case);

	if (failures != 0) {
		return 1;
	}

	printf("boot_mem_test: ok\n");
	return 0;
}
