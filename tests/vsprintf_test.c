#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <klib/stdio.h>

static int format(char *buf, size_t size, const char *fmt, ...)
{
	va_list args;
	int ret;

	va_start(args, fmt);
	ret = vsnprintf(buf, size, fmt, args);
	va_end(args);
	return ret;
}

static int expect_string(const char *name, const char *actual,
			 const char *expected)
{
	if (strcmp(actual, expected) == 0) {
		return 0;
	}

	printf("FAIL: %s expected=\"%s\" actual=\"%s\"\n", name, expected,
	       actual);
	return 1;
}

static int expect_int(const char *name, int actual, int expected)
{
	if (actual == expected) {
		return 0;
	}

	printf("FAIL: %s expected=%d actual=%d\n", name, expected, actual);
	return 1;
}

static int test_size_length_modifier(void)
{
	char buf[64];
	int failures = 0;
	int ret;

	ret = format(buf, sizeof(buf), "%zu", (size_t)12345);
	failures += expect_string("%zu", buf, "12345");
	failures += expect_int("%zu ret", ret, 5);

	ret = format(buf, sizeof(buf), "%zx", (size_t)0x1abc);
	failures += expect_string("%zx", buf, "1abc");
	failures += expect_int("%zx ret", ret, 4);

	ret = format(buf, sizeof(buf), "%zX", (size_t)0x1abc);
	failures += expect_string("%zX", buf, "1ABC");
	failures += expect_int("%zX ret", ret, 4);

	ret = format(buf, sizeof(buf), "%zd", (intptr_t)-42);
	failures += expect_string("%zd", buf, "-42");
	failures += expect_int("%zd ret", ret, 3);

	ret = format(buf, sizeof(buf), "%zi", (intptr_t)-7);
	failures += expect_string("%zi", buf, "-7");
	failures += expect_int("%zi ret", ret, 2);

	ret = format(buf, sizeof(buf), "%016zx", (size_t)0x2a);
	failures += expect_string("%016zx", buf, "000000000000002a");
	failures += expect_int("%016zx ret", ret, 16);

	return failures;
}

static int test_existing_formats(void)
{
	char buf[64];
	char trunc_source[] = {'a', 'b', 'c', 'd', 'e', 'f', '\0'};
	int failures = 0;
	int ret;

	ret = format(buf, sizeof(buf), "%016llx", 0x1234ull);
	failures += expect_string("%016llx", buf, "0000000000001234");
	failures += expect_int("%016llx ret", ret, 16);

	ret = format(buf, sizeof(buf), "%p", (void *)(uintptr_t)0x1234);
	failures += expect_string("%p", buf, "0x0000000000001234");
	failures += expect_int("%p ret", ret, 18);

	ret = format(buf, 5, "%s", trunc_source);
	failures += expect_string("truncate", buf, "abcd");
	failures += expect_int("truncate ret", ret, 6);

	return failures;
}

int main(void)
{
	int failures = 0;

	failures += test_size_length_modifier();
	failures += test_existing_formats();

	if (failures != 0) {
		return 1;
	}

	printf("vsprintf_test: ok\n");
	return 0;
}
