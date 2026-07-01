#include <stdint.h>

#include <plane/boot_info.h>
#include <plane/early_video.h>
#include <plane/entry.h>
#include <plane/printk.h>
#include <plane/pmm.h>
#include <hal/serial.h>
#include <hal/hal.h>

static const char *mem_type_name(uint32_t type)
{
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
		return "UNKNOWN";
	}
}

static void dump_boot_memmap(const struct plane_mem_info *mem)
{
	for (uint64_t i = 0; i < mem->entry_count; i++) {
		const struct plane_mem_region *region = &mem->map[i];

		pr_info("mem[%llu]: base=0x%016llx length=0x%016llx type=%s\n",
			(unsigned long long)i,
			(unsigned long long)region->base,
			(unsigned long long)region->length,
			mem_type_name(region->type));
	}
}

void kmain(struct boot_info *info) {
	hal_serial_init();
	hal_arch_early_init();

	BUG_ON_MSG(!plane_sanitize_memory_map(&info->mem),
		   "failed to sanitize boot memory map");
	dump_boot_memmap(&info->mem);
	BUG_ON_MSG(!plane_pmm_init(&info->mem),
		   "failed to initialize physical memory manager");
	struct plane_pmm_stats pmm_stats = plane_pmm_get_stats();
	pr_info("PMM: managed=%llu free=%llu allocated=%llu usable=%llu "
		"invalid=%llu reserved=%llu acpi_reclaimable=%llu "
		"acpi_nvs=%llu bootloader=%llu exec_modules=%llu "
		"framebuffer=%llu bad=%llu reserved_mapped=%llu "
		"ranges=%llu\n",
		(unsigned long long)pmm_stats.managed_pages,
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
