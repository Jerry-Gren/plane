#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <plane/memmap.h>

struct test_case {
	const char *name;
	struct plane_mem_region input[PLANE_MAX_MEMMAP_ENTRIES];
	uint64_t input_count;
	struct plane_mem_region expected[PLANE_MAX_MEMMAP_ENTRIES];
	uint64_t expected_count;
};

static const char *mem_type_name(uint32_t type) {
	switch (type) {
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

	plane_sanitize_memory_map(&actual);
	if (maps_equal(&actual, &expected)) {
		return 1;
	}

	printf("FAIL: %s\n", tc->name);
	dump_map("expected", &expected);
	dump_map("actual", &actual);
	return 0;
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
			.name = "different high-priority memory types degrade to reserved",
			.input = {
				{ .base = 0x1000, .length = 0x3000, .type = PLANE_MEM_ACPI_NVS },
				{ .base = 0x2000, .length = 0x3000, .type = PLANE_MEM_BAD_MEMORY },
			},
			.input_count = 2,
			.expected = {
				{ .base = 0x1000, .length = 0x4000, .type = PLANE_MEM_RESERVED },
			},
			.expected_count = 1,
		},
	};

	int passed = 0;
	int total = (int)(sizeof(cases) / sizeof(cases[0]));

	for (int i = 0; i < total; i++) {
		passed += run_case(&cases[i]);
	}

	if (passed != total) {
		printf("boot_mem_test: %d/%d passed\n", passed, total);
		return 1;
	}

	printf("boot_mem_test: %d cases passed\n", total);
	return 0;
}
