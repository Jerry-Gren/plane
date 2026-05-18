#include <plane/boot_info.h>
#include <plane/kernel.h>
#include <plane/mm.h>

void plane_sanitize_memory_map(struct plane_mem_info *mem) {
	if (mem->entry_count == 0) return;

	/*
	 * first:
	 *   strip entries whose length is 0
	 *   do align on USABLE memory regions
	 */
	uint64_t valid_count = 0;
	for (uint64_t i = 0; i < mem->entry_count; i++) {
		struct plane_mem_region *r = &mem->map[i];

		if (r->length == 0) continue;

		if (r->type == PLANE_MEM_USABLE) {
			uint64_t start = ALIGN(r->base, PAGE_SIZE);
			uint64_t end   = ALIGN_DOWN(r->base + r->length, PAGE_SIZE);
			if (start >= end) continue;
			r->base = start;
			r->length = end - start;
		}

		mem->map[valid_count++] = *r;
	}
	mem->entry_count = valid_count;

	/*
	 * second:
	 *   bubble sort from lo to hi
	 */
	for (uint64_t i = 0; i < mem->entry_count; i++) {
		for (uint64_t j = 0; j < mem->entry_count - i - 1; j++) {
			if (mem->map[j].base > mem->map[j+1].base) {
				struct plane_mem_region tmp = mem->map[j];
				mem->map[j] = mem->map[j+1];
				mem->map[j+1] = tmp;
			}
		}
	}

	/*
	 * thrid:
	 *   process overlap
	 */
	struct plane_mem_region clean_map[PLANE_MAX_MEMMAP_ENTRIES];
	uint64_t clean_count = 0;
	for (uint64_t i = 0; i < mem->entry_count; i++) {
		struct plane_mem_region curr = mem->map[i];

		if (clean_count == 0) {
			clean_map[clean_count++] = curr;
			continue;
		}

		struct plane_mem_region *prev = &clean_map[clean_count - 1];
		uint64_t prev_end = prev->base + prev->length;
		uint64_t curr_end = curr.base + curr.length;

		/*
		 * case 1: completely disjoint
		 * 
		 * prev: [========]
		 * curr:               [========]
		 * 
		 * action: append curr.
		 */
		if (curr.base >= prev_end) {
			if (clean_count < PLANE_MAX_MEMMAP_ENTRIES) {
				clean_map[clean_count++] = curr;
			}
			continue;
		}
		/*
		 * case 2: same type overlap
		 * 
		 * prev: [========]
		 * curr:      [========]
		 * 
		 * action: extend prev to cover curr's end.
		 */
		if (prev->type == curr.type) {
			if (curr_end > prev_end) {
				prev->length = curr_end - prev->base;
			}
			continue;
		}
		/* 
		 * case 3: different types overlap
		 * priority: non-USABLE > USABLE
		 */
		int prev_prio = (prev->type == PLANE_MEM_USABLE) ? 0 : 1;
		int curr_prio = (curr.type == PLANE_MEM_USABLE) ? 0 : 1;
		if (curr_prio > prev_prio) {
			/*
			 * case 3a: curr (hi-prio) truncates prev (lo-prio)
			 *
			 * prev (USABLE)  : [----------------------]
			 * curr (RESERVED):         [######]
			 * 
			 * step 1 (truncate prev) : [------]
			 * step 2 (append curr)   : [------][######]
			 * step 3 (save tail)     :                 [------] -> pushed back to i--
			 */
			uint64_t old_prev_end = prev_end;
			prev->length = curr.base - prev->base;

			if (prev->length == 0) {
				*prev = curr; /* prev is completely crushed, replace it */
			} else if (clean_count < PLANE_MAX_MEMMAP_ENTRIES) {
				clean_map[clean_count++] = curr;
			}

			/* rescue the remaining tail of prev if curr split it */
			if (curr_end < old_prev_end) {
				uint64_t tail_base = ALIGN(curr_end, PAGE_SIZE);
				if (tail_base < old_prev_end) {
					uint64_t tail_len = ALIGN_DOWN(old_prev_end - tail_base, PAGE_SIZE);
					if (tail_len > 0) {
						/* put the tail back, deduct i to reprocess it */
						mem->map[i].base = tail_base;
						mem->map[i].length = tail_len;
						mem->map[i].type = prev->type;
						i--; 
					}
				}
			}
		}
		else if (prev_prio > curr_prio) {
			/*
			 * case 3b: prev (hi-prio) cuts off curr's head (lo-prio)
			 *
			 * prev (RESERVED): [######]
			 * curr (USABLE)  :     [------------------]
			 * 
			 * step 1 (keep prev)    : [######]
			 * step 2 (truncate curr):         [-------] -> pushed back to i--
			 */
			if (curr_end > prev_end) {
				uint64_t new_base = ALIGN(prev_end, PAGE_SIZE);
				if (new_base < curr_end) {
					uint64_t new_len = ALIGN_DOWN(curr_end - new_base, PAGE_SIZE);
					if (new_len > 0) {
						/* put the tail back, deduct i to reprocess it */
						mem->map[i].base = new_base;
						mem->map[i].length = new_len;
						i--;
					}
				}
			}
		}
		else {
			/* 
			 * case 3c: both high priority but different specific types 
			 * (e.g., ACPI_NVS vs BAD_MEMORY)
			 * action: degrade to strictest generic RESERVED and merge.
			 */
			prev->type = PLANE_MEM_RESERVED;
			if (curr_end > prev_end) {
				prev->length = curr_end - prev->base;
			}
		}
	}

	/* write back */
	for (uint64_t i = 0; i < clean_count; i++) {
		mem->map[i] = clean_map[i];
	}
	mem->entry_count = clean_count;
}