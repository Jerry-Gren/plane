#ifndef KLIB_STDIO_H
#define KLIB_STDIO_H

#include <stdarg.h>
#include <stddef.h>

int vsnprintf(char *str, size_t size, const char *format, va_list ap);
int snprintf(char *str, size_t size, const char *format, ...);

#endif /* KLIB_STDIO_H */