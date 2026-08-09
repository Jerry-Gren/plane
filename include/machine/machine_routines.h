#ifndef MACHINE_MACHINE_ROUTINES_H
#define MACHINE_MACHINE_ROUTINES_H

#if defined(__x86_64__)
	#include <x86_64/machine_routines.h>
#else
	#error "Unsupported architecture for machine routines!"
#endif

#endif /* MACHINE_MACHINE_ROUTINES_H */
