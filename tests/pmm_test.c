#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <plane/compiler.h>
#include <plane/memmap.h>
#include <plane/mm.h>
#include <plane/pmm.h>
#include <plane/vm_page.h>

#include "support/test.h"

static bool physmap_available = true;
#define PHYSMAP_STORAGE_SIZE (1024 * 1024)
static uint64_t physmap_limit = PHYSMAP_STORAGE_SIZE;
static uint8_t physmap_storage[PHYSMAP_STORAGE_SIZE]
	__aligned(PAGE_SIZE);

void test_spinlock_stub_reset_counts(void);
uint64_t test_spinlock_stub_irqsave_depth(void);
uint64_t test_spinlock_stub_irqsave_max_depth(void);

static plane_paddr_t test_paddr(uint64_t raw)
{
	return plane_paddr_make(raw);
}

static uint64_t test_paddr_raw(plane_paddr_t addr)
{
	return plane_paddr_raw(addr);
}

plane_vaddr_t hal_mmu_physmap_phys_range_to_virt(plane_paddr_t phys_addr,
						uint64_t size)
{
	uint64_t raw = plane_paddr_raw(phys_addr);

	if (!physmap_available || size == 0 ||
	    raw > physmap_limit ||
	    size > physmap_limit - raw ||
	    raw > PHYSMAP_STORAGE_SIZE ||
	    size > PHYSMAP_STORAGE_SIZE - raw) {
		return plane_vaddr_make(0);
	}

	return plane_vaddr_from_ptr(&physmap_storage[raw]);
}

plane_vaddr_t hal_mmu_physmap_phys_to_virt(plane_paddr_t phys_addr)
{
	return hal_mmu_physmap_phys_range_to_virt(phys_addr, 1);
}

static void reset_physmap_stub(void)
{
	physmap_available = true;
	physmap_limit = PHYSMAP_STORAGE_SIZE;
	memset(physmap_storage, 0, sizeof(physmap_storage));
	test_spinlock_stub_reset_counts();
}

static int check_spinlock_depth(const char *prefix,
				uint64_t max_depth,
				uint64_t depth)
{
	int failures = 0;
	char name[96];

	snprintf(name, sizeof(name), "%s max depth", prefix);
	failures += test_expect_u64(name,
				    test_spinlock_stub_irqsave_max_depth(),
				    max_depth);
	snprintf(name, sizeof(name), "%s depth", prefix);
	failures += test_expect_u64(name,
				    test_spinlock_stub_irqsave_depth(),
				    depth);
	return failures;
}

static int check_page_state(const char *name,
			     enum plane_vm_page_state actual,
			     enum plane_vm_page_state expected)
{
	if (actual == expected) {
		return 0;
	}

	test_fail("%s expected=%d actual=%d", name, expected, actual);
	return 1;
}

static int check_phys_bytes(const char *name,
			    uint64_t phys_addr,
			    uint8_t expected,
			    uint64_t length)
{
	if (phys_addr > PHYSMAP_STORAGE_SIZE ||
	    length > PHYSMAP_STORAGE_SIZE - phys_addr) {
		test_fail("%s out of physmap test storage", name);
		return 1;
	}

	for (uint64_t i = 0; i < length; i++) {
		if (physmap_storage[phys_addr + i] != expected) {
			test_fail("%s offset=%llu expected=%u actual=%u",
				  name,
				  (unsigned long long)i,
				  expected,
				  physmap_storage[phys_addr + i]);
			return 1;
		}
	}

	return 0;
}

static uint64_t pages_for_bytes(uint64_t bytes)
{
	return (bytes + PAGE_SIZE - 1) / PAGE_SIZE;
}

static int check_metadata_stats(const char *prefix,
				 const struct plane_pmm_stats *stats,
				 uint64_t managed)
{
	int failures = 0;
	char name[96];

	if (managed == 0) {
		snprintf(name, sizeof(name), "%s metadata_pages", prefix);
		failures += test_expect_u64(name, stats->allocator.metadata_pages, 0);
		snprintf(name, sizeof(name), "%s metadata_bytes", prefix);
		failures += test_expect_u64(name, stats->allocator.metadata_bytes, 0);
		return failures;
	}

	snprintf(name, sizeof(name), "%s metadata_bytes nonzero", prefix);
	failures += test_expect_bool(name,
				     stats->allocator.metadata_bytes != 0,
				     true);
	snprintf(name, sizeof(name), "%s metadata_pages", prefix);
	failures += test_expect_u64(name, stats->allocator.metadata_pages,
				    pages_for_bytes(stats->allocator.metadata_bytes));
	snprintf(name, sizeof(name), "%s metadata_pages managed", prefix);
	failures += test_expect_bool(name,
				     stats->allocator.metadata_pages <= managed,
				     true);
	return failures;
}

static void add_region(struct plane_mem_info *mem, uint64_t base,
		       uint64_t length, uint32_t type)
{
	uint64_t index = mem->entry_count++;

	mem->map[index].base = plane_paddr_make(base);
	mem->map[index].length = length;
	mem->map[index].type = type;
}

static int check_stats_wired(const char *prefix,
			     const struct plane_pmm_stats *stats,
			     uint64_t managed,
			     uint64_t free,
			     uint64_t wired,
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
			     uint64_t free_runs)
{
	int failures = 0;
	char name[96];

#define EXPECT_ALLOCATOR_FIELD(field, expected) do {                            \
	snprintf(name, sizeof(name), "%s allocator.%s", prefix, #field);       \
	failures += test_expect_u64(name, stats->allocator.field, expected);    \
} while (0)

#define EXPECT_MEMTYPE_FIELD(field, expected) do {                              \
	snprintf(name, sizeof(name), "%s memtype.%s", prefix, #field);         \
	failures += test_expect_u64(name, stats->memtype.field, expected);      \
} while (0)

	EXPECT_ALLOCATOR_FIELD(managed_pages, managed);
	EXPECT_ALLOCATOR_FIELD(free_pages, free);
	EXPECT_ALLOCATOR_FIELD(wired_pages, wired);
	EXPECT_ALLOCATOR_FIELD(free_run_count, free_runs);
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
	failures += check_metadata_stats(prefix, stats, managed);

#undef EXPECT_ALLOCATOR_FIELD
#undef EXPECT_MEMTYPE_FIELD
	return failures;
}

static int check_stats(const char *prefix,
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
		       uint64_t free_runs)
{
	return check_stats_wired(prefix, stats, managed, free, 0, usable,
				 invalid, reserved, acpi_reclaimable, acpi_nvs,
				 bootloader, exec_modules, framebuffer, bad,
				 reserved_mapped, free_runs);
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

	failures += test_expect_bool("pmm init all types",
				plane_pmm_init(&mem), true);
	stats = plane_pmm_get_stats();
	failures += check_stats("all types", &stats,
				 3, 2, 3, 2, 1, 1, 1, 1, 2, 2, 1, 1, 1);

	return failures;
}

static int test_usable_region_reserves_null_physical_page(void)
{
	struct plane_mem_info mem = {0};
	struct plane_pmm_stats stats;
	plane_paddr_t phys = {0};
	int failures = 0;

	add_region(&mem, 0, 0x5000, PLANE_MEM_USABLE);
	failures += test_expect_bool("null guard init",
				     plane_pmm_init(&mem), true);
	failures += test_expect_null("null phys unmanaged",
				     plane_vm_page_from_phys(test_paddr(0)));
	failures += test_expect_bool("null guard first alloc",
				     plane_pmm_alloc_page_phys(&phys), true);
	failures += test_expect_u64("null guard first alloc phys",
				    test_paddr_raw(phys), 0x2000);

	stats = plane_pmm_get_stats();
	failures += check_stats("null guard stats", &stats,
				 4, 2, 4, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1);

	return failures;
}

static int test_usable_region_inside_null_page_is_reserved(void)
{
	struct plane_mem_info mem = {0};
	struct plane_pmm_stats stats;
	plane_paddr_t phys = {0};
	int failures = 0;

	add_region(&mem, 0, PAGE_SIZE, PLANE_MEM_USABLE);
	failures += test_expect_bool("null-only init",
				     plane_pmm_init(&mem), true);
	failures += test_expect_null("null-only page unmanaged",
				     plane_vm_page_from_phys(test_paddr(0)));
	failures += test_expect_bool("null-only alloc rejected",
				     plane_pmm_alloc_page_phys(&phys), false);

	stats = plane_pmm_get_stats();
	failures += check_stats("null-only stats", &stats,
				 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0);

	return failures;
}

static int test_run_allocation_skips_null_physical_page(void)
{
	struct plane_mem_info mem = {0};
	plane_paddr_t phys = {0};
	int failures = 0;

	add_region(&mem, 0, 0x7000, PLANE_MEM_USABLE);
	failures += test_expect_bool("null run init",
				     plane_pmm_init(&mem), true);
	failures += test_expect_bool("null run alloc",
				     plane_pmm_alloc_pages_phys(2, 1, &phys),
				     true);
	failures += test_expect_u64("null run alloc phys",
				    test_paddr_raw(phys), 0x2000);

	return failures;
}

static int test_grub_like_reservations_are_counted(void)
{
	struct plane_mem_info mem = {0};
	struct plane_pmm_stats stats;
	int failures = 0;

	add_region(&mem, 0x1000, 0x9000, PLANE_MEM_USABLE);
	failures += test_expect_bool("grub reserve handoff info",
				plane_memmap_reserve(&mem, plane_paddr_make(0x2000), 0x1000,
						     PLANE_MEM_BOOTLOADER_RECLAIMABLE),
				true);
	failures += test_expect_bool("grub reserve kernel image",
				plane_memmap_reserve(&mem, plane_paddr_make(0x4000), 0x2000,
						     PLANE_MEM_EXECUTABLE_AND_MODULES),
				true);
	failures += test_expect_bool("grub reserve framebuffer",
				plane_memmap_reserve(&mem, plane_paddr_make(0x7000), 0x1000,
						     PLANE_MEM_FRAMEBUFFER),
				true);

	failures += test_expect_bool("grub-like pmm init",
				plane_pmm_init(&mem), true);
	stats = plane_pmm_get_stats();
	failures += check_stats("grub-like", &stats,
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

	failures += test_expect_bool("limine-like sanitize",
				plane_memmap_sanitize(&mem), true);
	failures += test_expect_bool("limine-like pmm init",
				plane_pmm_init(&mem), true);
	stats = plane_pmm_get_stats();
	failures += check_stats("limine-like", &stats,
				 5, 4, 5, 0, 0, 0, 0, 1, 2, 1, 0, 0, 3);

	return failures;
}

static int test_single_page_allocation_order_and_exhaustion(void)
{
	struct plane_mem_info mem = {0};
	struct plane_pmm_stats stats;
	plane_paddr_t phys;
	int failures = 0;

	add_region(&mem, 0x1000, 0x3000, PLANE_MEM_USABLE);
	failures += test_expect_bool("alloc order init", plane_pmm_init(&mem), true);

	failures += test_expect_bool("alloc first page",
				plane_pmm_alloc_page_phys(&phys), true);
	failures += test_expect_u64("first page address", test_paddr_raw(phys),
				    0x2000);
	failures += test_expect_bool("alloc second page",
				plane_pmm_alloc_page_phys(&phys), true);
	failures += test_expect_u64("second page address", test_paddr_raw(phys),
				    0x3000);
	failures += test_expect_bool("alloc exhausted",
				plane_pmm_alloc_page_phys(&phys), false);

	stats = plane_pmm_get_stats();
	failures += check_stats("alloc exhausted", &stats,
				 3, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

	return failures;
}

static int test_plain_allocation_does_not_zero_page(void)
{
	struct plane_mem_info mem = {0};
	plane_paddr_t phys;
	int failures = 0;

	add_region(&mem, 0x1000, 0x3000, PLANE_MEM_USABLE);
	failures += test_expect_bool("plain zero init", plane_pmm_init(&mem),
				     true);

	memset(&physmap_storage[0x2000], 0xa5, PAGE_SIZE);
	failures += test_expect_bool("plain alloc",
				plane_pmm_alloc_page_phys(&phys), true);
	failures += test_expect_u64("plain alloc phys", test_paddr_raw(phys),
				    0x2000);
	failures += check_phys_bytes("plain alloc keeps data",
				     test_paddr_raw(phys), 0xa5, PAGE_SIZE);

	return failures;
}

static int test_zeroed_multi_page_allocation(void)
{
	struct plane_mem_info mem = {0};
	plane_paddr_t phys;
	int failures = 0;

	add_region(&mem, 0x1000, 0x7000, PLANE_MEM_USABLE);
	failures += test_expect_bool("zero multi init", plane_pmm_init(&mem),
				     true);

	memset(&physmap_storage[0x2000], 0xa5, 3 * PAGE_SIZE);
	memset(&physmap_storage[0x5000], 0x5a, PAGE_SIZE);
	failures += test_expect_bool("zero multi alloc",
				plane_pmm_alloc_pages_phys_flags(
					3, 1, PLANE_PMM_ALLOC_ZERO, &phys),
				true);
	failures += test_expect_u64("zero multi phys", test_paddr_raw(phys),
				    0x2000);
	failures += check_phys_bytes("zero multi cleared", 0x2000, 0,
				      3 * PAGE_SIZE);
	failures += check_phys_bytes("zero multi guard", 0x5000, 0x5a,
				      PAGE_SIZE);

	return failures;
}

static int test_zeroed_allocation_rolls_back_without_physmap(void)
{
	struct plane_mem_info mem = {0};
	struct plane_pmm_stats stats;
	plane_paddr_t phys = {0};
	int failures = 0;

	add_region(&mem, 0x1000, 0x3000, PLANE_MEM_USABLE);
	failures += test_expect_bool("zero rollback init", plane_pmm_init(&mem),
				     true);

	physmap_available = false;
	failures += test_expect_bool("zero rollback alloc",
				plane_pmm_alloc_pages_phys_flags(
					1, 1, PLANE_PMM_ALLOC_ZERO, &phys),
				false);
	physmap_available = true;

	stats = plane_pmm_get_stats();
	failures += check_stats("zero rollback stats", &stats,
				 3, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1);
	failures += check_page_state("zero rollback page state",
				      plane_vm_page_state(
					      plane_vm_page_from_phys(
						      test_paddr(0x2000))),
				      PLANE_VM_PAGE_FREE);
	failures += test_expect_bool("zero rollback reuses page",
				plane_pmm_alloc_page_phys(&phys), true);
	failures += test_expect_u64("zero rollback reused phys",
				    test_paddr_raw(phys), 0x2000);

	return failures;
}

static int test_zeroed_allocation_rolls_back_without_range_coverage(void)
{
	struct plane_mem_info mem = {0};
	struct plane_pmm_stats stats;
	plane_paddr_t phys = {0};
	int failures = 0;

	add_region(&mem, 0x1000, 0x3000, PLANE_MEM_USABLE);
	failures += test_expect_bool("zero range rollback init",
				     plane_pmm_init(&mem), true);

	physmap_limit = 0x2800;
	failures += test_expect_bool("zero range rollback alloc",
				plane_pmm_alloc_pages_phys_flags(
					1, 1, PLANE_PMM_ALLOC_ZERO, &phys),
				false);
	physmap_limit = PHYSMAP_STORAGE_SIZE;

	stats = plane_pmm_get_stats();
	failures += check_stats("zero range rollback stats", &stats,
				 3, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1);
	failures += check_page_state("zero range rollback page state",
				      plane_vm_page_state(
					      plane_vm_page_from_phys(
						      test_paddr(0x2000))),
				      PLANE_VM_PAGE_FREE);
	failures += test_expect_bool("zero range rollback reuses page",
				plane_pmm_alloc_page_phys(&phys), true);
	failures += test_expect_u64("zero range rollback reused phys", test_paddr_raw(phys),
				    0x2000);

	return failures;
}

static int test_allocation_flags_reject_unknown_bits(void)
{
	struct plane_mem_info mem = {0};
	struct plane_pmm_stats stats;
	plane_paddr_t phys;
	int failures = 0;

	add_region(&mem, 0x1000, 0x3000, PLANE_MEM_USABLE);
	failures += test_expect_bool("bad flags init", plane_pmm_init(&mem),
				     true);
	failures += test_expect_bool("bad flags alloc",
				plane_pmm_alloc_pages_phys_flags(1, 1,
								 0x80000000u,
								 &phys),
				false);

	stats = plane_pmm_get_stats();
	failures += check_stats("bad flags stats", &stats,
				 3, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1);

	return failures;
}

static int test_single_page_free_reuses_lowest_address(void)
{
	struct plane_mem_info mem = {0};
	plane_paddr_t phys;
	int failures = 0;

	add_region(&mem, 0x1000, 0x5000, PLANE_MEM_USABLE);
	failures += test_expect_bool("reuse lowest init",
				plane_pmm_init(&mem), true);

	failures += test_expect_bool("reuse alloc first",
				plane_pmm_alloc_page_phys(&phys), true);
	failures += test_expect_u64("reuse first addr", test_paddr_raw(phys), 0x2000);
	failures += test_expect_bool("reuse alloc second",
				plane_pmm_alloc_page_phys(&phys), true);
	failures += test_expect_u64("reuse second addr", test_paddr_raw(phys), 0x3000);
	failures += test_expect_bool("reuse alloc third",
				plane_pmm_alloc_page_phys(&phys), true);
	failures += test_expect_u64("reuse third addr", test_paddr_raw(phys), 0x4000);

	failures += test_expect_bool("reuse free second",
				plane_pmm_free_page_phys(test_paddr(0x3000)), true);
	failures += test_expect_bool("reuse free first",
				plane_pmm_free_page_phys(test_paddr(0x2000)), true);
	failures += test_expect_bool("reuse alloc lowest",
				plane_pmm_alloc_page_phys(&phys), true);
	failures += test_expect_u64("reuse lowest addr", test_paddr_raw(phys), 0x2000);

	return failures;
}

static int test_multi_page_alignment(void)
{
	struct plane_mem_info mem = {0};
	struct plane_pmm_stats stats;
	plane_paddr_t phys;
	int failures = 0;

	add_region(&mem, 0x1000, 0x8000, PLANE_MEM_USABLE);
	failures += test_expect_bool("alignment init", plane_pmm_init(&mem), true);
	failures += test_expect_bool("reject null output",
				plane_pmm_alloc_pages_phys(1, 1, NULL), false);
	failures += test_expect_bool("reject zero page count",
				plane_pmm_alloc_pages_phys(0, 1, &phys), false);
	failures += test_expect_bool("reject zero alignment",
				plane_pmm_alloc_pages_phys(1, 0, &phys), false);
	failures += test_expect_bool("reject non-power-of-two alignment",
				plane_pmm_alloc_pages_phys(1, 3, &phys), false);

	failures += test_expect_bool("aligned allocation",
				plane_pmm_alloc_pages_phys(1, 4, &phys), true);
	failures += test_expect_u64("aligned allocation address", test_paddr_raw(phys), 0x4000);
	failures += check_page_state("aligned allocation page state",
				      plane_vm_page_state(plane_vm_page_from_phys(phys)),
				      PLANE_VM_PAGE_ALLOCATED);

	stats = plane_pmm_get_stats();
	failures += check_stats("aligned allocation", &stats,
				 8, 6, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2);

	return failures;
}

static int test_multi_page_rejects_non_contiguous_ranges(void)
{
	struct plane_mem_info mem = {0};
	plane_paddr_t phys;
	int failures = 0;

	add_region(&mem, 0x1000, 0x2000, PLANE_MEM_USABLE);
	add_region(&mem, 0x4000, 0x1000, PLANE_MEM_USABLE);
	failures += test_expect_bool("non-contig init", plane_pmm_init(&mem), true);
	failures += test_expect_bool("non-contig two pages",
				plane_pmm_alloc_pages_phys(2, 1, &phys), false);

	return failures;
}

static int test_multi_page_rejects_metadata_run(void)
{
	struct plane_mem_info mem = {0};
	plane_paddr_t phys;
	int failures = 0;

	add_region(&mem, 0x1000, 0x3000, PLANE_MEM_USABLE);
	failures += test_expect_bool("metadata run init",
				plane_pmm_init(&mem), true);
	failures += test_expect_bool("metadata run rejects full range",
				plane_pmm_alloc_pages_phys(3, 1, &phys), false);
	failures += test_expect_bool("metadata run allows free tail",
				plane_pmm_alloc_pages_phys(2, 1, &phys), true);
	failures += test_expect_u64("metadata run tail addr", test_paddr_raw(phys), 0x2000);

	return failures;
}

static int test_multi_page_phys_api_updates_metadata(void)
{
	struct plane_mem_info mem = {0};
	struct plane_pmm_stats stats;
	plane_paddr_t phys;
	int failures = 0;

	add_region(&mem, 0x1000, 0x6000, PLANE_MEM_USABLE);
	failures += test_expect_bool("multi metadata init",
				plane_pmm_init(&mem), true);

	failures += test_expect_bool("multi metadata alloc",
				plane_pmm_alloc_pages_phys(3, 1, &phys), true);
	failures += test_expect_u64("multi metadata alloc addr",
				    test_paddr_raw(phys), 0x2000);
	for (uint64_t i = 0; i < 3; i++) {
		failures += check_page_state("multi metadata allocated",
					      plane_vm_page_state(
						      plane_vm_page_from_phys(
							      test_paddr(
								      test_paddr_raw(phys) +
								      i * PAGE_SIZE))),
					      PLANE_VM_PAGE_ALLOCATED);
	}

	stats = plane_pmm_get_stats();
	failures += check_stats("multi metadata allocated", &stats,
				 6, 2, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1);

	failures += test_expect_bool("multi metadata free",
				plane_pmm_free_pages_phys(phys, 3), true);
	for (uint64_t i = 0; i < 3; i++) {
		failures += check_page_state("multi metadata free",
					      plane_vm_page_state(
						      plane_vm_page_from_phys(
							      test_paddr(
								      test_paddr_raw(phys) +
								      i * PAGE_SIZE))),
					      PLANE_VM_PAGE_FREE);
	}

	stats = plane_pmm_get_stats();
	failures += check_stats("multi metadata freed", &stats,
				 6, 5, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1);

	return failures;
}

static int test_free_merges_ranges(void)
{
	struct plane_mem_info mem = {0};
	struct plane_pmm_stats stats;
	plane_paddr_t phys;
	int failures = 0;

	add_region(&mem, 0x1000, 0x5000, PLANE_MEM_USABLE);
	failures += test_expect_bool("free merge init", plane_pmm_init(&mem), true);
	failures += test_expect_bool("alloc page 1",
				plane_pmm_alloc_page_phys(&phys), true);
	failures += test_expect_u64("alloc page 1 addr", test_paddr_raw(phys), 0x2000);
	failures += test_expect_bool("alloc page 2",
				plane_pmm_alloc_page_phys(&phys), true);
	failures += test_expect_u64("alloc page 2 addr", test_paddr_raw(phys), 0x3000);
	failures += test_expect_bool("alloc page 3",
				plane_pmm_alloc_page_phys(&phys), true);
	failures += test_expect_u64("alloc page 3 addr", test_paddr_raw(phys), 0x4000);

	failures += test_expect_bool("free middle page",
				plane_pmm_free_page_phys(test_paddr(0x3000)), true);
	stats = plane_pmm_get_stats();
	failures += check_stats("free middle", &stats,
				 5, 2, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2);

	failures += test_expect_bool("free previous page",
				plane_pmm_free_page_phys(test_paddr(0x2000)), true);
	stats = plane_pmm_get_stats();
	failures += check_stats("free previous", &stats,
				 5, 3, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2);

	failures += test_expect_bool("free bridge page",
				plane_pmm_free_page_phys(test_paddr(0x4000)), true);
	stats = plane_pmm_get_stats();
	failures += check_stats("free bridge", &stats,
				 5, 4, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1);

	return failures;
}

static int test_free_rejects_invalid_ranges(void)
{
	struct plane_mem_info mem = {0};
	plane_paddr_t phys;
	int failures = 0;

	add_region(&mem, 0x1000, 0x2000, PLANE_MEM_USABLE);
	failures += test_expect_bool("free reject init", plane_pmm_init(&mem), true);
	failures += test_expect_bool("reject metadata free",
				plane_pmm_free_page_phys(test_paddr(0x1000)), false);
	failures += test_expect_bool("allocate first for reject tests",
				plane_pmm_alloc_page_phys(&phys), true);
	failures += test_expect_u64("allocated reject test page", test_paddr_raw(phys), 0x2000);
	failures += test_expect_bool("reject unaligned free",
				plane_pmm_free_page_phys(test_paddr(0x2001)), false);
	failures += test_expect_bool("reject unmanaged free",
				plane_pmm_free_page_phys(test_paddr(0x9000)), false);
	failures += test_expect_bool("free allocated page",
				plane_pmm_free_page_phys(test_paddr(0x2000)), true);
	failures += test_expect_bool("reject double-free after release",
				plane_pmm_free_page_phys(test_paddr(0x2000)), false);

	return failures;
}

static int test_public_pmm_operations_enter_vm_page_under_pmm_lock(void)
{
	struct plane_mem_info mem = {0};
	struct plane_pmm_stats stats;
	plane_paddr_t phys;
	int failures = 0;

	add_region(&mem, 0x1000, 0x6000, PLANE_MEM_USABLE);

	test_spinlock_stub_reset_counts();
	failures += test_expect_bool("lock init",
				     plane_pmm_init(&mem), true);
	failures += check_spinlock_depth("lock init", 2, 0);

	test_spinlock_stub_reset_counts();
	failures += test_expect_bool("lock alloc page",
				     plane_pmm_alloc_page_phys(&phys), true);
	failures += check_spinlock_depth("lock alloc page", 2, 0);

	test_spinlock_stub_reset_counts();
	failures += test_expect_bool("lock free page",
				     plane_pmm_free_page_phys(phys), true);
	failures += check_spinlock_depth("lock free page", 2, 0);

	test_spinlock_stub_reset_counts();
	stats = plane_pmm_get_stats();
	(void)stats;
	failures += check_spinlock_depth("lock stats", 2, 0);

	test_spinlock_stub_reset_counts();
	failures += test_expect_bool("lock alloc wrapper",
				     plane_pmm_alloc_pages_phys(1, 1, &phys),
				     true);
	failures += check_spinlock_depth("lock alloc wrapper", 2, 0);

	return failures;
}

static int test_init_fails_without_physmap(void)
{
	struct plane_mem_info mem = {0};
	int failures = 0;

	add_region(&mem, 0x1000, 0x3000, PLANE_MEM_USABLE);
	physmap_available = false;
	failures += test_expect_bool("physmap missing init",
				plane_pmm_init(&mem), false);
	physmap_available = true;

	return failures;
}

static int test_init_fails_without_metadata_range_coverage(void)
{
	struct plane_mem_info mem = {0};
	int failures = 0;

	add_region(&mem, 0x1000, 0x3000, PLANE_MEM_USABLE);
	physmap_limit = 0x1001;
	failures += test_expect_bool("metadata range missing init",
				plane_pmm_init(&mem), false);
	physmap_limit = PHYSMAP_STORAGE_SIZE;

	return failures;
}

int main(void)
{
	static const struct test_case cases[] = {
		TEST_CASE(test_init_accounts_all_memmap_types),
		TEST_CASE(test_usable_region_reserves_null_physical_page),
		TEST_CASE(test_usable_region_inside_null_page_is_reserved),
		TEST_CASE(test_run_allocation_skips_null_physical_page),
		TEST_CASE(test_grub_like_reservations_are_counted),
		TEST_CASE(test_limine_like_rich_memmap_is_counted),
		TEST_CASE(test_single_page_allocation_order_and_exhaustion),
		TEST_CASE(test_plain_allocation_does_not_zero_page),
		TEST_CASE(test_zeroed_multi_page_allocation),
		TEST_CASE(test_zeroed_allocation_rolls_back_without_physmap),
		TEST_CASE(test_zeroed_allocation_rolls_back_without_range_coverage),
		TEST_CASE(test_allocation_flags_reject_unknown_bits),
		TEST_CASE(test_single_page_free_reuses_lowest_address),
		TEST_CASE(test_multi_page_alignment),
		TEST_CASE(test_multi_page_rejects_non_contiguous_ranges),
		TEST_CASE(test_multi_page_rejects_metadata_run),
		TEST_CASE(test_multi_page_phys_api_updates_metadata),
		TEST_CASE(test_free_merges_ranges),
		TEST_CASE(test_free_rejects_invalid_ranges),
		TEST_CASE(test_public_pmm_operations_enter_vm_page_under_pmm_lock),
		TEST_CASE(test_init_fails_without_physmap),
		TEST_CASE(test_init_fails_without_metadata_range_coverage),
	};

	return test_run_cases_with_fixture("pmm_test", cases,
					   TEST_ARRAY_SIZE(cases),
					   reset_physmap_stub, NULL);
}
