#include <hal/hal.h>
#include <hal/x86_64/gdt.h>
#include <plane/kernel.h>

void hal_arch_early_init(void) {
	hal_gdt_init();

	/* hal_idt_init(); */

	/* hal_cpu_features_init(); */
}