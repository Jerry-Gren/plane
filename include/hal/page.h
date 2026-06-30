#ifndef HAL_PAGE_H
#define HAL_PAGE_H

#if defined(__x86_64__)
	#include <hal/x86_64/page.h>
#else
	#error "Unsupported architecture for page constants!"
#endif

#endif /* HAL_PAGE_H */
