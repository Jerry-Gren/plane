#ifndef MACHINE_PMAP_H
#define MACHINE_PMAP_H

#if defined(__x86_64__)
	#include <x86_64/pmap.h>
#else
	#error "Unsupported architecture for pmap!"
#endif

#endif /* MACHINE_PMAP_H */
