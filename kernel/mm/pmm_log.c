#include <plane/printk.h>
#include <plane/pmm.h>

void plane_pmm_log_stats(void)
{
	struct plane_pmm_stats stats = plane_pmm_get_stats();
	uint64_t allocated = stats.allocator.managed_pages -
			     stats.allocator.free_pages;

	pr_info("PMM: managed=%llu free=%llu allocated=%llu "
		"metadata_pages=%llu metadata_bytes=%llu ranges=%llu "
		"memtype: usable=%llu invalid=%llu reserved=%llu "
		"acpi_reclaimable=%llu acpi_nvs=%llu bootloader=%llu "
		"exec_modules=%llu framebuffer=%llu bad=%llu "
		"reserved_mapped=%llu\n",
		(unsigned long long)stats.allocator.managed_pages,
		(unsigned long long)stats.allocator.free_pages,
		(unsigned long long)allocated,
		(unsigned long long)stats.allocator.metadata_pages,
		(unsigned long long)stats.allocator.metadata_bytes,
		(unsigned long long)stats.allocator.free_range_count,
		(unsigned long long)stats.memtype.usable_pages,
		(unsigned long long)stats.memtype.invalid_pages,
		(unsigned long long)stats.memtype.reserved_pages,
		(unsigned long long)stats.memtype.acpi_reclaimable_pages,
		(unsigned long long)stats.memtype.acpi_nvs_pages,
		(unsigned long long)stats.memtype.bootloader_reclaimable_pages,
		(unsigned long long)stats.memtype.executable_and_modules_pages,
		(unsigned long long)stats.memtype.framebuffer_pages,
		(unsigned long long)stats.memtype.bad_pages,
		(unsigned long long)stats.memtype.reserved_mapped_pages);
}
