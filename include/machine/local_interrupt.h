#ifndef MACHINE_LOCAL_INTERRUPT_H
#define MACHINE_LOCAL_INTERRUPT_H

#if defined(__x86_64__)
	#include <x86_64/local_interrupt.h>
#else
	#error "Unsupported architecture for local interrupts!"
#endif

#endif /* MACHINE_LOCAL_INTERRUPT_H */
