#include <stdarg.h>

#include <klib/stdio.h>
#include <hal/serial.h>
#include <hal/cpu.h>

#define PRINTK_BUF_SIZE 1024

/* TODO: spinlock */
void printk(const char *fmt, ...) {
	char buf[PRINTK_BUF_SIZE];
	va_list args;
	
	va_start(args, fmt);
	int len = vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	for (int i = 0; i < len && buf[i] != '\0'; i++) {
		hal_serial_putchar(buf[i]);
	}
}

void panic(const char *fmt, ...) {
	char buf[PRINTK_BUF_SIZE];
	va_list args;
	
	printk("[PANIC] ");
	
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	
	printk("%s\n", buf);
	
	hal_cpu_hang();
}