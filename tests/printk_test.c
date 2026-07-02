#include <setjmp.h>
#include <stdarg.h>

#include <plane/printk.h>

#include "support/test.h"

static int printk_count;
static int panic_count;
static jmp_buf panic_env;

void printk(const char *fmt, ...)
{
	va_list args;

	(void)fmt;
	va_start(args, fmt);
	va_end(args);

	printk_count++;
}

void panic(const char *fmt, ...)
{
	va_list args;

	(void)fmt;
	va_start(args, fmt);
	va_end(args);

	panic_count++;
	longjmp(panic_env, 1);
	__builtin_unreachable();
}

static void reset_counts(void)
{
	printk_count = 0;
	panic_count = 0;
}

static int trigger_warn_once(void)
{
	return WARN_ON_ONCE(true);
}

static int trigger_warn_once_msg(void)
{
	return WARN_ON_ONCE_MSG(true, "once");
}

static int test_warn_macros(void)
{
	int failures = 0;

	reset_counts();
	failures += test_expect_bool("WARN_ON(false)", WARN_ON(false), false);
	failures += test_expect_int("printk after WARN_ON(false)", printk_count, 0);

	reset_counts();
	failures += test_expect_bool("WARN_ON(true)", WARN_ON(true), true);
	failures += test_expect_int("printk after WARN_ON(true)", printk_count, 1);

	reset_counts();
	failures += test_expect_bool("WARN_ON_MSG(true)", WARN_ON_MSG(true, "value=%d", 7), true);
	failures += test_expect_int("printk after WARN_ON_MSG(true)", printk_count, 1);

	reset_counts();
	failures += test_expect_bool("WARN_ON_ONCE(false)", WARN_ON_ONCE(false), false);
	failures += test_expect_int("printk after WARN_ON_ONCE(false)", printk_count, 0);

	reset_counts();
	failures += test_expect_bool("WARN_ON_ONCE first", trigger_warn_once(), true);
	failures += test_expect_bool("WARN_ON_ONCE second", trigger_warn_once(), true);
	failures += test_expect_int("printk after WARN_ON_ONCE twice", printk_count, 1);

	reset_counts();
	failures += test_expect_bool("WARN_ON_ONCE_MSG first", trigger_warn_once_msg(), true);
	failures += test_expect_bool("WARN_ON_ONCE_MSG second", trigger_warn_once_msg(), true);
	failures += test_expect_int("printk after WARN_ON_ONCE_MSG twice", printk_count, 1);

	return failures;
}

static int test_bug_macros_false_path(void)
{
	int failures = 0;

	reset_counts();
	BUG_ON(false);
	BUG_ON_MSG(false, "value=%d", 7);
	failures += test_expect_int("panic after false BUG_ON paths", panic_count, 0);

	return failures;
}

static int test_panic_from_bug(void)
{
	reset_counts();
	if (setjmp(panic_env) == 0) {
		BUG();
		return 1;
	}

	return test_expect_int("panic after BUG()", panic_count, 1);
}

static int test_panic_from_bug_on(void)
{
	reset_counts();
	if (setjmp(panic_env) == 0) {
		BUG_ON(true);
		return 1;
	}

	return test_expect_int("panic after BUG_ON(true)", panic_count, 1);
}

static int test_panic_from_bug_on_msg(void)
{
	reset_counts();
	if (setjmp(panic_env) == 0) {
		BUG_ON_MSG(true, "value=%d", 7);
		return 1;
	}

	return test_expect_int("panic after BUG_ON_MSG(true)", panic_count, 1);
}

int main(void)
{
	static const struct test_case cases[] = {
		TEST_CASE(test_warn_macros),
		TEST_CASE(test_bug_macros_false_path),
		TEST_CASE(test_panic_from_bug),
		TEST_CASE(test_panic_from_bug_on),
		TEST_CASE(test_panic_from_bug_on_msg),
	};

	return test_run_cases("printk_test",
			      cases, TEST_ARRAY_SIZE(cases));
}
