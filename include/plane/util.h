#ifndef PLANE_UTIL_H
#define PLANE_UTIL_H

#include <stddef.h>

#include <plane/compiler.h>

#ifndef offsetof
#define offsetof(TYPE, MEMBER) __builtin_offsetof(TYPE, MEMBER)
#endif

#define container_of(ptr, type, member) ({                              \
	void *__mptr = (void *)(ptr);                                   \
	_Static_assert(__same_type(*(ptr), ((type *)0)->member) ||      \
		       __same_type(*(ptr), void),                       \
		       "pointer type mismatch in container_of()");      \
	((type *)(__mptr - offsetof(type, member))); })

#define container_of_const(ptr, type, member)                           \
	_Generic(ptr,                                                   \
		const typeof(*(ptr)) *: ((const type *)container_of(ptr, type, member)),\
		default: ((type *)container_of(ptr, type, member))      \
	)

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]) + __must_be_array(arr))
#define ARRAY_END(arr)  (&(arr)[ARRAY_SIZE(arr)])

#define __ALIGN_KERNEL_MASK(x, mask)    (((x) + (mask)) & ~(mask))
#define __ALIGN_KERNEL(x, a)            __ALIGN_KERNEL_MASK(x, (typeof(x))(a) - 1)

#define ALIGN(x, a)                     __ALIGN_KERNEL((x), (a))
#define ALIGN_DOWN(x, a)                __ALIGN_KERNEL((x) - ((a) - 1), (a))
#define IS_ALIGNED(x, a)                (((x) & ((typeof(x))(a) - 1)) == 0)

#define PTR_ALIGN(p, a)                 ((typeof(p))ALIGN((unsigned long)(p), (a)))
#define PTR_ALIGN_DOWN(p, a)            ((typeof(p))ALIGN_DOWN((unsigned long)(p), (a)))

#endif /* PLANE_UTIL_H */
