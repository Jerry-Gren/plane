#include <stdarg.h>

#include "printk_stubs.h"

void printk(const char *fmt, ...)
{
	va_list args;

	(void)fmt;
	va_start(args, fmt);
	va_end(args);
}

void panic(const char *fmt, ...)
{
	va_list args;

	(void)fmt;
	va_start(args, fmt);
	va_end(args);

	__builtin_trap();
	__builtin_unreachable();
}
