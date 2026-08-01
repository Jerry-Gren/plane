#ifndef HAL_CPU_H
#define HAL_CPU_H

#include <stdbool.h>

#include <plane/compiler.h>

struct plane_cpu_data;

void hal_cpu_hang(void) __noreturn;
void hal_cpu_relax(void);
bool hal_cpu_set_current_data(struct plane_cpu_data *data);

#endif /* HAL_CPU_H */
