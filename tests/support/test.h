#ifndef TEST_SUPPORT_TEST_H
#define TEST_SUPPORT_TEST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

struct test_case {
	const char *name;
	int (*fn)(void);
};

static inline void test_fail(const char *fmt, ...)
{
	va_list args;

	printf("FAIL: ");
	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);
	printf("\n");
}

static inline int test_expect_bool(const char *name, bool actual, bool expected)
{
	if (actual == expected) {
		return 0;
	}

	test_fail("%s expected=%d actual=%d", name, expected, actual);
	return 1;
}

static inline int test_expect_int(const char *name, int actual, int expected)
{
	if (actual == expected) {
		return 0;
	}

	test_fail("%s expected=%d actual=%d", name, expected, actual);
	return 1;
}

static inline int test_expect_u32(const char *name, uint32_t actual, uint32_t expected)
{
	if (actual == expected) {
		return 0;
	}

	test_fail("%s expected=0x%08x actual=0x%08x", name, expected, actual);
	return 1;
}

static inline int test_expect_u64(const char *name, uint64_t actual, uint64_t expected)
{
	if (actual == expected) {
		return 0;
	}

	test_fail("%s expected=0x%016llx actual=0x%016llx",
		  name, (unsigned long long)expected, (unsigned long long)actual);
	return 1;
}

static inline int test_expect_ptr(const char *name,
				  const void *actual,
				  const void *expected)
{
	if (actual == expected) {
		return 0;
	}

	test_fail("%s expected=%p actual=%p", name, expected, actual);
	return 1;
}

static inline int test_expect_null(const char *name, const void *actual)
{
	if (actual == NULL) {
		return 0;
	}

	test_fail("%s expected=NULL actual=%p", name, actual);
	return 1;
}

static inline int test_expect_not_null(const char *name, const void *actual)
{
	if (actual != NULL) {
		return 0;
	}

	test_fail("%s expected=non-NULL actual=NULL", name);
	return 1;
}

static inline int test_expect_str(const char *name,
				  const char *actual,
				  const char *expected)
{
	if (strcmp(actual, expected) == 0) {
		return 0;
	}

	test_fail("%s expected=\"%s\" actual=\"%s\"", name, expected, actual);
	return 1;
}

static inline int test_finish_suite(const char *suite_name, int failures)
{
	if (failures != 0) {
		return 1;
	}

	printf("%s: ok\n", suite_name);
	return 0;
}

static inline int test_run_cases_with_fixture(const char *suite_name,
					      const struct test_case *cases,
					      size_t count,
					      void (*setup)(void),
					      void (*teardown)(void))
{
	int failures = 0;

	for (size_t i = 0; i < count; i++) {
		if (setup != NULL) {
			setup();
		}

		int case_failures = cases[i].fn();

		if (teardown != NULL) {
			teardown();
		}

		if (case_failures != 0) {
			test_fail("%s.%s (%d failures)",
				  suite_name, cases[i].name, case_failures);
			failures += case_failures;
		}
	}

	return test_finish_suite(suite_name, failures);
}

static inline int test_run_cases(const char *suite_name,
				 const struct test_case *cases,
				 size_t count)
{
	return test_run_cases_with_fixture(suite_name, cases, count, NULL, NULL);
}

#define TEST_ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#define TEST_CASE(fn) { #fn, fn }
#define TEST_CASE_NAMED(name, fn) { name, fn }

#endif /* TEST_SUPPORT_TEST_H */
