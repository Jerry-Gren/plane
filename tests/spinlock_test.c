#include <stdbool.h>
#include <stdint.h>

#include <hal/cpu.h>
#include <hal/irq.h>
#include <plane/spinlock.h>

#include "support/test.h"

static bool test_irq_enabled;
static uint32_t test_relax_count;

void hal_irq_disable(void)
{
	test_irq_enabled = false;
}

void hal_irq_enable(void)
{
	test_irq_enabled = true;
}

bool hal_irq_is_enabled(void)
{
	return test_irq_enabled;
}

plane_irq_state_t hal_irq_save(void)
{
	plane_irq_state_t state = {
		.enabled = test_irq_enabled
	};

	hal_irq_disable();
	return state;
}

void hal_irq_restore(plane_irq_state_t state)
{
	test_irq_enabled = state.enabled;
}

void hal_cpu_relax(void)
{
	test_relax_count++;
}

static int test_lock_unlock_try_lock(void)
{
	int failures = 0;
	struct plane_spinlock lock = PLANE_SPINLOCK_INIT;

	failures += test_expect_bool("initial unlocked",
				     plane_spin_is_locked(&lock), false);
	failures += test_expect_bool("try lock succeeds",
				     plane_spin_try_lock(&lock), true);
	failures += test_expect_bool("locked",
				     plane_spin_is_locked(&lock), true);
	failures += test_expect_bool("try lock fails while held",
				     plane_spin_try_lock(&lock), false);
	plane_spin_unlock(&lock);
	failures += test_expect_bool("unlock clears",
				     plane_spin_is_locked(&lock), false);

	plane_spin_lock(&lock);
	failures += test_expect_bool("lock acquires",
				     plane_spin_is_locked(&lock), true);
	plane_spin_unlock(&lock);

	return failures;
}

static int test_init_resets_lock(void)
{
	int failures = 0;
	struct plane_spinlock lock = PLANE_SPINLOCK_INIT;

	plane_spin_lock(&lock);
	plane_spin_init(&lock);
	failures += test_expect_bool("init resets held lock",
				     plane_spin_is_locked(&lock), false);
	return failures;
}

static int test_irqsave_restores_enabled_state(void)
{
	int failures = 0;
	struct plane_spinlock lock = PLANE_SPINLOCK_INIT;

	test_irq_enabled = true;
	plane_irq_state_t state = plane_spin_lock_irqsave(&lock);
	failures += test_expect_bool("irq disabled while locked",
				     test_irq_enabled, false);
	failures += test_expect_bool("lock held with irq disabled",
				     plane_spin_is_locked(&lock), true);
	plane_spin_unlock_irqrestore(&lock, state);
	failures += test_expect_bool("irq restored enabled",
				     test_irq_enabled, true);
	failures += test_expect_bool("lock released after irqrestore",
				     plane_spin_is_locked(&lock), false);

	return failures;
}

static int test_irqsave_restores_disabled_state(void)
{
	int failures = 0;
	struct plane_spinlock lock = PLANE_SPINLOCK_INIT;

	test_irq_enabled = false;
	plane_irq_state_t state = plane_spin_lock_irqsave(&lock);
	failures += test_expect_bool("irq remains disabled while locked",
				     test_irq_enabled, false);
	plane_spin_unlock_irqrestore(&lock, state);
	failures += test_expect_bool("irq restored disabled",
				     test_irq_enabled, false);

	return failures;
}

int main(void)
{
	static const struct test_case cases[] = {
		TEST_CASE(test_lock_unlock_try_lock),
		TEST_CASE(test_init_resets_lock),
		TEST_CASE(test_irqsave_restores_enabled_state),
		TEST_CASE(test_irqsave_restores_disabled_state),
	};

	return test_run_cases("spinlock_test",
			      cases, TEST_ARRAY_SIZE(cases));
}
