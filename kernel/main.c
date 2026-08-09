#include <plane/boot_info.h>
#include <plane/framebuffer.h>
#include <plane/entry.h>
#include <plane/io_map.h>
#include <plane/kmem.h>
#include <plane/printk.h>
#include <plane/pmm.h>
#include <plane/smp.h>
#include <machine/local_interrupt.h>
#include <machine/pmap.h>
#include <machine/serial.h>
#include <machine/machine_routines.h>

#include "smp_internal.h"

void kmain(struct plane_boot_info *info)
{
	serial_init();
	BUG_ON_MSG(!ml_startup_init(),
		   "failed to initialize architecture startup runtime");
	BUG_ON_MSG(!plane_smp_init_bsp(&info->smp),
		   "failed to initialize BSP SMP topology");
	pr_info("SMP: cpus=%u bsp=%u\n",
		plane_cpu_count(), plane_cpu_current_id());

	BUG_ON_MSG(!plane_memmap_sanitize(&info->mem),
		   "failed to sanitize memory map handoff");
	BUG_ON_MSG(!physmap_enable(&info->mem),
		   "failed to enable kernel physmap");
	BUG_ON_MSG(!plane_pmm_init(&info->mem),
		   "failed to initialize physical memory manager");
	if (info->release_framebuffer_bootstrap_mapping != NULL) {
		BUG_ON_MSG(!info->release_framebuffer_bootstrap_mapping(
				   info->framebuffer.framebuffer_addr,
				   info->framebuffer.framebuffer_size),
			   "failed to release framebuffer bootstrap mapping");
	}
	BUG_ON_MSG(!pmap_take_kernel_page_table_ownership(),
		   "failed to initialize kernel page tables");
	BUG_ON_MSG(!plane_kmem_init(),
		   "failed to initialize kernel memory allocator");
	BUG_ON_MSG(!plane_io_map_init(),
		   "failed to initialize kernel IO mapper");
	BUG_ON_MSG(!plane_framebuffer_remap(&info->framebuffer),
		   "failed to remap framebuffer through IO map");
	BUG_ON_MSG(!ml_local_interrupt_init_bsp(&info->smp),
		   "failed to initialize BSP local interrupts");
	plane_pmm_log_stats();

	BUG_ON_MSG(!plane_smp_startup_prepare_ap_stacks(),
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
	BUG_ON_MSG(!plane_framebuffer_draw_test_pattern(&info->framebuffer),
		   "failed to draw framebuffer test pattern");

	pr_info("Kernel initialization completed. System halted.\n");
}
