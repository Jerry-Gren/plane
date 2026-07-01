#ifndef PLANE_PRINTK_H
#define PLANE_PRINTK_H

#include <stdbool.h>

#include <plane/compiler.h>

/* in printk.c */
extern void printk(const char *fmt, ...);
extern void panic(const char *fmt, ...) __noreturn;

#define BUG() do {                                                                  \
	printk("BUG: failure at %s:%d/%s()!\n", __FILE__, __LINE__, __func__);      \
	panic("Kernel BUG!");                                                       \
} while (0)

#define BUG_ON(condition) do {          \
	if (unlikely(condition)) {      \
		BUG();                  \
	}                               \
} while (0)

#define DO_ONCE_LITE_IF(condition, func, ...) ({            \
	static bool __done = false;                         \
	int __ret_cond = !!(condition);                     \
	if (unlikely(__ret_cond && !__done)) {              \
		__done = true;                              \
		func(__VA_ARGS__);                          \
	}                                                   \
	unlikely(__ret_cond);                               \
})

#define WARN_ON(condition) ({                                       \
	int __ret_warn_on = !!(condition);                          \
	if (unlikely(__ret_warn_on))                                \
		printk("WARNING: at %s:%d\n", __FILE__, __LINE__);  \
	unlikely(__ret_warn_on);                                    \
})

#define WARN_ON_ONCE(condition) DO_ONCE_LITE_IF(condition, WARN_ON, 1)

#define printk_once(fmt, ...) do {                          \
	static bool __done = false;                         \
	if (unlikely(!__done)) {                            \
		__done = true;                              \
		printk(fmt, ##__VA_ARGS__);                 \
	}                                                   \
} while (0)

#define pr_info(fmt, ...)   printk("[INFO] " fmt, ##__VA_ARGS__)
#define pr_err(fmt, ...)    printk("[ERR]  " fmt, ##__VA_ARGS__)
#define pr_warn(fmt, ...)   printk("[WARN] " fmt, ##__VA_ARGS__)

#endif /* PLANE_PRINTK_H */
