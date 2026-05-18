#include <hal/cpu.h>

void hal_cpu_hang(void) {
	for (;;) {
		__asm__ volatile ("cli; hlt");
	}
}