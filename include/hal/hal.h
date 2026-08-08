#ifndef HAL_HAL_H
#define HAL_HAL_H

#include <stdbool.h>

/* Architecture startup init before generic kernel subsystems come up. */
bool hal_arch_startup_init(void);

#endif /* HAL_HAL_H */
