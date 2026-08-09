#include <machine/machine_routines.h>
#include <x86_64/cpu_features.h>
#include <x86_64/gdt.h>
#include <x86_64/idt.h>
#include <x86_64/pat.h>

bool ml_startup_init(void)
{
	if (!x86_64_cpu_features_init()) {
		return false;
	}

	if (!x86_64_pat_init()) {
		return false;
	}

	x86_64_gdt_init();

	x86_64_idt_init();
	return true;
}
