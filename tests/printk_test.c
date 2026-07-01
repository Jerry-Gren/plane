#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>

#include <plane/printk.h>

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

static int expect_bool(const char *name, int actual, int expected)
{
	if (!!actual == !!expected) {
		return 0;
	}

	printf("FAIL: %s expected %d got %d\n", name, !!expected, !!actual);
	return 1;
}

static int expect_int(const char *name, int actual, int expected)
{
	if (actual == expected) {
		return 0;
	}

	printf("FAIL: %s expected %d got %d\n", name, expected, actual);
	return 1;
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
	failures += expect_bool("WARN_ON(false)", WARN_ON(false), false);
	failures += expect_int("printk after WARN_ON(false)", printk_count, 0);

	reset_counts();
	failures += expect_bool("WARN_ON(true)", WARN_ON(true), true);
	failures += expect_int("printk after WARN_ON(true)", printk_count, 1);

	reset_counts();
	failures += expect_bool("WARN_ON_MSG(true)", WARN_ON_MSG(true, "value=%d", 7), true);
	failures += expect_int("printk after WARN_ON_MSG(true)", printk_count, 1);

	reset_counts();
	failures += expect_bool("WARN_ON_ONCE(false)", WARN_ON_ONCE(false), false);
	failures += expect_int("printk after WARN_ON_ONCE(false)", printk_count, 0);

	reset_counts();
	failures += expect_bool("WARN_ON_ONCE first", trigger_warn_once(), true);
	failures += expect_bool("WARN_ON_ONCE second", trigger_warn_once(), true);
	failures += expect_int("printk after WARN_ON_ONCE twice", printk_count, 1);

	reset_counts();
	failures += expect_bool("WARN_ON_ONCE_MSG first", trigger_warn_once_msg(), true);
	failures += expect_bool("WARN_ON_ONCE_MSG second", trigger_warn_once_msg(), true);
	failures += expect_int("printk after WARN_ON_ONCE_MSG twice", printk_count, 1);

	return failures;
}

static int test_bug_macros_false_path(void)
{
	int failures = 0;

	reset_counts();
	BUG_ON(false);
	BUG_ON_MSG(false, "value=%d", 7);
	failures += expect_int("panic after false BUG_ON paths", panic_count, 0);

	return failures;
}

static int expect_panic_from_bug(void)
{
	reset_counts();
	if (setjmp(panic_env) == 0) {
		BUG();
		return 1;
	}

	return expect_int("panic after BUG()", panic_count, 1);
}

static int expect_panic_from_bug_on(void)
{
	reset_counts();
	if (setjmp(panic_env) == 0) {
		BUG_ON(true);
		return 1;
	}

	return expect_int("panic after BUG_ON(true)", panic_count, 1);
}

static int expect_panic_from_bug_on_msg(void)
{
	reset_counts();
	if (setjmp(panic_env) == 0) {
		BUG_ON_MSG(true, "value=%d", 7);
		return 1;
	}

	return expect_int("panic after BUG_ON_MSG(true)", panic_count, 1);
}

int main(void)
{
	int failures = 0;

	failures += test_warn_macros();
	failures += test_bug_macros_false_path();
	failures += expect_panic_from_bug();
	failures += expect_panic_from_bug_on();
	failures += expect_panic_from_bug_on_msg();

	if (failures != 0) {
		return 1;
	}

	printf("printk_test: ok\n");
	return 0;
}
