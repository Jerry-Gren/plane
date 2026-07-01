#ifndef PLANE_BITS_H
#define PLANE_BITS_H

#ifdef __ASSEMBLER__

#define BIT(nr) (1 << (nr))

#else

#define BITS_PER_LONG      (sizeof(unsigned long) * 8)
#define BITS_PER_LONG_LONG (sizeof(unsigned long long) * 8)

#define BIT(nr)     (1ul << (nr))
#define BIT_ULL(nr) (1ull << (nr))

#define GENMASK(h, l) \
	(((~0ul) << (l)) & (~0ul >> (BITS_PER_LONG - 1 - (h))))
#define GENMASK_ULL(h, l) \
	(((~0ull) << (l)) & (~0ull >> (BITS_PER_LONG_LONG - 1 - (h))))

#define __bf_shf(mask) (__builtin_ctzll((unsigned long long)(mask)))
#define FIELD_GET(mask, reg) (((reg) & (mask)) >> __bf_shf(mask))
#define FIELD_PREP(mask, val) ((((typeof(mask))(val)) << __bf_shf(mask)) & (mask))

#endif /* __ASSEMBLER__ */

#endif /* PLANE_BITS_H */
