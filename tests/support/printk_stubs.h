#ifndef TEST_SUPPORT_PRINTK_STUBS_H
#define TEST_SUPPORT_PRINTK_STUBS_H

#include <stdarg.h>

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

#endif /* TEST_SUPPORT_PRINTK_STUBS_H */
