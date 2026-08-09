#ifndef MACHINE_SERIAL_H
#define MACHINE_SERIAL_H

#if defined(__x86_64__)
	#include <x86_64/serial.h>
#else
	#error "Unsupported architecture for serial!"
#endif

#endif /* MACHINE_SERIAL_H */
