#ifndef PLANE_SPINLOCK_H
#define PLANE_SPINLOCK_H

#include <stdbool.h>
#include <stdint.h>

#include <hal/irq.h>

struct plane_spinlock {
	uint32_t locked;
};

#define PLANE_SPINLOCK_INIT { .locked = 0 }

void plane_spin_init(struct plane_spinlock *lock);
void plane_spin_lock(struct plane_spinlock *lock);
bool plane_spin_try_lock(struct plane_spinlock *lock);
void plane_spin_unlock(struct plane_spinlock *lock);
plane_irq_state_t plane_spin_lock_irqsave(struct plane_spinlock *lock);
void plane_spin_unlock_irqrestore(struct plane_spinlock *lock,
				  plane_irq_state_t state);
bool plane_spin_is_locked(const struct plane_spinlock *lock);

#endif /* PLANE_SPINLOCK_H */
