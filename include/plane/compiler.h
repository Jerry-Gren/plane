#ifndef PLANE_COMPILER_H
#define PLANE_COMPILER_H

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#ifndef __always_inline
#define __always_inline inline __attribute__((always_inline))
#endif
#define __noinline      __attribute__((noinline))
#define __noreturn      __attribute__((noreturn))
#define __maybe_unused  __attribute__((unused))

/* GCC only */
#define __same_type(a, b) __builtin_types_compatible_p(typeof(a), typeof(b))

#define BUILD_BUG_ON(condition) ((void)sizeof(char[1 - 2*!!(condition)]))
#define BUILD_BUG_ON_ZERO(e)    ((int)(sizeof(struct { int:(-!!(e)); })))

#define __must_be_array(a)      BUILD_BUG_ON_ZERO(__same_type((a), &(a)[0]))

#endif /* PLANE_COMPILER_H */