#include <plane/boot_info.h>
#include <plane/early_video.h>
#include <plane/entry.h>
#include <plane/printk.h>
#include <plane/pmm.h>
#include <hal/mmu.h>
#include <hal/serial.h>
#include <hal/hal.h>

void kmain(struct boot_info *info) {
	hal_serial_init();
	hal_arch_early_init();

	BUG_ON_MSG(!plane_sanitize_memory_map(&info->mem),
		   "failed to sanitize boot memory map");
	BUG_ON_MSG(!hal_mmu_enable_direct_map(&info->mem),
		   "failed to enable kernel direct map");
	BUG_ON_MSG(!plane_pmm_init(&info->mem),
		   "failed to initialize physical memory manager");
	struct plane_pmm_stats pmm_stats = plane_pmm_get_stats();
	pr_info("PMM: managed=%llu tracked=%llu free=%llu allocated=%llu usable=%llu "
		"invalid=%llu reserved=%llu acpi_reclaimable=%llu "
		"acpi_nvs=%llu bootloader=%llu exec_modules=%llu "
		"framebuffer=%llu bad=%llu reserved_mapped=%llu "
		"ranges=%llu\n",
		(unsigned long long)pmm_stats.managed_pages,
		(unsigned long long)pmm_stats.tracked_pages,
		(unsigned long long)pmm_stats.free_pages,
		(unsigned long long)pmm_stats.allocated_pages,
		(unsigned long long)pmm_stats.usable_pages,
		(unsigned long long)pmm_stats.invalid_pages,
		(unsigned long long)pmm_stats.reserved_pages,
		(unsigned long long)pmm_stats.acpi_reclaimable_pages,
		(unsigned long long)pmm_stats.acpi_nvs_pages,
		(unsigned long long)pmm_stats.bootloader_reclaimable_pages,
		(unsigned long long)pmm_stats.executable_and_modules_pages,
		(unsigned long long)pmm_stats.framebuffer_pages,
		(unsigned long long)pmm_stats.bad_pages,
		(unsigned long long)pmm_stats.reserved_mapped_pages,
		(unsigned long long)pmm_stats.free_range_count);

	/*
	 * TODO:
	 * vmm_init();
	 */
	BUG_ON_MSG(!plane_early_video_draw_test_pattern(&info->video),
		   "failed to draw early framebuffer test pattern");

	pr_info("Kernel initialization completed. System halted.\n");
}
