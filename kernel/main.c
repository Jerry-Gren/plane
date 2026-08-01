#include <plane/boot_info.h>
#include <plane/early_video.h>
#include <plane/entry.h>
#include <plane/kmem.h>
#include <plane/printk.h>
#include <plane/pmm.h>
#include <plane/smp.h>
#include <hal/local_interrupt.h>
#include <hal/mmu.h>
#include <hal/serial.h>
#include <hal/hal.h>

#include "smp_internal.h"

void kmain(struct boot_info *info)
{
	hal_serial_init();
	hal_arch_early_init();
	BUG_ON_MSG(!plane_smp_init_bsp(&info->smp),
		   "failed to initialize BSP SMP topology");
	pr_info("SMP: cpus=%u bsp=%u\n",
		plane_cpu_count(), plane_cpu_current_id());

	BUG_ON_MSG(!plane_sanitize_memory_map(&info->mem),
		   "failed to sanitize boot memory map");
	BUG_ON_MSG(!hal_mmu_enable_direct_map(&info->mem),
		   "failed to enable kernel direct map");
	BUG_ON_MSG(!plane_pmm_init(&info->mem),
		   "failed to initialize physical memory manager");
	BUG_ON_MSG(!hal_mmu_take_kernel_page_table_ownership(),
		   "failed to initialize kernel page tables");
	/*
	 * Keep LAPIC init after PMM owns page-table allocation: the current
	 * xAPIC path may install a transitional MMIO PTE until Plane grows a
	 * real kernel IO-map API.
	 */
	BUG_ON_MSG(!hal_local_interrupt_init_bsp(&info->smp),
		   "failed to initialize BSP local interrupts");
	BUG_ON_MSG(!plane_kmem_init(),
		   "failed to initialize kernel memory allocator");
	plane_pmm_log_stats();

	BUG_ON_MSG(!plane_smp_prepare_ap_stacks(),
		   "failed to prepare AP startup stacks");
	if (info->start_aps != NULL) {
		BUG_ON_MSG(!info->start_aps(),
			   "failed to start AP park bringup");
		pr_info("SMP: parked APs=%u\n", plane_cpu_parked_count());
	}

	/*
	 * TODO:
	 * vmm_init();
	 */
	BUG_ON_MSG(!plane_early_video_draw_test_pattern(&info->video),
		   "failed to draw early framebuffer test pattern");

	pr_info("Kernel initialization completed. System halted.\n");
}
