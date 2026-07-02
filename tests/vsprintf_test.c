#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

#include <klib/stdio.h>

#include "support/test.h"

static int format(char *buf, size_t size, const char *fmt, ...)
{
	va_list args;
	int ret;

	va_start(args, fmt);
	ret = vsnprintf(buf, size, fmt, args);
	va_end(args);
	return ret;
}

static int test_size_length_modifier(void)
{
	char buf[64];
	int failures = 0;
	int ret;

	ret = format(buf, sizeof(buf), "%zu", (size_t)12345);
	failures += test_expect_str("%zu", buf, "12345");
	failures += test_expect_int("%zu ret", ret, 5);

	ret = format(buf, sizeof(buf), "%zx", (size_t)0x1abc);
	failures += test_expect_str("%zx", buf, "1abc");
	failures += test_expect_int("%zx ret", ret, 4);

	ret = format(buf, sizeof(buf), "%zX", (size_t)0x1abc);
	failures += test_expect_str("%zX", buf, "1ABC");
	failures += test_expect_int("%zX ret", ret, 4);

	ret = format(buf, sizeof(buf), "%zd", (intptr_t)-42);
	failures += test_expect_str("%zd", buf, "-42");
	failures += test_expect_int("%zd ret", ret, 3);

	ret = format(buf, sizeof(buf), "%zi", (intptr_t)-7);
	failures += test_expect_str("%zi", buf, "-7");
	failures += test_expect_int("%zi ret", ret, 2);

	ret = format(buf, sizeof(buf), "%016zx", (size_t)0x2a);
	failures += test_expect_str("%016zx", buf, "000000000000002a");
	failures += test_expect_int("%016zx ret", ret, 16);

	return failures;
}

static int test_existing_formats(void)
{
	char buf[64];
	char trunc_source[] = {'a', 'b', 'c', 'd', 'e', 'f', '\0'};
	int failures = 0;
	int ret;

	ret = format(buf, sizeof(buf), "%016llx", 0x1234ull);
	failures += test_expect_str("%016llx", buf, "0000000000001234");
	failures += test_expect_int("%016llx ret", ret, 16);

	ret = format(buf, sizeof(buf), "%p", (void *)(uintptr_t)0x1234);
	failures += test_expect_str("%p", buf, "0x0000000000001234");
	failures += test_expect_int("%p ret", ret, 18);

	ret = format(buf, 5, "%s", trunc_source);
	failures += test_expect_str("truncate", buf, "abcd");
	failures += test_expect_int("truncate ret", ret, 6);

	return failures;
}

int main(void)
{
	static const struct test_case cases[] = {
		TEST_CASE(test_size_length_modifier),
		TEST_CASE(test_existing_formats),
	};

	return test_run_cases("vsprintf_test",
			      cases, TEST_ARRAY_SIZE(cases));
}
