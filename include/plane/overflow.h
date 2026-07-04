#ifndef PLANE_OVERFLOW_H
#define PLANE_OVERFLOW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static inline bool plane_checked_add_u64(uint64_t lhs,
					 uint64_t rhs,
					 uint64_t *out)
{
	if (out == NULL || rhs > UINT64_MAX - lhs) {
		return false;
	}

	*out = lhs + rhs;
	return true;
}

static inline bool plane_checked_mul_u64(uint64_t lhs,
					 uint64_t rhs,
					 uint64_t *out)
{
	if (out == NULL || (lhs != 0 && rhs > UINT64_MAX / lhs)) {
		return false;
	}

	*out = lhs * rhs;
	return true;
}

static inline bool plane_is_power_of_two_u64(uint64_t value)
{
	return value != 0 && (value & (value - 1)) == 0;
}

static inline bool plane_checked_align_up_u64(uint64_t value,
					      uint64_t align,
					      uint64_t *out)
{
	uint64_t mask;

	if (out == NULL || !plane_is_power_of_two_u64(align)) {
		return false;
	}

	mask = align - 1;
	if ((value & mask) == 0) {
		*out = value;
		return true;
	}

	if (value > UINT64_MAX - mask) {
		return false;
	}

	*out = (value + mask) & ~mask;
	return true;
}

#endif /* PLANE_OVERFLOW_H */
