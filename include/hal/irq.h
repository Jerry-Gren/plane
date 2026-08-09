#ifndef HAL_IRQ_H
#define HAL_IRQ_H

#include <stdbool.h>

typedef struct {
	bool enabled;
} plane_irq_state_t;

void hal_irq_disable(void);
void hal_irq_enable(void);
bool hal_irq_is_enabled(void);
plane_irq_state_t hal_irq_save(void);
void hal_irq_restore(plane_irq_state_t state);

#endif /* HAL_IRQ_H */
