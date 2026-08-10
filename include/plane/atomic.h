#ifndef PLANE_ATOMIC_H
#define PLANE_ATOMIC_H

#include <stdbool.h>
#include <stdint.h>

static inline uint32_t plane_atomic_load_u32(const uint32_t *ptr)
{
	return __atomic_load_n(ptr, __ATOMIC_ACQUIRE);
}

static inline void plane_atomic_store_u32(uint32_t *ptr, uint32_t value)
{
	__atomic_store_n(ptr, value, __ATOMIC_RELEASE);
}

static inline bool plane_atomic_compare_exchange_u32(uint32_t *ptr,
						     uint32_t *expected,
						     uint32_t desired)
{
	return __atomic_compare_exchange_n(ptr, expected, desired, false,
					   __ATOMIC_ACQUIRE,
					   __ATOMIC_RELAXED);
}

static inline void plane_atomic_fence_release(void)
{
	__atomic_thread_fence(__ATOMIC_RELEASE);
}

#endif /* PLANE_ATOMIC_H */
