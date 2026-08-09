#include <stdarg.h>

#include <klib/stdio.h>
#include <machine/serial.h>
#include <machine/machine_routines.h>
#include <plane/printk.h>
#include <plane/spinlock.h>

#define PRINTK_BUF_SIZE 1024

static struct plane_spinlock printk_lock = PLANE_SPINLOCK_INIT;

static void printk_raw_puts(const char *buf, int len)
{
	for (int i = 0; i < len && buf[i] != '\0'; i++) {
		serial_putchar(buf[i]);
	}
}

void printk(const char *fmt, ...)
{
	char buf[PRINTK_BUF_SIZE];
	va_list args;
	
	va_start(args, fmt);
	int len = vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	plane_irq_state_t irq_state = plane_spin_lock_irqsave(&printk_lock);
	printk_raw_puts(buf, len);
	plane_spin_unlock_irqrestore(&printk_lock, irq_state);
}

void panic(const char *fmt, ...)
{
	char buf[PRINTK_BUF_SIZE];
	va_list args;

	ml_interrupts_disable();
	bool locked = plane_spin_try_lock(&printk_lock);
	
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	
	printk_raw_puts("[PANIC] ", 8);
	printk_raw_puts(buf, PRINTK_BUF_SIZE);
	printk_raw_puts("\n", 1);

	if (locked) {
		plane_spin_unlock(&printk_lock);
	}
	
	ml_cpu_halt();
}
