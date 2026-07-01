#include <plane/boot_info.h>
#include <plane/early_video.h>
#include <plane/entry.h>
#include <plane/printk.h>
#include <hal/serial.h>
#include <hal/hal.h>

void kmain(struct boot_info *info) {
	hal_serial_init();
	hal_arch_early_init();

	BUG_ON_MSG(!plane_sanitize_memory_map(&info->mem),
		   "failed to sanitize boot memory map");

	/*
	 * TODO:
	 * pmm_init(&info->mem);
	 * vmm_init();
	 */
	BUG_ON_MSG(!plane_early_video_draw_test_pattern(&info->video),
		   "failed to draw early framebuffer test pattern");

	pr_info("Kernel initialization completed. System halted.\n");
}
