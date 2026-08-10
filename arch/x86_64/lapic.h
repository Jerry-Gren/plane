#ifndef X86_64_LAPIC_H
#define X86_64_LAPIC_H

#include <stdbool.h>
#include <stdint.h>

struct plane_cpu_data;
struct plane_smp_info;

#define X86_64_LAPIC_VECTOR_INTERPROCESSOR 0xf0u

bool lapic_init_bsp(const struct plane_smp_info *info);
bool lapic_init_ap(struct plane_cpu_data *data);
bool lapic_interrupt(uint32_t vector);
bool lapic_end_of_interrupt(void);
bool lapic_send_ipi(uint32_t logical_id, uint8_t vector);

#endif /* X86_64_LAPIC_H */
