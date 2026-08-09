#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <plane/compiler.h>
#include <plane/memmap.h>
#include <plane/mm.h>
#include <plane/pmm.h>
#include <plane/vm_object.h>
#include <plane/vm_page.h>

#include "support/spinlock_stubs.h"
#include "support/test.h"
#include "../kernel/mm/vm_page_internal.h"
#include "../kernel/mm/vm_zone_internal.h"

#define PHYSMAP_STORAGE_SIZE (1024 * 1024)
#define TEST_GUARD_BOOTSTRAP_COUNT 64
#define TEST_GUARD_EXTRA_COUNT 4

static bool physmap_available = true;
static uint64_t physmap_limit = PHYSMAP_STORAGE_SIZE;
static uint8_t physmap_storage[PHYSMAP_STORAGE_SIZE] __aligned(PAGE_SIZE);
static uint8_t extra_guard_storage[PAGE_SIZE] __aligned(PAGE_SIZE);
static struct plane_vm_zone_segment extra_guard_segment;

static plane_paddr_t test_paddr(uint64_t raw)
{
	return plane_paddr_make(raw);
}

static uint64_t test_paddr_raw(plane_paddr_t addr)
{
	return plane_paddr_raw(addr);
}

static uint64_t test_page_phys_raw(const struct plane_page *page)
{
	return plane_paddr_raw(plane_vm_page_phys(page));
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
	memset(extra_guard_storage, 0, sizeof(extra_guard_storage));
	extra_guard_segment = (struct plane_vm_zone_segment){0};
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

static void add_region(struct plane_mem_info *mem, uint64_t base,
		       uint64_t length, uint32_t type)
{
	uint64_t index = mem->entry_count++;

	mem->map[index].base = plane_paddr_make(base);
	mem->map[index].length = length;
	mem->map[index].type = type;
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

static int test_vm_page_from_phys_metadata(void)
{
	struct plane_mem_info mem = {0};
	struct plane_page *page;
	int failures = 0;

	add_region(&mem, 0x1000, 0x2000, PLANE_MEM_USABLE);
	add_region(&mem, 0x8000, 0x1000, PLANE_MEM_USABLE);
	failures += test_expect_bool("metadata init", plane_pmm_init(&mem), true);

	page = plane_vm_page_from_phys(test_paddr(0x1000));
	failures += test_expect_not_null("metadata page 0x1000", page);
	failures += test_expect_u64("metadata phys 0x1000",
				    test_page_phys_raw(page), 0x1000);
	failures += check_page_state("metadata state",
				     plane_vm_page_state(page),
				     PLANE_VM_PAGE_METADATA);

	page = plane_vm_page_from_phys(test_paddr(0x8000));
	failures += test_expect_not_null("metadata non-contig page", page);
	failures += test_expect_u64("metadata non-contig phys",
				    test_page_phys_raw(page), 0x8000);
	failures += check_page_state("metadata non-contig state",
				     plane_vm_page_state(page),
				     PLANE_VM_PAGE_FREE);

	failures += test_expect_null("metadata reject unaligned",
				    plane_vm_page_from_phys(test_paddr(0x1001)));
	failures += test_expect_null("metadata reject unmanaged",
				    plane_vm_page_from_phys(test_paddr(0x4000)));
	failures += check_page_state("metadata null state",
				     plane_vm_page_state(NULL),
				     PLANE_VM_PAGE_INVALID);
	failures += test_expect_u64("metadata null phys",
				    test_page_phys_raw(NULL),
				    PLANE_VM_PAGE_NO_PHYS_RAW);

	return failures;
}

static int test_vm_page_queue_insert_orders_by_phys(void)
{
	struct plane_mem_info mem = {0};
	struct plane_vm_page_queue queue;
	struct plane_page foreign = {0};
	struct plane_page *pages[3];
	plane_paddr_t phys[3];
	int failures = 0;

	add_region(&mem, 0x1000, 0x6000, PLANE_MEM_USABLE);
	failures += test_expect_bool("queue order pmm init",
				     plane_pmm_init(&mem), true);
	failures += test_expect_bool("queue reject null init",
				     plane_vm_page_queue_init(
					     NULL, PLANE_VM_PAGE_QUEUE_FREE),
				     false);
	failures += test_expect_bool("queue reject none state",
				     plane_vm_page_queue_init(
					     &queue, PLANE_VM_PAGE_QUEUE_NONE),
				     false);
	failures += test_expect_bool("queue init",
				     plane_vm_page_queue_init(
					     &queue, PLANE_VM_PAGE_QUEUE_FREE),
				     true);
	failures += test_expect_u64("queue initial count",
				    plane_vm_page_queue_count(&queue), 0);
	failures += test_expect_ptr("queue initial head", queue.head, NULL);
	failures += test_expect_ptr("queue initial tail", queue.tail, NULL);

	for (uint64_t i = 0; i < TEST_ARRAY_SIZE(pages); i++) {
		failures += test_expect_bool("queue alloc page",
					     plane_pmm_alloc_page_phys(&phys[i]),
					     true);
		pages[i] = plane_vm_page_from_phys(phys[i]);
		failures += test_expect_not_null("queue page metadata",
						 pages[i]);
	}

	failures += test_expect_bool("queue insert high",
				     plane_vm_page_queue_insert_ordered(
					     &queue, pages[2]),
				     true);
	failures += test_expect_bool("queue insert low",
				     plane_vm_page_queue_insert_ordered(
					     &queue, pages[0]),
				     true);
	failures += test_expect_bool("queue insert middle",
				     plane_vm_page_queue_insert_ordered(
					     &queue, pages[1]),
				     true);
	failures += test_expect_u64("queue ordered count",
				    plane_vm_page_queue_count(&queue), 3);
	failures += test_expect_ptr("queue ordered head", queue.head,
				    pages[0]);
	failures += test_expect_ptr("queue ordered tail", queue.tail,
				    pages[2]);
	failures += test_expect_ptr("queue low next", pages[0]->queue_next,
				    pages[1]);
	failures += test_expect_ptr("queue middle prev",
				    pages[1]->queue_prev, pages[0]);
	failures += test_expect_ptr("queue middle next",
				    pages[1]->queue_next, pages[2]);
	failures += test_expect_ptr("queue high prev", pages[2]->queue_prev,
				    pages[1]);
	failures += test_expect_int("queue low state",
				    plane_vm_page_queue_state(pages[0]),
				    PLANE_VM_PAGE_QUEUE_FREE);
	failures += test_expect_int("queue middle state",
				    plane_vm_page_queue_state(pages[1]),
				    PLANE_VM_PAGE_QUEUE_FREE);
	failures += test_expect_bool("queue reject duplicate",
				     plane_vm_page_queue_insert_ordered(
					     &queue, pages[1]),
				     false);
	failures += test_expect_bool("queue reject null page",
				     plane_vm_page_queue_insert_ordered(
					     &queue, NULL),
				     false);
	failures += test_expect_bool("queue reject foreign page",
				     plane_vm_page_queue_insert_ordered(
					     &queue, &foreign),
				     false);

	for (uint64_t i = 0; i < TEST_ARRAY_SIZE(pages); i++) {
		failures += test_expect_bool("queue cleanup remove",
					     plane_vm_page_queue_remove(
						     &queue, pages[i]),
					     true);
		failures += test_expect_bool("queue cleanup free",
					     plane_pmm_free_page_phys(phys[i]),
					     true);
	}

	return failures;
}

static int test_vm_page_queue_remove_and_pop_clear_membership(void)
{
	struct plane_mem_info mem = {0};
	struct plane_vm_page_queue queue;
	struct plane_vm_page_queue wrong_queue;
	struct plane_page *pages[4];
	plane_paddr_t phys[4];
	struct plane_page *popped;
	int failures = 0;

	add_region(&mem, 0x1000, 0x8000, PLANE_MEM_USABLE);
	failures += test_expect_bool("queue remove pmm init",
				     plane_pmm_init(&mem), true);
	failures += test_expect_bool("queue remove init",
				     plane_vm_page_queue_init(
					     &queue, PLANE_VM_PAGE_QUEUE_FREE),
				     true);
	failures += test_expect_bool("queue wrong init",
				     plane_vm_page_queue_init(
					     &wrong_queue,
					     PLANE_VM_PAGE_QUEUE_FREE),
				     true);

	for (uint64_t i = 0; i < TEST_ARRAY_SIZE(pages); i++) {
		failures += test_expect_bool("queue remove alloc",
					     plane_pmm_alloc_page_phys(&phys[i]),
					     true);
		pages[i] = plane_vm_page_from_phys(phys[i]);
		failures += test_expect_bool("queue remove insert",
					     plane_vm_page_queue_insert_ordered(
						     &queue, pages[i]),
					     true);
	}

	failures += test_expect_bool("queue remove middle",
				     plane_vm_page_queue_remove(&queue,
								pages[1]),
				     true);
	failures += test_expect_u64("queue remove middle count",
				    plane_vm_page_queue_count(&queue), 3);
	failures += test_expect_ptr("queue middle prev clear",
				    pages[1]->queue_prev, NULL);
	failures += test_expect_ptr("queue middle next clear",
				    pages[1]->queue_next, NULL);
	failures += test_expect_int("queue middle state clear",
				    plane_vm_page_queue_state(pages[1]),
				    PLANE_VM_PAGE_QUEUE_NONE);
	failures += test_expect_ptr("queue low skips removed",
				    pages[0]->queue_next, pages[2]);
	failures += test_expect_ptr("queue high links back",
				    pages[2]->queue_prev, pages[0]);

	failures += test_expect_bool("queue remove head",
				     plane_vm_page_queue_remove(&queue,
								pages[0]),
				     true);
	failures += test_expect_ptr("queue head advanced", queue.head,
				    pages[2]);
	failures += test_expect_ptr("queue head prev clear",
				    pages[2]->queue_prev, NULL);

	failures += test_expect_bool("queue remove tail",
				     plane_vm_page_queue_remove(&queue,
								pages[3]),
				     true);
	failures += test_expect_ptr("queue tail moved", queue.tail,
				    pages[2]);
	failures += test_expect_ptr("queue tail next clear",
				    pages[2]->queue_next, NULL);

	popped = plane_vm_page_queue_pop_head(&queue);
	failures += test_expect_ptr("queue pop remaining", popped, pages[2]);
	failures += test_expect_u64("queue pop empty count",
				    plane_vm_page_queue_count(&queue), 0);
	failures += test_expect_ptr("queue pop empty head", queue.head, NULL);
	failures += test_expect_ptr("queue pop empty tail", queue.tail, NULL);
	failures += test_expect_int("queue popped state clear",
				    plane_vm_page_queue_state(pages[2]),
				    PLANE_VM_PAGE_QUEUE_NONE);
	failures += test_expect_null("queue pop empty",
				     plane_vm_page_queue_pop_head(&queue));
	failures += test_expect_bool("queue reject unqueued remove",
				     plane_vm_page_queue_remove(&queue,
								pages[1]),
				     false);

	failures += test_expect_bool("queue insert wrong target",
				     plane_vm_page_queue_insert_ordered(
					     &queue, pages[1]),
				     true);
	failures += test_expect_bool("queue insert other queue",
				     plane_vm_page_queue_insert_ordered(
					     &wrong_queue, pages[0]),
				     true);
	failures += test_expect_bool("queue reject wrong queue remove",
				     plane_vm_page_queue_remove(&wrong_queue,
								pages[1]),
				     false);
	failures += test_expect_bool("queue remove right queue",
				     plane_vm_page_queue_remove(&queue,
								pages[1]),
				     true);
	failures += test_expect_bool("queue remove other queue",
				     plane_vm_page_queue_remove(&wrong_queue,
								pages[0]),
				     true);

	for (uint64_t i = 0; i < TEST_ARRAY_SIZE(pages); i++) {
		failures += test_expect_bool("queue remove cleanup free",
					     plane_pmm_free_page_phys(phys[i]),
					     true);
	}

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
				    test_page_phys_raw(page), 0x2000);
	failures += check_page_state("grabbed vm page state",
				     plane_vm_page_state(page),
				     PLANE_VM_PAGE_ALLOCATED);

	stats = plane_pmm_get_stats();
	failures += test_expect_u64("vm page grabbed free pages",
				    stats.allocator.free_pages, 1);

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
	failures += test_expect_u64("vm page released free pages",
				    stats.allocator.free_pages, 2);

	return failures;
}

static int test_vm_page_release_drops_page_lock_before_pmm_free(void)
{
	struct plane_mem_info mem = {0};
	struct plane_page *page;
	int failures = 0;

	add_region(&mem, 0x1000, 0x3000, PLANE_MEM_USABLE);
	failures += test_expect_bool("release lock pmm init",
				     plane_pmm_init(&mem), true);
	failures += test_expect_bool("release lock grab",
				     plane_vm_page_grab(0, &page), true);

	test_spinlock_stub_reset_counts();
	failures += test_expect_bool("release lock release",
				     plane_vm_page_release(page), true);
	failures += check_spinlock_depth("release lock", 2, 0);
	failures += check_page_state("release lock state",
				     plane_vm_page_state(page),
				     PLANE_VM_PAGE_FREE);
	return failures;
}

static int test_vm_page_hold_blocks_release_until_unheld(void)
{
	struct plane_mem_info mem = {0};
	struct plane_page *page;
	uint64_t hold_count = UINT64_MAX;
	int failures = 0;

	add_region(&mem, 0x1000, 0x3000, PLANE_MEM_USABLE);
	failures += test_expect_bool("hold pmm init", plane_pmm_init(&mem),
				     true);
	failures += test_expect_bool("hold grab", plane_vm_page_grab(0, &page),
				     true);
	failures += test_expect_bool("hold initial count",
				     plane_vm_page_hold_count(page,
							      &hold_count),
				     true);
	failures += test_expect_u64("hold initial count value",
				    hold_count, 0);
	failures += test_expect_bool("hold page", plane_vm_page_hold(page),
				     true);
	failures += test_expect_bool("hold count",
				     plane_vm_page_hold_count(page,
							      &hold_count),
				     true);
	failures += test_expect_u64("hold count value", hold_count, 1);
	failures += test_expect_bool("held release rejected",
				     plane_vm_page_release(page), false);
	failures += test_expect_bool("unhold page", plane_vm_page_unhold(page),
				     true);
	failures += test_expect_bool("unhold zero rejected",
				     plane_vm_page_unhold(page), false);
	failures += test_expect_bool("release unheld page",
				     plane_vm_page_release(page), true);

	return failures;
}

static int test_vm_page_hold_rejects_invalid_pages_and_overflow(void)
{
	struct plane_mem_info mem = {0};
	struct plane_page *page;
	uint64_t hold_count;
	int failures = 0;

	add_region(&mem, 0x1000, 0x3000, PLANE_MEM_USABLE);
	failures += test_expect_bool("hold invalid init",
				     plane_pmm_init(&mem), true);
	page = plane_vm_page_from_phys(test_paddr(0x1000));
	failures += test_expect_bool("hold rejects free",
				     plane_vm_page_hold(page), false);
	failures += test_expect_bool("hold rejects null",
				     plane_vm_page_hold(NULL), false);
	failures += test_expect_bool("unhold rejects null",
				     plane_vm_page_unhold(NULL), false);
	failures += test_expect_bool("hold count rejects null page",
				     plane_vm_page_hold_count(NULL,
							      &hold_count),
				     false);
	failures += test_expect_bool("hold count rejects null out",
				     plane_vm_page_hold_count(page, NULL),
				     false);
	failures += test_expect_bool("hold alloc",
				     plane_vm_page_grab(0, &page), true);
	page->hold_count = UINT64_MAX;
	failures += test_expect_bool("hold overflow rejected",
				     plane_vm_page_hold(page), false);
	page->hold_count = 0;
	failures += test_expect_bool("hold cleanup release",
				     plane_vm_page_release(page), true);

	return failures;
}

static int test_vm_page_zero_grab_clears_page(void)
{
	struct plane_mem_info mem = {0};
	struct plane_page *page;
	int failures = 0;

	add_region(&mem, 0x1000, 0x3000, PLANE_MEM_USABLE);
	failures += test_expect_bool("zero page init", plane_pmm_init(&mem),
				     true);

	memset(&physmap_storage[0x2000], 0xa5, PAGE_SIZE);
	failures += test_expect_bool("zero page alloc",
				     plane_vm_page_grab(PLANE_VM_PAGE_GRAB_ZERO,
							&page),
				     true);
	failures += test_expect_not_null("zero page metadata", page);
	failures += test_expect_u64("zero page phys", test_page_phys_raw(page),
				    0x2000);
	failures += check_phys_bytes("zero page cleared", 0x2000, 0,
				     PAGE_SIZE);

	return failures;
}

static int test_vm_page_wire_count_tracks_allocated_pages(void)
{
	struct plane_mem_info mem = {0};
	struct plane_pmm_stats stats;
	struct plane_page *page;
	plane_paddr_t phys;
	uint64_t wire_count;
	int failures = 0;

	add_region(&mem, 0x1000, 0x3000, PLANE_MEM_USABLE);
	failures += test_expect_bool("wire init", plane_pmm_init(&mem), true);
	failures += test_expect_bool("wire alloc",
				     plane_pmm_alloc_page_phys(&phys), true);
	failures += test_expect_u64("wire alloc phys", test_paddr_raw(phys),
				    0x2000);
	page = plane_vm_page_from_phys(phys);
	failures += test_expect_bool("wire initial count query",
				     plane_vm_page_wire_count(page, &wire_count),
				     true);
	failures += test_expect_u64("wire initial count", wire_count, 0);

	failures += test_expect_bool("wire page", plane_vm_page_wire(page),
				     true);
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
	failures += test_expect_u64("wire stats wired pages",
				    stats.allocator.wired_pages, 1);
	failures += test_expect_bool("free wired rejected",
				     plane_pmm_free_page_phys(phys), false);
	failures += test_expect_bool("release wired rejected",
				     plane_vm_page_release(page), false);

	failures += test_expect_bool("unwire page", plane_vm_page_unwire(page),
				     true);
	failures += test_expect_bool("wire count one after unwire query",
				     plane_vm_page_wire_count(page, &wire_count),
				     true);
	failures += test_expect_u64("wire count one after unwire", wire_count,
				    1);
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

	return failures;
}

static int test_vm_page_wire_rejects_invalid_pages(void)
{
	struct plane_mem_info mem = {0};
	struct plane_page *free_page;
	struct plane_page *metadata_page;
	plane_paddr_t phys;
	uint64_t wire_count;
	int failures = 0;

	add_region(&mem, 0x1000, 0x3000, PLANE_MEM_USABLE);
	failures += test_expect_bool("wire invalid init",
				     plane_pmm_init(&mem), true);
	metadata_page = plane_vm_page_from_phys(test_paddr(0x1000));
	free_page = plane_vm_page_from_phys(test_paddr(0x2000));
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
	failures += test_expect_u64("wire invalid phys", test_paddr_raw(phys),
				    0x2000);
	failures += test_expect_bool("wire invalid free unwired",
				     plane_pmm_free_page_phys(phys), true);
	return failures;
}

static int test_vm_page_guard_metadata(void)
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
				    test_page_phys_raw(guard),
				    PLANE_VM_PAGE_GUARD_PHYS_RAW);
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
	failures += test_expect_bool("guard hold",
				     plane_vm_page_hold(guard), true);
	failures += test_expect_bool("held guard release rejected",
				     plane_vm_page_release_guard(guard), false);
	failures += test_expect_bool("guard unhold",
				     plane_vm_page_unhold(guard), true);
	failures += test_expect_bool("guard attach",
				     plane_vm_page_attach_object(guard,
								 &object, 0),
				     true);
	failures += test_expect_bool("guard attached release rejected",
				     plane_vm_page_release_guard(guard), false);
	failures += test_expect_bool("guard detach",
				     plane_vm_page_detach_object(guard,
								 &object, 0),
				     true);
	failures += test_expect_bool("guard release",
				     plane_vm_page_release_guard(guard), true);
	failures += check_page_state("guard released state",
				     plane_vm_page_state(guard),
				     PLANE_VM_PAGE_INVALID);
	failures += test_expect_u64("guard released phys",
				    test_page_phys_raw(guard),
				    PLANE_VM_PAGE_NO_PHYS_RAW);
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

static int test_vm_page_guard_operations_take_page_lock(void)
{
	struct plane_mem_info mem = {0};
	struct plane_page *guard;
	uint64_t storage_size = 0;
	int failures = 0;

	add_region(&mem, 0x1000, 0x3000, PLANE_MEM_USABLE);
	failures += test_expect_bool("guard lock pmm init",
				     plane_pmm_init(&mem), true);

	test_spinlock_stub_reset_counts();
	guard = plane_vm_page_create_guard();
	failures += test_expect_not_null("guard lock create", guard);
	failures += check_spinlock_depth("guard create lock", 1, 0);

	test_spinlock_stub_reset_counts();
	failures += test_expect_bool("guard lock release",
				     plane_vm_page_release_guard(guard), true);
	failures += check_spinlock_depth("guard release lock", 1, 0);

	failures += test_expect_bool("guard lock storage size",
				     plane_vm_page_guard_storage_size(
					     TEST_GUARD_EXTRA_COUNT,
					     &storage_size),
				     true);
	failures += test_expect_bool("guard lock storage fits",
				     storage_size <= sizeof(extra_guard_storage),
				     true);
	test_spinlock_stub_reset_counts();
	failures += test_expect_bool("guard lock add storage",
				     plane_vm_page_add_guard_storage(
					     extra_guard_storage,
					     TEST_GUARD_EXTRA_COUNT,
					     &extra_guard_segment),
				     true);
	failures += check_spinlock_depth("guard add storage lock", 1, 0);
	return failures;
}

static int test_vm_page_guard_zone_extends_bootstrap_storage(void)
{
	struct plane_mem_info mem = {0};
	struct plane_pmm_stats before;
	struct plane_pmm_stats after;
	struct plane_page *guards[TEST_GUARD_BOOTSTRAP_COUNT +
				 TEST_GUARD_EXTRA_COUNT];
	struct plane_page *reused;
	struct plane_page *extra;
	uint64_t storage_size = 0;
	int failures = 0;

	for (uint64_t i = 0; i < TEST_ARRAY_SIZE(guards); i++) {
		guards[i] = NULL;
	}

	add_region(&mem, 0x1000, 0x3000, PLANE_MEM_USABLE);
	failures += test_expect_bool("guard zone pmm init",
				     plane_pmm_init(&mem), true);
	before = plane_pmm_get_stats();

	for (uint64_t i = 0; i < TEST_GUARD_BOOTSTRAP_COUNT; i++) {
		guards[i] = plane_vm_page_create_guard();
		if (guards[i] == NULL) {
			test_fail("bootstrap guard alloc %llu",
				  (unsigned long long)i);
			failures++;
		}
	}

	failures += test_expect_null("bootstrap guard exhausted",
				     plane_vm_page_create_guard());
	failures += test_expect_bool("release bootstrap guard",
				     plane_vm_page_release_guard(guards[0]),
				     true);
	reused = plane_vm_page_create_guard();
	failures += test_expect_ptr("bootstrap guard reuses slot",
				    reused, guards[0]);
	guards[0] = reused;
	failures += test_expect_null("bootstrap guard exhausted again",
				     plane_vm_page_create_guard());

	failures += test_expect_bool("guard storage size",
				     plane_vm_page_guard_storage_size(
					     TEST_GUARD_EXTRA_COUNT,
					     &storage_size),
				     true);
	failures += test_expect_bool("guard storage fits test page",
				     storage_size <= sizeof(extra_guard_storage),
				     true);
	failures += test_expect_bool("add guard storage",
				     plane_vm_page_add_guard_storage(
					     extra_guard_storage,
					     TEST_GUARD_EXTRA_COUNT,
					     &extra_guard_segment),
				     true);

	for (uint64_t i = 0; i < TEST_GUARD_EXTRA_COUNT; i++) {
		extra = plane_vm_page_create_guard();
		guards[TEST_GUARD_BOOTSTRAP_COUNT + i] = extra;
		if (extra == NULL) {
			test_fail("extra guard alloc %llu",
				  (unsigned long long)i);
			failures++;
		}
	}

	failures += test_expect_null("expanded guard exhausted",
				     plane_vm_page_create_guard());
	failures += test_expect_bool("reject duplicate storage segment",
				     plane_vm_page_add_guard_storage(
					     extra_guard_storage,
					     TEST_GUARD_EXTRA_COUNT,
					     &extra_guard_segment),
				     false);
	failures += test_expect_bool("reject zero guard storage size",
				     plane_vm_page_guard_storage_size(
					     0, &storage_size),
				     false);
	failures += test_expect_bool("reject null guard storage out",
				     plane_vm_page_guard_storage_size(
					     TEST_GUARD_EXTRA_COUNT, NULL),
				     false);

	for (uint64_t i = 0; i < TEST_ARRAY_SIZE(guards); i++) {
		if (guards[i] != NULL &&
		    !plane_vm_page_release_guard(guards[i])) {
			test_fail("guard release %llu",
				  (unsigned long long)i);
			failures++;
		}
	}

	after = plane_pmm_get_stats();
	failures += test_expect_u64("expanded guard managed unchanged",
				    after.allocator.managed_pages,
				    before.allocator.managed_pages);
	failures += test_expect_u64("expanded guard free unchanged",
				    after.allocator.free_pages,
				    before.allocator.free_pages);
	failures += test_expect_u64("expanded guard wired unchanged",
				    after.allocator.wired_pages,
				    before.allocator.wired_pages);
	return failures;
}

static int test_vm_page_object_identity_blocks_free(void)
{
	struct plane_mem_info mem = {0};
	struct plane_vm_object object = {0};
	struct plane_page *page;
	uint64_t offset = 0;
	plane_paddr_t phys;
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
	test_spinlock_stub_reset_counts();
	failures += test_expect_bool("object identity wire",
				     plane_vm_page_wire(page), true);
	failures += check_spinlock_depth("object identity wire lock", 1, 0);
	failures += test_expect_u64("object identity wired once",
				    plane_vm_object_wired_page_count(&object),
				    1);
	test_spinlock_stub_reset_counts();
	failures += test_expect_bool("object identity wire twice",
				     plane_vm_page_wire(page), true);
	failures += check_spinlock_depth("object identity wire twice lock", 1,
					 0);
	failures += test_expect_u64("object identity wired count stable",
				    plane_vm_object_wired_page_count(&object),
				    1);
	test_spinlock_stub_reset_counts();
	failures += test_expect_bool("object identity unwire once",
				     plane_vm_page_unwire(page), true);
	failures += check_spinlock_depth("object identity unwire lock", 1, 0);
	failures += test_expect_u64("object identity still wired",
				    plane_vm_object_wired_page_count(&object),
				    1);
	test_spinlock_stub_reset_counts();
	failures += test_expect_bool("object identity unwire twice",
				     plane_vm_page_unwire(page), true);
	failures += check_spinlock_depth("object identity unwire twice lock",
					 1, 0);
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

int main(void)
{
	static const struct test_case cases[] = {
		TEST_CASE(test_vm_page_from_phys_metadata),
		TEST_CASE(test_vm_page_queue_insert_orders_by_phys),
		TEST_CASE(test_vm_page_queue_remove_and_pop_clear_membership),
		TEST_CASE(test_vm_page_grab_allocates_and_releases_metadata),
		TEST_CASE(test_vm_page_release_drops_page_lock_before_pmm_free),
		TEST_CASE(test_vm_page_hold_blocks_release_until_unheld),
		TEST_CASE(test_vm_page_hold_rejects_invalid_pages_and_overflow),
		TEST_CASE(test_vm_page_zero_grab_clears_page),
		TEST_CASE(test_vm_page_wire_count_tracks_allocated_pages),
		TEST_CASE(test_vm_page_wire_rejects_invalid_pages),
		TEST_CASE(test_vm_page_guard_metadata),
		TEST_CASE(test_vm_page_guard_operations_take_page_lock),
		TEST_CASE(test_vm_page_guard_zone_extends_bootstrap_storage),
		TEST_CASE(test_vm_page_object_identity_blocks_free),
	};

	return test_run_cases_with_fixture("vm_page_test", cases,
					   TEST_ARRAY_SIZE(cases),
					   reset_physmap_stub, NULL);
}
