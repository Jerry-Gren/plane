#ifndef HAL_PMAP_H
#define HAL_PMAP_H

/*
 * Generic pmap signal landing points. Architecture pmap code owns the actual
 * page-table/TLB mechanics; generic SMP only depends on this HAL boundary.
 */
void hal_pmap_update_interrupt(void);

#endif /* HAL_PMAP_H */
