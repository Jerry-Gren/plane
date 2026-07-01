#include <stdint.h>
#include <stddef.h>
#include <plane/boot_info.h>
#include <plane/early_video.h>
#include <plane/kernel.h>
#include <hal/cpu.h>
#include <hal/serial.h>
#include <hal/hal.h>

void kmain(struct boot_info *info) {
	hal_serial_init();
	hal_arch_early_init();

	if (!plane_sanitize_memory_map(&info->mem)) {
		hal_cpu_hang();
	}
	
	/* 
	 * TODO: 
	 * pmm_init(&info->mem); 
	 * vmm_init();
	 */
	if (!plane_early_video_draw_test_pattern(&info->video)) {
		hal_cpu_hang();
	}

	pr_info("Kernel initialization completed. System halted.\n");
}
