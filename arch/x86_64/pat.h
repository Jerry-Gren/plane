#ifndef X86_64_PAT_H
#define X86_64_PAT_H

#include <stdbool.h>

bool x86_64_pat_init(void);
bool x86_64_pat_write_combine_is_ready(void);

#endif /* X86_64_PAT_H */
