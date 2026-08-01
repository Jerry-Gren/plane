#ifndef HAL_CPU_H
#define HAL_CPU_H

#include <plane/compiler.h>

void hal_cpu_hang(void) __noreturn;
void hal_cpu_relax(void);

#endif /* HAL_CPU_H */
