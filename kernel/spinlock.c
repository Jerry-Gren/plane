#include <machine/machine_routines.h>

#include <plane/atomic.h>
#include <plane/spinlock.h>

void plane_spin_init(struct plane_spinlock *lock)
{
	lock->locked = 0;
}

bool plane_spin_is_locked(const struct plane_spinlock *lock)
{
	return plane_atomic_load_u32(&lock->locked) != 0;
}

bool plane_spin_try_lock(struct plane_spinlock *lock)
{
	uint32_t expected = 0;

	return plane_atomic_compare_exchange_u32(&lock->locked, &expected, 1);
}

void plane_spin_lock(struct plane_spinlock *lock)
{
	while (!plane_spin_try_lock(lock)) {
		while (plane_spin_is_locked(lock)) {
			cpu_pause();
		}
	}
}

void plane_spin_unlock(struct plane_spinlock *lock)
{
	plane_atomic_store_u32(&lock->locked, 0);
}

plane_irq_state_t plane_spin_lock_irqsave(struct plane_spinlock *lock)
{
	plane_irq_state_t state = ml_irq_save();

	plane_spin_lock(lock);
	return state;
}

void plane_spin_unlock_irqrestore(struct plane_spinlock *lock,
				  plane_irq_state_t state)
{
	plane_spin_unlock(lock);
	ml_irq_restore(state);
}
