#include <plane/memmap.h>
#include <plane/mm.h>
#include <plane/util.h>

#define PLANE_MEMMAP_MAX_BOUNDARIES (PLANE_MAX_MEMMAP_ENTRIES * 2)

static bool checked_region_end(uint64_t base, uint64_t length, uint64_t *end) {
	*end = base + length;
	return *end >= base;
}

static bool append_clean_region(struct plane_mem_region *clean_map,
				uint64_t *clean_count,
				struct plane_mem_region region) {
	if (*clean_count > 0) {
		struct plane_mem_region *prev = &clean_map[*clean_count - 1];
		uint64_t prev_end;

		if (!checked_region_end(prev->base, prev->length, &prev_end)) {
			return false;
		}

		if (prev_end == region.base && prev->type == region.type) {
			uint64_t region_end;

			/*
			 * Boundary scan emits small adjacent pieces.
			 *
			 * prev: [====]
			 * curr:      [====]
			 *
			 * action: merge equal-type neighbors.
			 */
			if (!checked_region_end(region.base, region.length,
						&region_end)) {
				return false;
			}
			prev->length = region_end - prev->base;
			return true;
		}
	}

	if (*clean_count >= PLANE_MAX_MEMMAP_ENTRIES) {
		return false;
	}

	clean_map[(*clean_count)++] = region;
	return true;
}

static int mem_type_rank(uint32_t type) {
	/*
	 * Overlap priority:
	 *   usable             : allocator input, lowest rank
	 *   generic/untrusted  : unavailable, invalid, unknown, or mapped fallback
	 *   specific reserved  : keeps boot protocol / hardware semantics
	 *   bootloader handoff : protects data passed into early kernel C
	 */
	switch (type) {
	case PLANE_MEM_USABLE:
		return 0;
	case PLANE_MEM_INVALID:
	case PLANE_MEM_RESERVED:
	case PLANE_MEM_RESERVED_MAPPED:
		return 1;
	case PLANE_MEM_BOOTLOADER_RECLAIMABLE:
		return 3;
	case PLANE_MEM_ACPI_RECLAIMABLE:
	case PLANE_MEM_ACPI_NVS:
	case PLANE_MEM_BAD_MEMORY:
	case PLANE_MEM_EXECUTABLE_AND_MODULES:
	case PLANE_MEM_FRAMEBUFFER:
		return 2;
	default:
		return 1;
	}
}

static bool append_boundary(uint64_t *boundaries, uint64_t *boundary_count,
			    uint64_t boundary) {
	for (uint64_t i = 0; i < *boundary_count; i++) {
		if (boundaries[i] == boundary) {
			return true;
		}
	}

	if (*boundary_count >= PLANE_MEMMAP_MAX_BOUNDARIES) {
		return false;
	}

	boundaries[(*boundary_count)++] = boundary;
	return true;
}

static void sort_boundaries(uint64_t *boundaries, uint64_t boundary_count) {
	for (uint64_t i = 0; i < boundary_count; i++) {
		for (uint64_t j = 0; j < boundary_count - i - 1; j++) {
			if (boundaries[j] > boundaries[j + 1]) {
				uint64_t tmp = boundaries[j];
				boundaries[j] = boundaries[j + 1];
				boundaries[j + 1] = tmp;
			}
		}
	}
}

static bool choose_interval_type(const struct plane_mem_info *mem,
				 uint64_t base,
				 uint64_t end,
				 uint32_t *type,
				 bool *covered) {
	int best_rank = -1;
	uint32_t best_type = PLANE_MEM_INVALID;
	bool conflict = false;

	*covered = false;

	for (uint64_t i = 0; i < mem->entry_count; i++) {
		const struct plane_mem_region *region = &mem->map[i];
		uint64_t region_end;

		if (!checked_region_end(region->base, region->length,
					&region_end)) {
			return false;
		}

		if (region->base > base || region_end < end) {
			continue;
		}

		int rank = mem_type_rank(region->type);
		if (!*covered || rank > best_rank) {
			best_rank = rank;
			best_type = region->type;
			conflict = false;
			*covered = true;
		} else if (rank == best_rank && region->type != best_type) {
			/*
			 * ACPI_NVS: [------]
			 * BAD_MEM :    [------]
			 * result  : [NVS][RESV][BAD]
			 *
			 * action: same-rank conflict degrades only the
			 * conflicting interval to generic RESERVED.
			 */
			conflict = true;
		}
	}

	if (*covered) {
		*type = conflict ? PLANE_MEM_RESERVED : best_type;
	}
	return true;
}

bool plane_sanitize_memory_map(struct plane_mem_info *mem) {
	if (mem->entry_count == 0) return true;

	/*
	 * first:
	 *   strip entries whose length is 0
	 *   do align on USABLE memory regions
	 */
	uint64_t valid_count = 0;
	for (uint64_t i = 0; i < mem->entry_count; i++) {
		struct plane_mem_region *r = &mem->map[i];

		if (r->length == 0) continue;

		uint64_t end;
		if (!checked_region_end(r->base, r->length, &end)) {
			return false;
		}

		if (r->type == PLANE_MEM_USABLE) {
			uint64_t start = ALIGN(r->base, PAGE_SIZE);
			uint64_t aligned_end = ALIGN_DOWN(end, PAGE_SIZE);
			if (start >= aligned_end) continue;
			r->base = start;
			r->length = aligned_end - start;
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

	uint64_t boundaries[PLANE_MEMMAP_MAX_BOUNDARIES];
	uint64_t boundary_count = 0;
	for (uint64_t i = 0; i < mem->entry_count; i++) {
		uint64_t end;

		if (!checked_region_end(mem->map[i].base, mem->map[i].length,
					&end)) {
			return false;
		}

		if (!append_boundary(boundaries, &boundary_count,
				     mem->map[i].base) ||
		    !append_boundary(boundaries, &boundary_count, end)) {
			return false;
		}
	}
	sort_boundaries(boundaries, boundary_count);

	/*
	 * raw:
	 *   A: [-----------]
	 *   B:     [###]
	 * bounds:
	 *      |---|###|---|
	 * action:
	 *   choose type for each interval, then merge adjacent equal types.
	 */
	/*
	 * third:
	 *   split overlaps on all boundaries and select the highest-rank type
	 */
	struct plane_mem_region clean_map[PLANE_MAX_MEMMAP_ENTRIES];
	uint64_t clean_count = 0;
	for (uint64_t i = 0; i + 1 < boundary_count; i++) {
		uint64_t base = boundaries[i];
		uint64_t end = boundaries[i + 1];
		uint32_t type;
		bool covered;

		if (base == end) {
			continue;
		}

		if (!choose_interval_type(mem, base, end, &type, &covered)) {
			return false;
		}
		if (!covered) {
			continue;
		}

		if (!append_clean_region(clean_map, &clean_count,
					 (struct plane_mem_region){
						 .base = base,
						 .length = end - base,
						 .type = type
					 })) {
			return false;
		}
	}

	/* write back */
	for (uint64_t i = 0; i < clean_count; i++) {
		mem->map[i] = clean_map[i];
	}
	mem->entry_count = clean_count;
	return true;
}

bool plane_memmap_reserve(struct plane_mem_info *mem, uint64_t base,
			  uint64_t length, uint32_t type) {
	if (length == 0) {
		return true;
	}

	if (type == PLANE_MEM_INVALID || type == PLANE_MEM_USABLE) {
		return false;
	}

	if (mem->entry_count >= PLANE_MAX_MEMMAP_ENTRIES) {
		return false;
	}

	uint64_t end;
	if (!checked_region_end(base, length, &end)) {
		return false;
	}

	uint64_t reserve_base = ALIGN_DOWN(base, PAGE_SIZE);
	uint64_t reserve_end = ALIGN(end, PAGE_SIZE);
	if (reserve_end < end || reserve_end <= reserve_base) {
		return false;
	}

	struct plane_mem_info tmp = *mem;
	uint64_t index = tmp.entry_count++;
	tmp.map[index].base = reserve_base;
	tmp.map[index].length = reserve_end - reserve_base;
	tmp.map[index].type = type;

	if (!plane_sanitize_memory_map(&tmp)) {
		return false;
	}

	*mem = tmp;
	return true;
}
