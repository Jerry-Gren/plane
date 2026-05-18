#ifndef PLANE_KERNEL_H
#define PLANE_KERNEL_H

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

#define BIT(nr)         (1UL << (nr))

#define __ALIGN_KERNEL_MASK(x, mask)    (((x) + (mask)) & ~(mask))
#define __ALIGN_KERNEL(x, a)            __ALIGN_KERNEL_MASK(x, (typeof(x))(a) - 1)

#define ALIGN(x, a)                     __ALIGN_KERNEL((x), (a))
#define ALIGN_DOWN(x, a)                __ALIGN_KERNEL((x) - ((a) - 1), (a))
#define IS_ALIGNED(x, a)                (((x) & ((typeof(x))(a) - 1)) == 0)

#define PTR_ALIGN(p, a)                 ((typeof(p))ALIGN((unsigned long)(p), (a)))
#define PTR_ALIGN_DOWN(p, a)            ((typeof(p))ALIGN_DOWN((unsigned long)(p), (a)))

extern void printk(const char *fmt, ...);
extern void panic(const char *fmt, ...) __noreturn;

#define BUG() do { \
    printk("BUG: failure at %s:%d/%s()!\n", __FILE__, __LINE__, __func__); \
    panic("Kernel BUG!"); \
} while (0)

#define BUG_ON(condition) do { \
    if (unlikely(condition)) { \
        BUG(); \
    } \
} while (0)

#define DO_ONCE_LITE_IF(condition, func, ...) ({        \
    static bool __done = false;                         \
    int __ret_cond = !!(condition);                     \
    if (unlikely(__ret_cond && !__done)) {              \
        __done = true;                                  \
        func(__VA_ARGS__);                              \
    }                                                   \
    unlikely(__ret_cond);                               \
})

#define WARN_ON(condition) ({                                   \
    int __ret_warn_on = !!(condition);                          \
    if (unlikely(__ret_warn_on))                                \
        printk("WARNING: at %s:%d\n", __FILE__, __LINE__);      \
    unlikely(__ret_warn_on);                                    \
})

#define WARN_ON_ONCE(condition) DO_ONCE_LITE_IF(condition, WARN_ON, 1)

#define printk_once(fmt, ...) do {                      \
    static bool __done = false;                         \
    if (unlikely(!__done)) {                            \
        __done = true;                                  \
        printk(fmt, ##__VA_ARGS__);                     \
    }                                                   \
} while (0)

#define pr_info(fmt, ...)   printk("[INFO] " fmt, ##__VA_ARGS__)
#define pr_err(fmt, ...)    printk("[ERR]  " fmt, ##__VA_ARGS__)
#define pr_warn(fmt, ...)   printk("[WARN] " fmt, ##__VA_ARGS__)

#endif /* PLANE_KERNEL_H */