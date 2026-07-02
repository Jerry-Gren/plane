#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <plane/memmap.h>
#include <plane/mm.h>
#include <plane/pmm.h>

static bool direct_map_available = true;
static uint8_t direct_map_storage[64 * 1024] __attribute__((aligned(PAGE_SIZE)));

void *hal_mmu_direct_phys_to_virt(uint64_t phys_addr)
{
	(void)phys_addr;

	if (!direct_map_available) {
		return NULL;
	}

	return direct_map_storage;
}

static void reset_direct_map_stub(void)
{
	direct_map_available = true;
	memset(direct_map_storage, 0, sizeof(direct_map_storage));
}

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

static int expect_ptr_null(const char *name, const void *actual)
{
	if (actual == NULL) {
		return 0;
	}

	printf("FAIL: %s expected=NULL actual=%p\n", name, actual);
	return 1;
}

static int expect_ptr_not_null(const char *name, const void *actual)
{
	if (actual != NULL) {
		return 0;
	}

	printf("FAIL: %s expected=non-NULL actual=NULL\n", name);
	return 1;
}

static int expect_page_state(const char *name,
			     enum plane_page_state actual,
			     enum plane_page_state expected)
{
	if (actual == expected) {
		return 0;
	}

	printf("FAIL: %s expected=%d actual=%d\n", name, expected, actual);
	return 1;
}

static uint64_t pages_for_bytes(uint64_t bytes)
{
	return (bytes + PAGE_SIZE - 1) / PAGE_SIZE;
}

static int expect_metadata_stats(const char *prefix,
				 const struct plane_pmm_stats *stats,
				 uint64_t managed)
{
	int failures = 0;
	char name[96];

	if (managed == 0) {
		snprintf(name, sizeof(name), "%s metadata_pages", prefix);
		failures += expect_u64(name, stats->allocator.metadata_pages, 0);
		snprintf(name, sizeof(name), "%s metadata_bytes", prefix);
		failures += expect_u64(name, stats->allocator.metadata_bytes, 0);
		return failures;
	}

	snprintf(name, sizeof(name), "%s metadata_bytes nonzero", prefix);
	failures += expect_bool(name, stats->allocator.metadata_bytes != 0, true);
	snprintf(name, sizeof(name), "%s metadata_pages", prefix);
	failures += expect_u64(name, stats->allocator.metadata_pages,
			       pages_for_bytes(stats->allocator.metadata_bytes));
	snprintf(name, sizeof(name), "%s metadata_pages managed", prefix);
	failures += expect_bool(name, stats->allocator.metadata_pages <= managed, true);
	return failures;
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

#define EXPECT_ALLOCATOR_FIELD(field, expected) do {                            \
	snprintf(name, sizeof(name), "%s allocator.%s", prefix, #field);       \
	failures += expect_u64(name, stats->allocator.field, expected);         \
} while (0)

#define EXPECT_MEMTYPE_FIELD(field, expected) do {                              \
	snprintf(name, sizeof(name), "%s memtype.%s", prefix, #field);         \
	failures += expect_u64(name, stats->memtype.field, expected);           \
} while (0)

	EXPECT_ALLOCATOR_FIELD(managed_pages, managed);
	EXPECT_ALLOCATOR_FIELD(free_pages, free);
	EXPECT_ALLOCATOR_FIELD(free_range_count, free_ranges);
	EXPECT_MEMTYPE_FIELD(usable_pages, usable);
	EXPECT_MEMTYPE_FIELD(invalid_pages, invalid);
	EXPECT_MEMTYPE_FIELD(reserved_pages, reserved);
	EXPECT_MEMTYPE_FIELD(acpi_reclaimable_pages, acpi_reclaimable);
	EXPECT_MEMTYPE_FIELD(acpi_nvs_pages, acpi_nvs);
	EXPECT_MEMTYPE_FIELD(bootloader_reclaimable_pages, bootloader);
	EXPECT_MEMTYPE_FIELD(executable_and_modules_pages, exec_modules);
	EXPECT_MEMTYPE_FIELD(framebuffer_pages, framebuffer);
	EXPECT_MEMTYPE_FIELD(bad_pages, bad);
	EXPECT_MEMTYPE_FIELD(reserved_mapped_pages, reserved_mapped);
	failures += expect_metadata_stats(prefix, stats, managed);

#undef EXPECT_ALLOCATOR_FIELD
#undef EXPECT_MEMTYPE_FIELD
	return failures;
}

static int test_phys_to_page_metadata(void)
{
	struct plane_mem_info mem = {0};
	struct plane_page *page;
	int failures = 0;

	add_region(&mem, 0x1000, 0x2000, PLANE_MEM_USABLE);
	add_region(&mem, 0x8000, 0x1000, PLANE_MEM_USABLE);
	failures += expect_bool("metadata init", plane_pmm_init(&mem), true);

	page = plane_pmm_phys_to_page(0x1000);
	failures += expect_ptr_not_null("metadata page 0x1000", page);
	failures += expect_u64("metadata phys 0x1000",
			       plane_page_phys(page), 0x1000);
	failures += expect_page_state("metadata state",
				      plane_page_state(page),
				      PLANE_PAGE_METADATA);

	page = plane_pmm_phys_to_page(0x8000);
	failures += expect_ptr_not_null("metadata non-contig page", page);
	failures += expect_u64("metadata non-contig phys",
			       plane_page_phys(page), 0x8000);
	failures += expect_page_state("metadata non-contig state",
				      plane_page_state(page),
				      PLANE_PAGE_FREE);

	failures += expect_ptr_null("metadata reject unaligned",
				    plane_pmm_phys_to_page(0x1001));
	failures += expect_ptr_null("metadata reject unmanaged",
				    plane_pmm_phys_to_page(0x4000));
	failures += expect_page_state("metadata null state",
				      plane_page_state(NULL),
				      PLANE_PAGE_INVALID);
	failures += expect_u64("metadata null phys",
			       plane_page_phys(NULL), UINT64_MAX);

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
				 3, 2, 3, 2, 1, 1, 1, 1, 2, 2, 1, 1, 1);

	return failures;
}

static int test_page_api_allocates_and_frees_metadata(void)
{
	struct plane_mem_info mem = {0};
	struct plane_pmm_stats stats;
	struct plane_page *page;
	struct plane_page *bad_page = (struct plane_page *)(uintptr_t)&mem;
	int failures = 0;

	add_region(&mem, 0x1000, 0x3000, PLANE_MEM_USABLE);
	failures += expect_bool("page api init", plane_pmm_init(&mem), true);

	failures += expect_bool("reject null page output",
				plane_pmm_alloc_page(NULL), false);
	failures += expect_bool("alloc page api page",
				plane_pmm_alloc_page(&page), true);
	failures += expect_ptr_not_null("allocated page api page", page);
	failures += expect_u64("allocated page api phys",
			       plane_page_phys(page), 0x2000);
	failures += expect_page_state("allocated page api state",
				      plane_page_state(page),
				      PLANE_PAGE_ALLOCATED);

	stats = plane_pmm_get_stats();
	failures += expect_stats("page api allocated", &stats,
				 3, 1, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1);

	failures += expect_bool("reject null metadata free",
				plane_pmm_free_page(NULL), false);
	failures += expect_bool("reject foreign metadata free",
				plane_pmm_free_page(bad_page), false);
	failures += expect_bool("free page api page",
				plane_pmm_free_page(page), true);
	failures += expect_page_state("freed page api state",
				      plane_page_state(page),
				      PLANE_PAGE_FREE);
	failures += expect_bool("reject page api double free",
				plane_pmm_free_page(page), false);

	stats = plane_pmm_get_stats();
	failures += expect_stats("page api freed", &stats,
				 3, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1);

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
				 5, 4, 5, 0, 0, 0, 0, 1, 2, 1, 0, 0, 3);

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
				 5, 4, 5, 0, 0, 0, 0, 1, 2, 1, 0, 0, 3);

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
	failures += expect_u64("first page address", phys, 0x2000);
	failures += expect_bool("alloc second page",
				plane_pmm_alloc_page_phys(&phys), true);
	failures += expect_u64("second page address", phys, 0x3000);
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
	failures += expect_page_state("aligned allocation page state",
				      plane_page_state(plane_pmm_phys_to_page(phys)),
				      PLANE_PAGE_ALLOCATED);

	stats = plane_pmm_get_stats();
	failures += expect_stats("aligned allocation", &stats,
				 8, 6, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2);

	return failures;
}

static int test_multi_page_phys_api_updates_metadata(void)
{
	struct plane_mem_info mem = {0};
	struct plane_pmm_stats stats;
	uint64_t phys;
	int failures = 0;

	add_region(&mem, 0x1000, 0x6000, PLANE_MEM_USABLE);
	failures += expect_bool("multi metadata init",
				plane_pmm_init(&mem), true);

	failures += expect_bool("multi metadata alloc",
				plane_pmm_alloc_pages_phys(3, 1, &phys), true);
	failures += expect_u64("multi metadata alloc addr", phys, 0x2000);
	for (uint64_t i = 0; i < 3; i++) {
		failures += expect_page_state("multi metadata allocated",
					      plane_page_state(
						      plane_pmm_phys_to_page(
							      phys + i * PAGE_SIZE)),
					      PLANE_PAGE_ALLOCATED);
	}

	stats = plane_pmm_get_stats();
	failures += expect_stats("multi metadata allocated", &stats,
				 6, 2, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1);

	failures += expect_bool("multi metadata free",
				plane_pmm_free_pages_phys(phys, 3), true);
	for (uint64_t i = 0; i < 3; i++) {
		failures += expect_page_state("multi metadata free",
					      plane_page_state(
						      plane_pmm_phys_to_page(
							      phys + i * PAGE_SIZE)),
					      PLANE_PAGE_FREE);
	}

	stats = plane_pmm_get_stats();
	failures += expect_stats("multi metadata freed", &stats,
				 6, 5, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1);

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
	failures += expect_u64("alloc page 1 addr", phys, 0x2000);
	failures += expect_bool("alloc page 2",
				plane_pmm_alloc_page_phys(&phys), true);
	failures += expect_u64("alloc page 2 addr", phys, 0x3000);
	failures += expect_bool("alloc page 3",
				plane_pmm_alloc_page_phys(&phys), true);
	failures += expect_u64("alloc page 3 addr", phys, 0x4000);

	failures += expect_bool("free middle page",
				plane_pmm_free_page_phys(0x3000), true);
	stats = plane_pmm_get_stats();
	failures += expect_stats("free middle", &stats,
				 5, 2, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2);

	failures += expect_bool("free previous page",
				plane_pmm_free_page_phys(0x2000), true);
	stats = plane_pmm_get_stats();
	failures += expect_stats("free previous", &stats,
				 5, 3, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2);

	failures += expect_bool("free bridge page",
				plane_pmm_free_page_phys(0x4000), true);
	stats = plane_pmm_get_stats();
	failures += expect_stats("free bridge", &stats,
				 5, 4, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1);

	return failures;
}

static int test_free_rejects_invalid_ranges(void)
{
	struct plane_mem_info mem = {0};
	uint64_t phys;
	int failures = 0;

	add_region(&mem, 0x1000, 0x2000, PLANE_MEM_USABLE);
	failures += expect_bool("free reject init", plane_pmm_init(&mem), true);
	failures += expect_bool("reject metadata free",
				plane_pmm_free_page_phys(0x1000), false);
	failures += expect_bool("allocate first for reject tests",
				plane_pmm_alloc_page_phys(&phys), true);
	failures += expect_u64("allocated reject test page", phys, 0x2000);
	failures += expect_bool("reject unaligned free",
				plane_pmm_free_page_phys(0x2001), false);
	failures += expect_bool("reject unmanaged free",
				plane_pmm_free_page_phys(0x9000), false);
	failures += expect_bool("free allocated page",
				plane_pmm_free_page_phys(0x2000), true);
	failures += expect_bool("reject double-free after release",
				plane_pmm_free_page_phys(0x2000), false);

	return failures;
}

static int test_init_fails_without_direct_map(void)
{
	struct plane_mem_info mem = {0};
	int failures = 0;

	add_region(&mem, 0x1000, 0x3000, PLANE_MEM_USABLE);
	direct_map_available = false;
	failures += expect_bool("direct map missing init",
				plane_pmm_init(&mem), false);
	direct_map_available = true;

	return failures;
}

int main(void)
{
	int failures = 0;

#define RUN_TEST(fn) do {                                                       \
	reset_direct_map_stub();                                               \
	failures += fn();                                                      \
} while (0)

	RUN_TEST(test_init_accounts_all_memmap_types);
	RUN_TEST(test_phys_to_page_metadata);
	RUN_TEST(test_page_api_allocates_and_frees_metadata);
	RUN_TEST(test_grub_like_reservations_are_counted);
	RUN_TEST(test_limine_like_rich_memmap_is_counted);
	RUN_TEST(test_single_page_allocation_order_and_exhaustion);
	RUN_TEST(test_multi_page_alignment);
	RUN_TEST(test_multi_page_phys_api_updates_metadata);
	RUN_TEST(test_free_merges_ranges);
	RUN_TEST(test_free_rejects_invalid_ranges);
	RUN_TEST(test_init_fails_without_direct_map);

#undef RUN_TEST

	if (failures != 0) {
		return 1;
	}

	printf("pmm_test: ok\n");
	return 0;
}
