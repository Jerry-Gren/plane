#ifndef PLANE_PRINTK_H
#define PLANE_PRINTK_H

#include <stdbool.h>

#include <plane/compiler.h>

/* in printk.c */
extern void printk(const char *fmt, ...);
extern void panic(const char *fmt, ...) __noreturn;

#define BUG() do {                                                          \
	panic("BUG at %s:%d/%s()", __FILE__, __LINE__, __func__);           \
} while (0)

#define BUG_ON(condition) do {                                              \
	if (unlikely(condition)) {                                          \
		panic("BUG_ON(%s) at %s:%d/%s()", #condition, __FILE__,    \
		      __LINE__, __func__);                                  \
	}                                                                   \
} while (0)

#define BUG_ON_MSG(condition, fmt, ...) do {                                  \
	if (unlikely(condition)) {                                            \
		panic("BUG_ON(%s) at %s:%d/%s(): " fmt, #condition, __FILE__, \
		      __LINE__, __func__, ##__VA_ARGS__);                     \
	}                                                                     \
} while (0)

#define WARN_ON(condition) ({                                                \
	int __ret_warn_on = !!(condition);                                   \
	if (unlikely(__ret_warn_on)) {                                       \
		printk("[WARN] WARN_ON(%s) at %s:%d/%s()\n", #condition,    \
		       __FILE__, __LINE__, __func__);                       \
	}                                                                    \
	unlikely(__ret_warn_on);                                             \
})

#define WARN_ON_MSG(condition, fmt, ...) ({                                   \
	int __ret_warn_on = !!(condition);                                    \
	if (unlikely(__ret_warn_on)) {                                        \
		printk("[WARN] WARN_ON(%s) at %s:%d/%s(): " fmt "\n",        \
		       #condition, __FILE__, __LINE__, __func__,             \
		       ##__VA_ARGS__);                                       \
	}                                                                     \
	unlikely(__ret_warn_on);                                              \
})

#define WARN_ON_ONCE(condition) ({                                            \
	static bool __warned;                                                \
	int __ret_warn_on = !!(condition);                                   \
	if (unlikely(__ret_warn_on && !__warned)) {                          \
		__warned = true;                                             \
		printk("[WARN] WARN_ON_ONCE(%s) at %s:%d/%s()\n",            \
		       #condition, __FILE__, __LINE__, __func__);            \
	}                                                                    \
	unlikely(__ret_warn_on);                                             \
})

#define WARN_ON_ONCE_MSG(condition, fmt, ...) ({                              \
	static bool __warned;                                                \
	int __ret_warn_on = !!(condition);                                   \
	if (unlikely(__ret_warn_on && !__warned)) {                          \
		__warned = true;                                             \
		printk("[WARN] WARN_ON_ONCE(%s) at %s:%d/%s(): " fmt "\n",   \
		       #condition, __FILE__, __LINE__, __func__,             \
		       ##__VA_ARGS__);                                       \
	}                                                                    \
	unlikely(__ret_warn_on);                                             \
})

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
