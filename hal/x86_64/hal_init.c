#include <hal/hal.h>
#include <hal/x86_64/cpu_features.h>
#include <hal/x86_64/gdt.h>
#include <hal/x86_64/idt.h>

void hal_arch_early_init(void) {
	x86_64_cpu_features_init();

	x86_64_gdt_init();

	x86_64_idt_init();
}
