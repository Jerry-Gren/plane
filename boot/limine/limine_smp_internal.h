#ifndef BOOT_LIMINE_SMP_INTERNAL_H
#define BOOT_LIMINE_SMP_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include <limine.h>

void boot_limine_smp_reset_cpu_handles(void);
bool boot_limine_smp_set_cpu_handle(uint32_t logical_id,
				    struct limine_mp_info *cpu);
bool boot_limine_smp_start_aps(void);

#endif /* BOOT_LIMINE_SMP_INTERNAL_H */
