#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <plane/compiler.h>
#include <plane/memmap.h>
#include <plane/mm.h>
#include <plane/pmm.h>
#include <plane/vm_page.h>
#include <plane/vm_object.h>

#include "support/test.h"
#include "../kernel/mm/vm_page_internal.h"

static bool direct_map_available = true;
#define DIRECT_MAP_STORAGE_SIZE (1024 * 1024)
static uint64_t direct_map_limit = DIRECT_MAP_STORAGE_SIZE;
static uint8_t direct_map_storage[DIRECT_MAP_STORAGE_SIZE]
	__aligned(PAGE_SIZE);

void *hal_mmu_direct_phys_range_to_virt(uint64_t phys_addr, uint64_t size)
{
	if (!direct_map_available || size == 0 ||
	    phys_addr > direct_map_limit ||
	    size > direct_map_limit - phys_addr ||
	    phys_addr > DIRECT_MAP_STORAGE_SIZE ||
	    size > DIRECT_MAP_STORAGE_SIZE - phys_addr) {
		return NULL;
	}

	return &direct_map_storage[phys_addr];
}

void *hal_mmu_direct_phys_to_virt(uint64_t phys_addr)
{
	return hal_mmu_direct_phys_range_to_virt(phys_addr, 1);
}

static void reset_direct_map_stub(void)
{
	direct_map_available = true;
	direct_map_limit = DIRECT_MAP_STORAGE_SIZE;
	memset(direct_map_storage, 0, sizeof(direct_map_storage));
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
	if (phys_addr > DIRECT_MAP_STORAGE_SIZE ||
	    length > DIRECT_MAP_STORAGE_SIZE - phys_addr) {
		test_fail("%s out of direct-map test storage", name);
		return 1;
	}

	for (uint64_t i = 0; i < length; i++) {
		if (direct_map_storage[phys_addr + i] != expected) {
			test_fail("%s offset=%llu expected=%u actual=%u",
				  name,
				  (unsigned long long)i,
				  expected,
				  direct_map_storage[phys_addr + i]);
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

	mem->map[index].base = base;
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

static int test_phys_to_page_metadata(void)
{
	struct plane_mem_info mem = {0};
	struct plane_page *page;
	int failures = 0;

	add_region(&mem, 0x1000, 0x2000, PLANE_MEM_USABLE);
	add_region(&mem, 0x8000, 0x1000, PLANE_MEM_USABLE);
	failures += test_expect_bool("metadata init", plane_pmm_init(&mem), true);

	page = plane_vm_page_from_phys(0x1000);
	failures += test_expect_not_null("metadata page 0x1000", page);
	failures += test_expect_u64("metadata phys 0x1000",
			       plane_vm_page_phys(page), 0x1000);
	failures += check_page_state("metadata state",
				      plane_vm_page_state(page),
				      PLANE_VM_PAGE_METADATA);

	page = plane_vm_page_from_phys(0x8000);
	failures += test_expect_not_null("metadata non-contig page", page);
	failures += test_expect_u64("metadata non-contig phys",
			       plane_vm_page_phys(page), 0x8000);
	failures += check_page_state("metadata non-contig state",
				      plane_vm_page_state(page),
				      PLANE_VM_PAGE_FREE);

	failures += test_expect_null("metadata reject unaligned",
				    plane_vm_page_from_phys(0x1001));
	failures += test_expect_null("metadata reject unmanaged",
				    plane_vm_page_from_phys(0x4000));
	failures += check_page_state("metadata null state",
				      plane_vm_page_state(NULL),
				      PLANE_VM_PAGE_INVALID);
	failures += test_expect_u64("metadata null phys",
				    plane_vm_page_phys(NULL),
				    PLANE_VM_PAGE_NO_PHYS);

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

	failures += test_expect_bool("pmm init all types",
				plane_pmm_init(&mem), true);
	stats = plane_pmm_get_stats();
	failures += check_stats("all types", &stats,
				 3, 2, 3, 2, 1, 1, 1, 1, 2, 2, 1, 1, 1);

	return failures;
}

static int test_vm_page_grab_allocates_and_releases_metadata(void)
{
	struct plane_mem_info mem = {0};
	struct plane_pmm_stats stats;
	struct plane_page *page;
	struct plane_page *bad_page = (struct plane_page *)(uintptr_t)&mem;
	int failures = 0;

	add_region(&mem, 0x1000, 0x3000, PLANE_MEM_USABLE);
	failures += test_expect_bool("vm page init", plane_pmm_init(&mem), true);

	failures += test_expect_bool("reject null vm page output",
				plane_vm_page_grab(0, NULL), false);
	failures += test_expect_bool("grab vm page",
				plane_vm_page_grab(0, &page), true);
	failures += test_expect_not_null("grabbed vm page", page);
	failures += test_expect_u64("grabbed vm page phys",
			       plane_vm_page_phys(page), 0x2000);
	failures += check_page_state("grabbed vm page state",
				      plane_vm_page_state(page),
				      PLANE_VM_PAGE_ALLOCATED);

	stats = plane_pmm_get_stats();
	failures += check_stats("vm page grabbed", &stats,
				 3, 1, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1);

	failures += test_expect_bool("reject null vm page release",
				plane_vm_page_release(NULL), false);
	failures += test_expect_bool("reject foreign vm page release",
				plane_vm_page_release(bad_page), false);
	failures += test_expect_bool("release vm page",
				plane_vm_page_release(page), true);
	failures += check_page_state("released vm page state",
				      plane_vm_page_state(page),
				      PLANE_VM_PAGE_FREE);
	failures += test_expect_bool("reject vm page double release",
				plane_vm_page_release(page), false);

	stats = plane_pmm_get_stats();
	failures += check_stats("vm page released", &stats,
				 3, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1);

	return failures;
}

static int test_page_wire_count_tracks_allocated_pages(void)
{
	struct plane_mem_info mem = {0};
	struct plane_pmm_stats stats;
	struct plane_page *page;
	uint64_t phys;
	uint64_t wire_count;
	int failures = 0;

	add_region(&mem, 0x1000, 0x3000, PLANE_MEM_USABLE);
	failures += test_expect_bool("wire init", plane_pmm_init(&mem), true);
	failures += test_expect_bool("wire alloc",
				plane_pmm_alloc_page_phys(&phys), true);
	failures += test_expect_u64("wire alloc phys", phys, 0x2000);
	page = plane_vm_page_from_phys(phys);
	failures += test_expect_bool("wire initial count query",
				     plane_vm_page_wire_count(page, &wire_count),
				     true);
	failures += test_expect_u64("wire initial count", wire_count, 0);

	failures += test_expect_bool("wire page",
				     plane_vm_page_wire(page), true);
	failures += test_expect_bool("wire count one query",
				     plane_vm_page_wire_count(page, &wire_count),
				     true);
	failures += test_expect_u64("wire count one", wire_count, 1);
	failures += test_expect_bool("wire page again",
				     plane_vm_page_wire(page), true);
	failures += test_expect_bool("wire count two query",
				     plane_vm_page_wire_count(page, &wire_count),
				     true);
	failures += test_expect_u64("wire count two", wire_count, 2);

	stats = plane_pmm_get_stats();
	failures += check_stats_wired("wire stats", &stats,
				 3, 1, 1, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1);
	failures += test_expect_bool("free wired rejected",
				     plane_pmm_free_page_phys(phys), false);
	failures += test_expect_bool("release wired rejected",
				     plane_vm_page_release(page), false);

	failures += test_expect_bool("unwire page",
				     plane_vm_page_unwire(page), true);
	failures += test_expect_bool("wire count one after unwire query",
				     plane_vm_page_wire_count(page, &wire_count),
				     true);
	failures += test_expect_u64("wire count one after unwire", wire_count, 1);
	failures += test_expect_bool("unwire page again",
				     plane_vm_page_unwire(page), true);
	failures += test_expect_bool("wire count zero query",
				     plane_vm_page_wire_count(page, &wire_count),
				     true);
	failures += test_expect_u64("wire count zero", wire_count, 0);
	failures += test_expect_bool("unwire zero rejected",
				     plane_vm_page_unwire(page), false);
	failures += test_expect_bool("free unwired page",
				     plane_pmm_free_page_phys(phys), true);

	stats = plane_pmm_get_stats();
	failures += check_stats("wire freed stats", &stats,
				 3, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1);
	return failures;
}

static int test_wire_rejects_invalid_pages(void)
{
	struct plane_mem_info mem = {0};
	struct plane_page *free_page;
	struct plane_page *metadata_page;
	uint64_t phys;
	uint64_t wire_count;
	int failures = 0;

	add_region(&mem, 0x1000, 0x3000, PLANE_MEM_USABLE);
	failures += test_expect_bool("wire invalid init",
				     plane_pmm_init(&mem), true);
	metadata_page = plane_vm_page_from_phys(0x1000);
	free_page = plane_vm_page_from_phys(0x2000);
	failures += test_expect_bool("wire rejects metadata",
				     plane_vm_page_wire(metadata_page), false);
	failures += test_expect_bool("unwire rejects metadata",
				     plane_vm_page_unwire(metadata_page), false);
	failures += test_expect_bool("wire rejects free page",
				     plane_vm_page_wire(free_page), false);
	failures += test_expect_bool("unwire rejects free page",
				     plane_vm_page_unwire(free_page), false);
	failures += test_expect_bool("wire rejects unmanaged",
				     plane_vm_page_wire(NULL), false);
	failures += test_expect_bool("unwire rejects unmanaged",
				     plane_vm_page_unwire(NULL), false);
	failures += test_expect_bool("wire count rejects null page",
				     plane_vm_page_wire_count(NULL, &wire_count),
				     false);
	failures += test_expect_bool("wire count rejects null out",
				     plane_vm_page_wire_count(free_page, NULL),
				     false);
	failures += test_expect_bool("wire count accepts metadata page",
				     plane_vm_page_wire_count(metadata_page,
							   &wire_count),
				     true);
	failures += test_expect_u64("metadata wire count", wire_count, 0);
	failures += test_expect_bool("wire count accepts free page",
				     plane_vm_page_wire_count(free_page,
							   &wire_count),
				     true);
	failures += test_expect_u64("free page wire count", wire_count, 0);

	failures += test_expect_bool("wire invalid alloc",
				     plane_pmm_alloc_page_phys(&phys), true);
	failures += test_expect_u64("wire invalid phys", phys, 0x2000);
	failures += test_expect_bool("wire invalid free unwired",
				     plane_pmm_free_page_phys(phys), true);
	return failures;
}

static int test_guard_pages_are_not_pmm_managed(void)
{
	struct plane_mem_info mem = {0};
	struct plane_pmm_stats before;
	struct plane_pmm_stats after;
	struct plane_vm_object object = {0};
	struct plane_page *guard;
	uint64_t wire_count = UINT64_MAX;
	int failures = 0;

	add_region(&mem, 0x1000, 0x3000, PLANE_MEM_USABLE);
	failures += test_expect_bool("guard pmm init",
				     plane_pmm_init(&mem), true);
	failures += test_expect_bool("guard object init",
				     plane_vm_object_init(&object, PAGE_SIZE),
				     true);
	before = plane_pmm_get_stats();
	guard = plane_vm_page_create_guard();
	failures += test_expect_not_null("guard create", guard);
	failures += check_page_state("guard state",
				      plane_vm_page_state(guard),
				      PLANE_VM_PAGE_GUARD);
	failures += test_expect_bool("guard query",
				     plane_vm_page_is_guard(guard), true);
	failures += test_expect_u64("guard phys",
				    plane_vm_page_phys(guard),
				    PLANE_VM_PAGE_GUARD_PHYS);
	failures += test_expect_null("guard from phys",
				     plane_vm_page_from_phys(
					     PLANE_VM_PAGE_GUARD_PHYS));
	failures += test_expect_bool("guard wire rejected",
				     plane_vm_page_wire(guard), false);
	failures += test_expect_bool("guard unwire rejected",
				     plane_vm_page_unwire(guard), false);
	failures += test_expect_bool("guard wire count query",
				     plane_vm_page_wire_count(guard,
							      &wire_count),
				     true);
	failures += test_expect_u64("guard wire count", wire_count, 0);
	failures += test_expect_bool("guard pmm free rejected",
				     plane_vm_page_release(guard), false);
	failures += test_expect_bool("guard release",
				     plane_vm_page_release_guard(guard), true);
	failures += check_page_state("guard released state",
				      plane_vm_page_state(guard),
				      PLANE_VM_PAGE_INVALID);
	failures += test_expect_u64("guard released phys",
				    plane_vm_page_phys(guard),
				    PLANE_VM_PAGE_NO_PHYS);
	failures += test_expect_bool("guard released query",
				     plane_vm_page_is_guard(guard), false);
	failures += test_expect_bool("guard released wire count rejected",
				     plane_vm_page_wire_count(guard,
							      &wire_count),
				     false);
	failures += test_expect_bool("guard released attach rejected",
				     plane_vm_page_attach_object(guard,
								 &object, 0),
				     false);
	failures += test_expect_bool("guard double release rejected",
				     plane_vm_page_release_guard(guard), false);
	after = plane_pmm_get_stats();
	failures += test_expect_u64("guard managed unchanged",
				    after.allocator.managed_pages,
				    before.allocator.managed_pages);
	failures += test_expect_u64("guard free unchanged",
				    after.allocator.free_pages,
				    before.allocator.free_pages);
	failures += test_expect_u64("guard wired unchanged",
				    after.allocator.wired_pages,
				    before.allocator.wired_pages);
	return failures;
}

static int test_page_object_identity_blocks_free(void)
{
	struct plane_mem_info mem = {0};
	struct plane_vm_object object = {0};
	struct plane_page *page;
	uint64_t offset = 0;
	uint64_t phys;
	int failures = 0;

	add_region(&mem, 0x1000, 0x3000, PLANE_MEM_USABLE);
	failures += test_expect_bool("object identity init",
				     plane_pmm_init(&mem), true);
	failures += test_expect_bool("object identity alloc",
				     plane_pmm_alloc_page_phys(&phys), true);
	failures += test_expect_bool("object identity object init",
				     plane_vm_object_init(&object, 0x8000),
				     true);
	page = plane_vm_page_from_phys(phys);
	failures += test_expect_null("object identity initial object",
				     plane_vm_page_object(page));
	failures += test_expect_bool("object identity initial offset",
				     plane_vm_page_object_offset(page, &offset),
				     false);
	failures += test_expect_bool("object identity insert",
				     plane_vm_object_insert_page(&object,
								 0x4000, page),
				     true);
	failures += test_expect_bool("object identity release rejected",
				     plane_vm_page_release(page), false);
	failures += test_expect_u64("object identity resident count",
				    plane_vm_object_resident_page_count(&object),
				    1);
	failures += test_expect_u64("object identity wired count",
				    plane_vm_object_wired_page_count(&object),
				    0);
	failures += test_expect_ptr("object identity object",
				    plane_vm_page_object(page), &object);
	failures += test_expect_bool("object identity offset query",
				     plane_vm_page_object_offset(page, &offset),
				     true);
	failures += test_expect_u64("object identity offset", offset, 0x4000);
	failures += test_expect_bool("object identity free rejected",
				     plane_pmm_free_page_phys(phys), false);
	failures += test_expect_bool("object identity wire",
				     plane_vm_page_wire(page), true);
	failures += test_expect_u64("object identity wired once",
				    plane_vm_object_wired_page_count(&object),
				    1);
	failures += test_expect_bool("object identity wire twice",
				     plane_vm_page_wire(page), true);
	failures += test_expect_u64("object identity wired count stable",
				    plane_vm_object_wired_page_count(&object),
				    1);
	failures += test_expect_bool("object identity unwire once",
				     plane_vm_page_unwire(page), true);
	failures += test_expect_u64("object identity still wired",
				    plane_vm_object_wired_page_count(&object),
				    1);
	failures += test_expect_bool("object identity unwire twice",
				     plane_vm_page_unwire(page), true);
	failures += test_expect_u64("object identity unwired",
				    plane_vm_object_wired_page_count(&object),
				    0);
	failures += test_expect_ptr("object identity remove",
				    plane_vm_object_remove_page(&object, 0x4000),
				    page);
	failures += test_expect_u64("object identity resident removed",
				    plane_vm_object_resident_page_count(&object),
				    0);
	failures += test_expect_u64("object identity wired removed",
				    plane_vm_object_wired_page_count(&object),
				    0);
	failures += test_expect_null("object identity cleared object",
				     plane_vm_page_object(page));
	failures += test_expect_bool("object identity cleared offset",
				     plane_vm_page_object_offset(page, &offset),
				     false);
	failures += test_expect_bool("object identity free",
				     plane_pmm_free_page_phys(phys), true);
	return failures;
}

static int test_grub_like_reservations_are_counted(void)
{
	struct plane_mem_info mem = {0};
	struct plane_pmm_stats stats;
	int failures = 0;

	add_region(&mem, 0x1000, 0x9000, PLANE_MEM_USABLE);
	failures += test_expect_bool("grub reserve boot info",
				plane_memmap_reserve(&mem, 0x2000, 0x1000,
						     PLANE_MEM_BOOTLOADER_RECLAIMABLE),
				true);
	failures += test_expect_bool("grub reserve kernel image",
				plane_memmap_reserve(&mem, 0x4000, 0x2000,
						     PLANE_MEM_EXECUTABLE_AND_MODULES),
				true);
	failures += test_expect_bool("grub reserve framebuffer",
				plane_memmap_reserve(&mem, 0x7000, 0x1000,
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
				plane_sanitize_memory_map(&mem), true);
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
	uint64_t phys;
	int failures = 0;

	add_region(&mem, 0x1000, 0x3000, PLANE_MEM_USABLE);
	failures += test_expect_bool("alloc order init", plane_pmm_init(&mem), true);

	failures += test_expect_bool("alloc first page",
				plane_pmm_alloc_page_phys(&phys), true);
	failures += test_expect_u64("first page address", phys, 0x2000);
	failures += test_expect_bool("alloc second page",
				plane_pmm_alloc_page_phys(&phys), true);
	failures += test_expect_u64("second page address", phys, 0x3000);
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
	uint64_t phys;
	int failures = 0;

	add_region(&mem, 0x1000, 0x3000, PLANE_MEM_USABLE);
	failures += test_expect_bool("plain zero init", plane_pmm_init(&mem),
				     true);

	memset(&direct_map_storage[0x2000], 0xa5, PAGE_SIZE);
	failures += test_expect_bool("plain alloc",
				plane_pmm_alloc_page_phys(&phys), true);
	failures += test_expect_u64("plain alloc phys", phys, 0x2000);
	failures += check_phys_bytes("plain alloc keeps data", phys, 0xa5,
				      PAGE_SIZE);

	return failures;
}

static int test_zeroed_single_page_allocation(void)
{
	struct plane_mem_info mem = {0};
	struct plane_page *page;
	int failures = 0;

	add_region(&mem, 0x1000, 0x3000, PLANE_MEM_USABLE);
	failures += test_expect_bool("zero page init", plane_pmm_init(&mem),
				     true);

	memset(&direct_map_storage[0x2000], 0xa5, PAGE_SIZE);
	failures += test_expect_bool("zero page alloc",
				plane_vm_page_grab(PLANE_VM_PAGE_GRAB_ZERO,
						   &page),
				true);
	failures += test_expect_not_null("zero page metadata", page);
	failures += test_expect_u64("zero page phys", plane_vm_page_phys(page),
				    0x2000);
	failures += check_phys_bytes("zero page cleared", 0x2000, 0,
				      PAGE_SIZE);

	return failures;
}

static int test_zeroed_multi_page_allocation(void)
{
	struct plane_mem_info mem = {0};
	uint64_t phys;
	int failures = 0;

	add_region(&mem, 0x1000, 0x7000, PLANE_MEM_USABLE);
	failures += test_expect_bool("zero multi init", plane_pmm_init(&mem),
				     true);

	memset(&direct_map_storage[0x2000], 0xa5, 3 * PAGE_SIZE);
	memset(&direct_map_storage[0x5000], 0x5a, PAGE_SIZE);
	failures += test_expect_bool("zero multi alloc",
				plane_pmm_alloc_pages_phys_flags(
					3, 1, PLANE_PMM_ALLOC_ZERO, &phys),
				true);
	failures += test_expect_u64("zero multi phys", phys, 0x2000);
	failures += check_phys_bytes("zero multi cleared", 0x2000, 0,
				      3 * PAGE_SIZE);
	failures += check_phys_bytes("zero multi guard", 0x5000, 0x5a,
				      PAGE_SIZE);

	return failures;
}

static int test_zeroed_allocation_rolls_back_without_direct_map(void)
{
	struct plane_mem_info mem = {0};
	struct plane_pmm_stats stats;
	uint64_t phys = UINT64_MAX;
	int failures = 0;

	add_region(&mem, 0x1000, 0x3000, PLANE_MEM_USABLE);
	failures += test_expect_bool("zero rollback init", plane_pmm_init(&mem),
				     true);

	direct_map_available = false;
	failures += test_expect_bool("zero rollback alloc",
				plane_pmm_alloc_pages_phys_flags(
					1, 1, PLANE_PMM_ALLOC_ZERO, &phys),
				false);
	direct_map_available = true;

	stats = plane_pmm_get_stats();
	failures += check_stats("zero rollback stats", &stats,
				 3, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1);
	failures += check_page_state("zero rollback page state",
				      plane_vm_page_state(
					      plane_vm_page_from_phys(0x2000)),
				      PLANE_VM_PAGE_FREE);
	failures += test_expect_bool("zero rollback reuses page",
				plane_pmm_alloc_page_phys(&phys), true);
	failures += test_expect_u64("zero rollback reused phys", phys, 0x2000);

	return failures;
}

static int test_zeroed_allocation_rolls_back_without_range_coverage(void)
{
	struct plane_mem_info mem = {0};
	struct plane_pmm_stats stats;
	uint64_t phys = UINT64_MAX;
	int failures = 0;

	add_region(&mem, 0x1000, 0x3000, PLANE_MEM_USABLE);
	failures += test_expect_bool("zero range rollback init",
				     plane_pmm_init(&mem), true);

	direct_map_limit = 0x2800;
	failures += test_expect_bool("zero range rollback alloc",
				plane_pmm_alloc_pages_phys_flags(
					1, 1, PLANE_PMM_ALLOC_ZERO, &phys),
				false);
	direct_map_limit = DIRECT_MAP_STORAGE_SIZE;

	stats = plane_pmm_get_stats();
	failures += check_stats("zero range rollback stats", &stats,
				 3, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1);
	failures += check_page_state("zero range rollback page state",
				      plane_vm_page_state(
					      plane_vm_page_from_phys(0x2000)),
				      PLANE_VM_PAGE_FREE);
	failures += test_expect_bool("zero range rollback reuses page",
				plane_pmm_alloc_page_phys(&phys), true);
	failures += test_expect_u64("zero range rollback reused phys", phys,
				    0x2000);

	return failures;
}

static int test_allocation_flags_reject_unknown_bits(void)
{
	struct plane_mem_info mem = {0};
	struct plane_pmm_stats stats;
	uint64_t phys;
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
	uint64_t phys;
	int failures = 0;

	add_region(&mem, 0x1000, 0x5000, PLANE_MEM_USABLE);
	failures += test_expect_bool("reuse lowest init",
				plane_pmm_init(&mem), true);

	failures += test_expect_bool("reuse alloc first",
				plane_pmm_alloc_page_phys(&phys), true);
	failures += test_expect_u64("reuse first addr", phys, 0x2000);
	failures += test_expect_bool("reuse alloc second",
				plane_pmm_alloc_page_phys(&phys), true);
	failures += test_expect_u64("reuse second addr", phys, 0x3000);
	failures += test_expect_bool("reuse alloc third",
				plane_pmm_alloc_page_phys(&phys), true);
	failures += test_expect_u64("reuse third addr", phys, 0x4000);

	failures += test_expect_bool("reuse free second",
				plane_pmm_free_page_phys(0x3000), true);
	failures += test_expect_bool("reuse free first",
				plane_pmm_free_page_phys(0x2000), true);
	failures += test_expect_bool("reuse alloc lowest",
				plane_pmm_alloc_page_phys(&phys), true);
	failures += test_expect_u64("reuse lowest addr", phys, 0x2000);

	return failures;
}

static int test_multi_page_alignment(void)
{
	struct plane_mem_info mem = {0};
	struct plane_pmm_stats stats;
	uint64_t phys;
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
	failures += test_expect_u64("aligned allocation address", phys, 0x4000);
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
	uint64_t phys;
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
	uint64_t phys;
	int failures = 0;

	add_region(&mem, 0x1000, 0x3000, PLANE_MEM_USABLE);
	failures += test_expect_bool("metadata run init",
				plane_pmm_init(&mem), true);
	failures += test_expect_bool("metadata run rejects full range",
				plane_pmm_alloc_pages_phys(3, 1, &phys), false);
	failures += test_expect_bool("metadata run allows free tail",
				plane_pmm_alloc_pages_phys(2, 1, &phys), true);
	failures += test_expect_u64("metadata run tail addr", phys, 0x2000);

	return failures;
}

static int test_multi_page_phys_api_updates_metadata(void)
{
	struct plane_mem_info mem = {0};
	struct plane_pmm_stats stats;
	uint64_t phys;
	int failures = 0;

	add_region(&mem, 0x1000, 0x6000, PLANE_MEM_USABLE);
	failures += test_expect_bool("multi metadata init",
				plane_pmm_init(&mem), true);

	failures += test_expect_bool("multi metadata alloc",
				plane_pmm_alloc_pages_phys(3, 1, &phys), true);
	failures += test_expect_u64("multi metadata alloc addr", phys, 0x2000);
	for (uint64_t i = 0; i < 3; i++) {
		failures += check_page_state("multi metadata allocated",
					      plane_vm_page_state(
						      plane_vm_page_from_phys(
							      phys + i * PAGE_SIZE)),
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
							      phys + i * PAGE_SIZE)),
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
	uint64_t phys;
	int failures = 0;

	add_region(&mem, 0x1000, 0x5000, PLANE_MEM_USABLE);
	failures += test_expect_bool("free merge init", plane_pmm_init(&mem), true);
	failures += test_expect_bool("alloc page 1",
				plane_pmm_alloc_page_phys(&phys), true);
	failures += test_expect_u64("alloc page 1 addr", phys, 0x2000);
	failures += test_expect_bool("alloc page 2",
				plane_pmm_alloc_page_phys(&phys), true);
	failures += test_expect_u64("alloc page 2 addr", phys, 0x3000);
	failures += test_expect_bool("alloc page 3",
				plane_pmm_alloc_page_phys(&phys), true);
	failures += test_expect_u64("alloc page 3 addr", phys, 0x4000);

	failures += test_expect_bool("free middle page",
				plane_pmm_free_page_phys(0x3000), true);
	stats = plane_pmm_get_stats();
	failures += check_stats("free middle", &stats,
				 5, 2, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2);

	failures += test_expect_bool("free previous page",
				plane_pmm_free_page_phys(0x2000), true);
	stats = plane_pmm_get_stats();
	failures += check_stats("free previous", &stats,
				 5, 3, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2);

	failures += test_expect_bool("free bridge page",
				plane_pmm_free_page_phys(0x4000), true);
	stats = plane_pmm_get_stats();
	failures += check_stats("free bridge", &stats,
				 5, 4, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1);

	return failures;
}

static int test_free_rejects_invalid_ranges(void)
{
	struct plane_mem_info mem = {0};
	uint64_t phys;
	int failures = 0;

	add_region(&mem, 0x1000, 0x2000, PLANE_MEM_USABLE);
	failures += test_expect_bool("free reject init", plane_pmm_init(&mem), true);
	failures += test_expect_bool("reject metadata free",
				plane_pmm_free_page_phys(0x1000), false);
	failures += test_expect_bool("allocate first for reject tests",
				plane_pmm_alloc_page_phys(&phys), true);
	failures += test_expect_u64("allocated reject test page", phys, 0x2000);
	failures += test_expect_bool("reject unaligned free",
				plane_pmm_free_page_phys(0x2001), false);
	failures += test_expect_bool("reject unmanaged free",
				plane_pmm_free_page_phys(0x9000), false);
	failures += test_expect_bool("free allocated page",
				plane_pmm_free_page_phys(0x2000), true);
	failures += test_expect_bool("reject double-free after release",
				plane_pmm_free_page_phys(0x2000), false);

	return failures;
}

static int test_init_fails_without_direct_map(void)
{
	struct plane_mem_info mem = {0};
	int failures = 0;

	add_region(&mem, 0x1000, 0x3000, PLANE_MEM_USABLE);
	direct_map_available = false;
	failures += test_expect_bool("direct map missing init",
				plane_pmm_init(&mem), false);
	direct_map_available = true;

	return failures;
}

static int test_init_fails_without_metadata_range_coverage(void)
{
	struct plane_mem_info mem = {0};
	int failures = 0;

	add_region(&mem, 0x1000, 0x3000, PLANE_MEM_USABLE);
	direct_map_limit = 0x1001;
	failures += test_expect_bool("metadata range missing init",
				plane_pmm_init(&mem), false);
	direct_map_limit = DIRECT_MAP_STORAGE_SIZE;

	return failures;
}

int main(void)
{
	static const struct test_case cases[] = {
		TEST_CASE(test_init_accounts_all_memmap_types),
		TEST_CASE(test_phys_to_page_metadata),
		TEST_CASE(test_vm_page_grab_allocates_and_releases_metadata),
		TEST_CASE(test_page_wire_count_tracks_allocated_pages),
		TEST_CASE(test_wire_rejects_invalid_pages),
		TEST_CASE(test_guard_pages_are_not_pmm_managed),
		TEST_CASE(test_page_object_identity_blocks_free),
		TEST_CASE(test_grub_like_reservations_are_counted),
		TEST_CASE(test_limine_like_rich_memmap_is_counted),
		TEST_CASE(test_single_page_allocation_order_and_exhaustion),
		TEST_CASE(test_plain_allocation_does_not_zero_page),
		TEST_CASE(test_zeroed_single_page_allocation),
		TEST_CASE(test_zeroed_multi_page_allocation),
		TEST_CASE(test_zeroed_allocation_rolls_back_without_direct_map),
		TEST_CASE(test_zeroed_allocation_rolls_back_without_range_coverage),
		TEST_CASE(test_allocation_flags_reject_unknown_bits),
		TEST_CASE(test_single_page_free_reuses_lowest_address),
		TEST_CASE(test_multi_page_alignment),
		TEST_CASE(test_multi_page_rejects_non_contiguous_ranges),
		TEST_CASE(test_multi_page_rejects_metadata_run),
		TEST_CASE(test_multi_page_phys_api_updates_metadata),
		TEST_CASE(test_free_merges_ranges),
		TEST_CASE(test_free_rejects_invalid_ranges),
		TEST_CASE(test_init_fails_without_direct_map),
		TEST_CASE(test_init_fails_without_metadata_range_coverage),
	};

	return test_run_cases_with_fixture("pmm_test", cases,
					   TEST_ARRAY_SIZE(cases),
					   reset_direct_map_stub, NULL);
}
