#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <plane/memmap.h>
#include <plane/mm.h>
#include <plane/pmm.h>

static int expect_bool(const char *name, bool actual, bool expected)
{
	if (actual == expected) {
		return 0;
	}

	printf("FAIL: %s expected=%d actual=%d\n", name, expected, actual);
	return 1;
}

static int expect_u64(const char *name, uint64_t actual, uint64_t expected)
{
	if (actual == expected) {
		return 0;
	}

	printf("FAIL: %s expected=0x%016llx actual=0x%016llx\n",
	       name, (unsigned long long)expected, (unsigned long long)actual);
	return 1;
}

static void add_region(struct plane_mem_info *mem, uint64_t base,
		       uint64_t length, uint32_t type)
{
	uint64_t index = mem->entry_count++;

	mem->map[index].base = base;
	mem->map[index].length = length;
	mem->map[index].type = type;
}

static int expect_stats(const char *prefix,
			const struct plane_pmm_stats *stats,
			uint64_t managed,
			uint64_t free,
			uint64_t usable,
			uint64_t invalid,
			uint64_t reserved,
			uint64_t acpi_reclaimable,
			uint64_t acpi_nvs,
			uint64_t bootloader,
			uint64_t exec_modules,
			uint64_t framebuffer,
			uint64_t bad,
			uint64_t reserved_mapped,
			uint64_t free_ranges)
{
	int failures = 0;
	char name[96];

#define EXPECT_FIELD(field, expected) do {                                      \
	snprintf(name, sizeof(name), "%s %s", prefix, #field);                 \
	failures += expect_u64(name, stats->field, expected);                   \
} while (0)

	EXPECT_FIELD(managed_pages, managed);
	EXPECT_FIELD(free_pages, free);
	EXPECT_FIELD(allocated_pages, managed - free);
	EXPECT_FIELD(usable_pages, usable);
	EXPECT_FIELD(invalid_pages, invalid);
	EXPECT_FIELD(reserved_pages, reserved);
	EXPECT_FIELD(acpi_reclaimable_pages, acpi_reclaimable);
	EXPECT_FIELD(acpi_nvs_pages, acpi_nvs);
	EXPECT_FIELD(bootloader_reclaimable_pages, bootloader);
	EXPECT_FIELD(executable_and_modules_pages, exec_modules);
	EXPECT_FIELD(framebuffer_pages, framebuffer);
	EXPECT_FIELD(bad_pages, bad);
	EXPECT_FIELD(reserved_mapped_pages, reserved_mapped);
	EXPECT_FIELD(free_range_count, free_ranges);

#undef EXPECT_FIELD
	return failures;
}

static int test_init_accounts_all_memmap_types(void)
{
	struct plane_mem_info mem = {0};
	struct plane_pmm_stats stats;
	int failures = 0;

	add_region(&mem, 0x1000, 0x3000, PLANE_MEM_USABLE);
	add_region(&mem, 0x5003, 0x0001, PLANE_MEM_RESERVED);
	add_region(&mem, 0x6000, 0x1000, PLANE_MEM_BOOTLOADER_RECLAIMABLE);
	add_region(&mem, 0x7000, 0x2000, PLANE_MEM_EXECUTABLE_AND_MODULES);
	add_region(&mem, 0x9001, 0x1fff, PLANE_MEM_FRAMEBUFFER);
	add_region(&mem, 0xb000, 0x1000, PLANE_MEM_BAD_MEMORY);
	add_region(&mem, 0xc100, 0x0100, PLANE_MEM_ACPI_RECLAIMABLE);
	add_region(&mem, 0xd000, 0x1000, PLANE_MEM_ACPI_NVS);
	add_region(&mem, 0xe000, 0x1000, PLANE_MEM_RESERVED_MAPPED);
	add_region(&mem, 0xf000, 0x1000, PLANE_MEM_INVALID);
	add_region(&mem, 0x10000, 0x1000, 0xffffffffu);

	failures += expect_bool("pmm init all types",
				plane_pmm_init(&mem), true);
	stats = plane_pmm_get_stats();
	failures += expect_stats("all types", &stats,
				 3, 3, 3, 2, 1, 1, 1, 1, 2, 2, 1, 1, 1);

	return failures;
}

static int test_grub_like_reservations_are_counted(void)
{
	struct plane_mem_info mem = {0};
	struct plane_pmm_stats stats;
	int failures = 0;

	add_region(&mem, 0x1000, 0x9000, PLANE_MEM_USABLE);
	failures += expect_bool("grub reserve boot info",
				plane_memmap_reserve(&mem, 0x2000, 0x1000,
						     PLANE_MEM_BOOTLOADER_RECLAIMABLE),
				true);
	failures += expect_bool("grub reserve kernel image",
				plane_memmap_reserve(&mem, 0x4000, 0x2000,
						     PLANE_MEM_EXECUTABLE_AND_MODULES),
				true);
	failures += expect_bool("grub reserve framebuffer",
				plane_memmap_reserve(&mem, 0x7000, 0x1000,
						     PLANE_MEM_FRAMEBUFFER),
				true);

	failures += expect_bool("grub-like pmm init",
				plane_pmm_init(&mem), true);
	stats = plane_pmm_get_stats();
	failures += expect_stats("grub-like", &stats,
				 5, 5, 5, 0, 0, 0, 0, 1, 2, 1, 0, 0, 4);

	return failures;
}

static int test_limine_like_rich_memmap_is_counted(void)
{
	struct plane_mem_info mem = {0};
	struct plane_pmm_stats stats;
	int failures = 0;

	add_region(&mem, 0x1000, 0x1000, PLANE_MEM_USABLE);
	add_region(&mem, 0x2000, 0x1000, PLANE_MEM_BOOTLOADER_RECLAIMABLE);
	add_region(&mem, 0x3000, 0x1000, PLANE_MEM_USABLE);
	add_region(&mem, 0x4000, 0x2000, PLANE_MEM_EXECUTABLE_AND_MODULES);
	add_region(&mem, 0x6000, 0x1000, PLANE_MEM_USABLE);
	add_region(&mem, 0x7000, 0x1000, PLANE_MEM_FRAMEBUFFER);
	add_region(&mem, 0x8000, 0x2000, PLANE_MEM_USABLE);

	failures += expect_bool("limine-like sanitize",
				plane_sanitize_memory_map(&mem), true);
	failures += expect_bool("limine-like pmm init",
				plane_pmm_init(&mem), true);
	stats = plane_pmm_get_stats();
	failures += expect_stats("limine-like", &stats,
				 5, 5, 5, 0, 0, 0, 0, 1, 2, 1, 0, 0, 4);

	return failures;
}

static int test_single_page_allocation_order_and_exhaustion(void)
{
	struct plane_mem_info mem = {0};
	struct plane_pmm_stats stats;
	uint64_t phys;
	int failures = 0;

	add_region(&mem, 0x1000, 0x3000, PLANE_MEM_USABLE);
	failures += expect_bool("alloc order init", plane_pmm_init(&mem), true);

	failures += expect_bool("alloc first page",
				plane_pmm_alloc_page_phys(&phys), true);
	failures += expect_u64("first page address", phys, 0x1000);
	failures += expect_bool("alloc second page",
				plane_pmm_alloc_page_phys(&phys), true);
	failures += expect_u64("second page address", phys, 0x2000);
	failures += expect_bool("alloc third page",
				plane_pmm_alloc_page_phys(&phys), true);
	failures += expect_u64("third page address", phys, 0x3000);
	failures += expect_bool("alloc exhausted",
				plane_pmm_alloc_page_phys(&phys), false);

	stats = plane_pmm_get_stats();
	failures += expect_stats("alloc exhausted", &stats,
				 3, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

	return failures;
}

static int test_multi_page_alignment(void)
{
	struct plane_mem_info mem = {0};
	struct plane_pmm_stats stats;
	uint64_t phys;
	int failures = 0;

	add_region(&mem, 0x1000, 0x8000, PLANE_MEM_USABLE);
	failures += expect_bool("alignment init", plane_pmm_init(&mem), true);
	failures += expect_bool("reject null output",
				plane_pmm_alloc_pages_phys(1, 1, NULL), false);
	failures += expect_bool("reject zero page count",
				plane_pmm_alloc_pages_phys(0, 1, &phys), false);
	failures += expect_bool("reject zero alignment",
				plane_pmm_alloc_pages_phys(1, 0, &phys), false);
	failures += expect_bool("reject non-power-of-two alignment",
				plane_pmm_alloc_pages_phys(1, 3, &phys), false);

	failures += expect_bool("aligned allocation",
				plane_pmm_alloc_pages_phys(1, 4, &phys), true);
	failures += expect_u64("aligned allocation address", phys, 0x4000);

	stats = plane_pmm_get_stats();
	failures += expect_stats("aligned allocation", &stats,
				 8, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2);

	return failures;
}

static int test_free_merges_ranges(void)
{
	struct plane_mem_info mem = {0};
	struct plane_pmm_stats stats;
	uint64_t phys;
	int failures = 0;

	add_region(&mem, 0x1000, 0x5000, PLANE_MEM_USABLE);
	failures += expect_bool("free merge init", plane_pmm_init(&mem), true);
	failures += expect_bool("alloc page 1",
				plane_pmm_alloc_page_phys(&phys), true);
	failures += expect_u64("alloc page 1 addr", phys, 0x1000);
	failures += expect_bool("alloc page 2",
				plane_pmm_alloc_page_phys(&phys), true);
	failures += expect_u64("alloc page 2 addr", phys, 0x2000);
	failures += expect_bool("alloc page 3",
				plane_pmm_alloc_page_phys(&phys), true);
	failures += expect_u64("alloc page 3 addr", phys, 0x3000);

	failures += expect_bool("free middle page",
				plane_pmm_free_page_phys(0x2000), true);
	stats = plane_pmm_get_stats();
	failures += expect_stats("free middle", &stats,
				 5, 3, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2);

	failures += expect_bool("free previous page",
				plane_pmm_free_page_phys(0x1000), true);
	stats = plane_pmm_get_stats();
	failures += expect_stats("free previous", &stats,
				 5, 4, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2);

	failures += expect_bool("free bridge page",
				plane_pmm_free_page_phys(0x3000), true);
	stats = plane_pmm_get_stats();
	failures += expect_stats("free bridge", &stats,
				 5, 5, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1);

	return failures;
}

static int test_free_rejects_invalid_ranges(void)
{
	struct plane_mem_info mem = {0};
	uint64_t phys;
	int failures = 0;

	add_region(&mem, 0x1000, 0x2000, PLANE_MEM_USABLE);
	failures += expect_bool("free reject init", plane_pmm_init(&mem), true);
	failures += expect_bool("reject double-free before allocation",
				plane_pmm_free_page_phys(0x1000), false);
	failures += expect_bool("allocate first for reject tests",
				plane_pmm_alloc_page_phys(&phys), true);
	failures += expect_u64("allocated reject test page", phys, 0x1000);
	failures += expect_bool("reject unaligned free",
				plane_pmm_free_page_phys(0x1001), false);
	failures += expect_bool("reject unmanaged free",
				plane_pmm_free_page_phys(0x9000), false);
	failures += expect_bool("reject still-free page",
				plane_pmm_free_page_phys(0x2000), false);
	failures += expect_bool("free allocated page",
				plane_pmm_free_page_phys(0x1000), true);
	failures += expect_bool("reject double-free after release",
				plane_pmm_free_page_phys(0x1000), false);

	return failures;
}

int main(void)
{
	int failures = 0;

	failures += test_init_accounts_all_memmap_types();
	failures += test_grub_like_reservations_are_counted();
	failures += test_limine_like_rich_memmap_is_counted();
	failures += test_single_page_allocation_order_and_exhaustion();
	failures += test_multi_page_alignment();
	failures += test_free_merges_ranges();
	failures += test_free_rejects_invalid_ranges();

	if (failures != 0) {
		return 1;
	}

	printf("pmm_test: ok\n");
	return 0;
}
