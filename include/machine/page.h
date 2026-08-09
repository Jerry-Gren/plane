#ifndef MACHINE_PAGE_H
#define MACHINE_PAGE_H

#if defined(__x86_64__)
	#include <x86_64/page.h>
#else
	#error "Unsupported architecture for page constants!"
#endif

#endif /* MACHINE_PAGE_H */
