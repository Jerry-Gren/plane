#ifndef TEST_SUPPORT_TEST_H
#define TEST_SUPPORT_TEST_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static inline int test_expect_bool(const char *name, bool actual, bool expected)
{
	if (actual == expected) {
		return 0;
	}

	printf("FAIL: %s expected=%d actual=%d\n", name, expected, actual);
	return 1;
}

static inline int test_expect_int(const char *name, int actual, int expected)
{
	if (actual == expected) {
		return 0;
	}

	printf("FAIL: %s expected=%d actual=%d\n", name, expected, actual);
	return 1;
}

static inline int test_expect_u32(const char *name, uint32_t actual, uint32_t expected)
{
	if (actual == expected) {
		return 0;
	}

	printf("FAIL: %s expected=0x%08x actual=0x%08x\n",
	       name, expected, actual);
	return 1;
}

static inline int test_expect_u64(const char *name, uint64_t actual, uint64_t expected)
{
	if (actual == expected) {
		return 0;
	}

	printf("FAIL: %s expected=0x%016llx actual=0x%016llx\n",
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

	printf("FAIL: %s expected=%p actual=%p\n", name, expected, actual);
	return 1;
}

static inline int test_expect_null(const char *name, const void *actual)
{
	if (actual == NULL) {
		return 0;
	}

	printf("FAIL: %s expected=NULL actual=%p\n", name, actual);
	return 1;
}

static inline int test_expect_not_null(const char *name, const void *actual)
{
	if (actual != NULL) {
		return 0;
	}

	printf("FAIL: %s expected=non-NULL actual=NULL\n", name);
	return 1;
}

static inline int test_expect_str(const char *name,
				  const char *actual,
				  const char *expected)
{
	if (strcmp(actual, expected) == 0) {
		return 0;
	}

	printf("FAIL: %s expected=\"%s\" actual=\"%s\"\n",
	       name, expected, actual);
	return 1;
}

#define TEST_RUN(failures, fn) do { \
	(failures) += (fn)();       \
} while (0)

#endif /* TEST_SUPPORT_TEST_H */
