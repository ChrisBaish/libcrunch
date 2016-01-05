#ifndef LIBCRUNCH_CIL_INLINES_H_
#define LIBCRUNCH_CIL_INLINES_H_

/* NO -- assume uniqtype is already defined, e.g. by -include */
// #include "uniqtype.h"

/* Ideally we really want to fit in 64 bits on x86-64. 
 * This makes life a bit trickier, however. 
 * For now, we support both representations, using conditional compilation. */
struct __libcrunch_bounds_s
{
#ifdef LIBCRUNCH_WORDSIZE_BOUNDS
	/* We store only the lower 32 bits of the base and limit. 
	 * We infer the upper 32 bits from those of the derivedfrom ptr,
	 * and from the inequality
	 *    base <= ptr < limit
	 * ... see __libcrunch_get_base and __libcrunch_get_limit.
	 */
	unsigned long base:32;
	unsigned long limit:32;
#else
	unsigned long base;
	unsigned long limit;
#endif
};
typedef struct __libcrunch_bounds_s __libcrunch_bounds_t;

#ifndef unlikely
#define __libcrunch_defined_unlikely
#define unlikely(cond) (__builtin_expect( (cond), 0 ))
#endif
#ifndef likely
#define __libcrunch_defined_likely
#define likely(cond)   (__builtin_expect( (cond), 1 ))
#endif

/* Our functions are *not* weak -- they're defined in the noop library. 
 * (We would like the noop library not to be necessary.) */

int __libcrunch_global_init (void);

#if !defined(NO_PURE) && !defined(PURE)
#define PURE __attribute__((pure))
#elif !defined(PURE)
#define PURE
#endif

/* Type checking */
int __is_a_internal(const void *obj, const void *u) PURE;
int __like_a_internal(const void *obj, const void *u) PURE;
int __named_a_internal(const void *obj, const void *u) PURE;
int __is_a_function_refining_internal(const void *obj, const void *u) PURE;
int __is_a_pointer_of_degree_internal(const void *obj, int d) PURE;
int __can_hold_pointer_internal(const void *obj, const void *value) PURE;

/* Bounds checking */
__libcrunch_bounds_t __fetch_bounds_internal(const void *ptr, struct uniqtype *u) PURE;
void __libcrunch_bounds_error(const void *derived, const void *derivedfrom, 
		__libcrunch_bounds_t bounds);
// NOTE that __check_derive_ptr is entirely inline, in terms of __fetch_bounds

/* Utilities */
// FIXME: is it okay that this is weak? I think we don't use it anyway
const void *__libcrunch_typestr_to_uniqtype (const char *) __attribute__((weak));
/* This is not weak. */
void __assert_fail(const char *__assertion, const char *__file,
    unsigned int __line, const char *__function);
void abort(void) __attribute__((noreturn));

extern _Bool __libcrunch_is_initialized __attribute__((weak));
extern unsigned long __libcrunch_begun __attribute__((weak));
extern unsigned long __libcrunch_aborted_typestr __attribute__((weak));
extern unsigned long __libcrunch_succeeded __attribute__((weak));
extern unsigned long __libcrunch_failed __attribute__((weak));
extern unsigned long __libcrunch_is_a_hit_cache __attribute__((weak));
extern unsigned long __libcrunch_checked_pointer_adjustments __attribute__((weak));
extern unsigned long __libcrunch_created_invalid_pointer __attribute__((weak));

extern unsigned int /* __thread */ __libcrunch_is_a_cache_validity;
extern const unsigned short __libcrunch_is_a_cache_size;
extern unsigned short __libcrunch_is_a_cache_next_victim;
#ifndef LIBCRUNCH_MAX_IS_A_CACHE_SIZE
#define LIBCRUNCH_MAX_IS_A_CACHE_SIZE 4
#endif

#ifndef LIBCRUNCH_TRAP_TAG_SHIFT
#define LIBCRUNCH_TRAP_TAG_SHIFT 48 /* FIXME: good for x86-64, less good for others */
#endif

#define LIBCRUNCH_TRAP_ONE_PAST 1
#define LIBCRUNCH_TRAP_ONE_BEFORE 2
#define LIBCRUNCH_TRAP_INVALID 15     
#define LIBCRUNCH_TRAP_MASK (((unsigned long)(LIBCRUNCH_TRAP_INVALID)) << LIBCRUNCH_TRAP_TAG_SHIFT)

/* tentative redesign to integrate bounds and types:
 * 
 * - lower
 * - upper     (one-past)
 * - t         (may be null, i.e. bounds only)
 * - sz        (size of t)
 * - period    (need not be same as period, i.e. if T is int, alloc is array of stat, say)
 *                 ** ptr arithmetic is only valid if sz == period
 *                 ** entries with sz != period are still useful for checking types 
 * - results   (__is_a, __like_a, __locally_like_a, __is_function_refining, ... others?)
 */

struct __libcrunch_is_a_cache_s
{
	const void *obj_base;
	const void *obj_limit;
	unsigned long long uniqtype:((8 * sizeof(void*))-1);
	unsigned result:1;
	unsigned short period;
	/* add alloc base and uniqtype, to do inline uniqtype cache word check? */
};
extern inline int (__attribute__((always_inline,gnu_inline)) __is_aU )(const void *obj, const void *uniqtype);

struct __libcrunch_is_a_cache_s /* __thread */ __libcrunch_is_a_cache[LIBCRUNCH_MAX_IS_A_CACHE_SIZE] __attribute__((weak)); /* some length */

extern inline void (__attribute__((always_inline,gnu_inline)) __inline_assert)(
	int cond, const char *assertion, const char *file, unsigned int line, const char *func
		);
extern inline void (__attribute__((always_inline,gnu_inline)) __inline_assert)(
	int cond, const char *assertion, const char *file, unsigned int line, const char *func
		){
	if (!cond) __assert_fail(assertion, file, line, func);
}

extern inline int (__attribute__((always_inline,gnu_inline)) __libcrunch_check_init)(void);
extern inline int (__attribute__((always_inline,gnu_inline)) __libcrunch_check_init)(void)
{
	if (unlikely(! & __libcrunch_is_initialized))
	{
		/* This means that we're not linked with libcrunch. 
		 * There's nothing we can do! */
		return -1;
	}
	if (unlikely(!__libcrunch_is_initialized))
	{
		/* This means we haven't initialized.
		 * Try that now (it won't try more than once). */
		int ret = __libcrunch_global_init ();
		return ret;
	}
	
	return 0;
}

extern inline void (__attribute__((always_inline,gnu_inline)) __libcrunch_check_local_bounds)(int idx, int limit);
extern inline void (__attribute__((always_inline,gnu_inline)) __libcrunch_check_local_bounds)(int idx, int limit)
{
	/* FIXME: actually do something more sophisticated involving trap pointers here. */
	if (unlikely(idx >= limit)) abort();
}

/* FIXME: reinstate "extended counts" versions, which were

#ifdef LIBCRUNCH_EXTENDED_COUNTS
#define LIBCRUNCH_BASIC_CHECKS \
	do { \
		++__libcrunch_begun; \
		// Check for init first, else we can't use the counts. \
		\ 
		if (unlikely(__libcrunch_check_init() == -1)) \
		{ \
			++__libcrunch_begun; \
			++__libcrunch_aborted_init; \
			return 1; \
		} \
		if (!obj) \
		{ \
			++__libcrunch_begun; \
			++__libcrunch_trivially_succeeded; \
			return 1; \
		} \
	} while (0)
#else
#define LIBCRUNCH_BASIC_CHECKS \
	do { \
		if (!obj) \
		{ \
			return 1; \
		} \
		if (unlikely(__libcrunch_check_init() == -1)) \
		{ \
			return 1; \
		} \
	} while (0)
#endif

extern inline int __attribute__((always_inline,gnu_inline)) __is_aU(const void *obj, struct uniqtype *r)
{
	LIBCRUNCH_BASIC_CHECKS;
	
	// Null uniqtype means __is_aS got a bad typestring, OR we're not 
	// linked with enough uniqtypes data. 
	if (unlikely(r == NULL))
	{
		++__libcrunch_begun;
		if (__libcrunch_debug_level > 0) warnx("Aborted __is_a(%p, %p), reason: %\n", obj, r, 
			"unrecognised typename (see stack trace)");
		++__libcrunch_aborted_typestr;
		return 1;
	}
	
	if (r == (void*) &__uniqtype__signed_char || r == (void*) &__uniqtype__unsigned_char)
	{
#ifdef LIBCRUNCH_EXTENDED_COUNTS
		++__libcrunch_begun;
		++__libcrunch_trivially_succeeded;
#endif
		return 1;
	}
	
	// now we're really started
	++__libcrunch_begun;
	return __is_a_internal(obj, r);
}

extern inline int __attribute__((always_inline,gnu_inline)) __is_aS(const void *obj, const char *typestr)
{
	LIBCRUNCH_BASIC_CHECKS;
	
	const struct uniqtype * r = __libcrunch_typestr_to_uniqtype(typestr);

	return __is_aU(obj, r);
}

*/

/* To implement a get_pc() function, we'd like to use __builtin_return_address
 * which means the function really needs to be noinline -- but also static,
 * to avoid multiple-definition link errors. This causes the compiler to 
 * complain (warn) when it's used from extern inline functions, like all
 * our functions here. HMM. Instead, ditch __builtin_return_address in favour
 * of some different GNU extensions: "&&label" and gnu_inline. ACTUALLY that
 * doesn't work on my current gcc (4.9.2) because it mistakenly thinks that 
 * "&&mylabel" is a local variable, then does bogus UB optimisations on the
 * result. SIGH. Use an arch-specific sequence instead. DOUBLE SIGH: if we
 * write "callq 0" it gets relocated, so use .byte. */
extern inline void * (__attribute__((always_inline,gnu_inline)) __libcrunch_get_pc)(void);
extern inline void * (__attribute__((always_inline,gnu_inline)) __libcrunch_get_pc)(void)
{
	// mylabel: return &&mylabel;
	void *addr;
	__asm__ volatile (".byte 0xe8   # callq \n\
	                   .byte 0x0    # 0 \n\
	                   .byte 0x0    # \n\
	                   .byte 0x0    # \n\
	                   .byte 0x0    # \n\
	                   pop %0" : "=r"(addr)); /* FIXME: CIL frontc parse bug: */ // : /* no inputs */ : /* no clobbers */);
	return addr;
}
void warnx(const char *fmt, ...);

extern inline void (__attribute__((always_inline,gnu_inline)) __libcrunch_trace_widen_int_to_pointer )(unsigned long long val, unsigned long from_size);
extern inline void (__attribute__((always_inline,gnu_inline)) __libcrunch_trace_widen_int_to_pointer )(unsigned long long val, unsigned long from_size)
{
#ifdef LIBCRUNCH_TRACE_WIDEN_INT_TO_POINTER
	/* To get a return address, use a noinline nested function. */
	if (from_size < sizeof (void*)) warnx("Unsafe integer-to-pointer cast of value %llx %at %p\n", val, __libcrunch_get_pc());
#endif
}

extern inline struct __libcrunch_is_a_cache_s *(__attribute__((always_inline,gnu_inline)) __libcrunch_cache_lookup )(const void *obj, struct uniqtype *t);
extern inline struct __libcrunch_is_a_cache_s *(__attribute__((always_inline,gnu_inline)) __libcrunch_cache_lookup )(const void *obj, struct uniqtype *t)
{
	unsigned i;
	for (i = 0; i < __libcrunch_is_a_cache_size; ++i)
	{
		if (__libcrunch_is_a_cache_validity & (1<<i))
		{
			unsigned long long cache_uniqtype = __libcrunch_is_a_cache[i].uniqtype;
			/* We test whether the difference is divisible by the period and within the bounds */
			signed long long diff = (char*) obj - (char*) __libcrunch_is_a_cache[i].obj_base;
			if ((void*) cache_uniqtype == t
					&& (char*) obj >= (char*)__libcrunch_is_a_cache[i].obj_base
					&& (char*) obj < (char*)__libcrunch_is_a_cache[i].obj_limit
					&& 
					((diff == 0)
						|| (__libcrunch_is_a_cache[i].period != 0
							&& diff % __libcrunch_is_a_cache[i].period == 0)))
			{
				// hit -- make sure we're not the next victim
				if (unlikely(__libcrunch_is_a_cache_next_victim == i))
				{
					if (__libcrunch_is_a_cache_size > 0)
					{
						__libcrunch_is_a_cache_next_victim
						 = (__libcrunch_is_a_cache_next_victim + 1) % __libcrunch_is_a_cache_size;
					}
				}
				return &__libcrunch_is_a_cache[i];
			}
		}
	}
	return ((void*)0);
}

extern inline int (__attribute__((always_inline,gnu_inline)) __is_aU )(const void *obj, const void *uniqtype);
extern inline int (__attribute__((always_inline,gnu_inline)) __is_aU )(const void *obj, const void *uniqtype)
{
	if (!obj) 
	{ 
		return 1; 
	} 
	if (obj == (void*) -1) 
	{ 
		return 1; 
	} 
	// int inited = __libcrunch_check_init (); 
	// if (unlikely(inited == -1))
	// { 
	//	 return 1; 
	// } 
	
	/* Null uniqtype means __is_aS got a bad typestring, OR we're not  
	 * linked with enough uniqtypes data. */ 
	if (unlikely(!uniqtype))
	{ 
	   __libcrunch_begun++; 
	   __libcrunch_aborted_typestr++; 
		 return 1; 
	} 
	/* No need for the char check in the CIL version */ 
	// now we're really started 
	__libcrunch_begun++; 

	struct __libcrunch_is_a_cache_s *hit = __libcrunch_cache_lookup(obj, (struct uniqtype*) uniqtype);
	if (hit)
	{
		// hit!
		++__libcrunch_is_a_hit_cache;
		if (hit->result) 
		{
			++__libcrunch_succeeded;
			return 1;
		}
		else
		{
			// to make sure the error message and suppression handling happen,
			// we have to call the slow-path code.
			return __is_a_internal(obj, uniqtype); 
		}
		return 1; //__libcrunch_is_a_cache[i].result;
	}
	// miss: __is_a_internal will cache if it's cacheable
	int ret = __is_a_internal(obj, uniqtype); 
	
	return ret;
}

extern inline int (__attribute__((always_inline,gnu_inline)) __is_aS) (const void *obj, const char *typestr);
extern inline int (__attribute__((always_inline,gnu_inline)) __is_aS) (const void *obj, const char *typestr)
{
	if (!obj)
	{
		return 1;
	}
	if (obj == (void*) -1)
	{
		return 1;
	}
	// int inited = __libcrunch_check_init ();
	// if (unlikely(inited == -1))
	// {
	//	 return 1;
	// }

	const void * r = __libcrunch_typestr_to_uniqtype(typestr);

	int ret = __is_aU(obj, r);
	return ret;

}

extern inline int (__attribute__((always_inline,gnu_inline)) __like_aU )(const void *obj, const void *uniqtype);
extern inline int (__attribute__((always_inline,gnu_inline)) __like_aU )(const void *obj, const void *uniqtype)
{
	if (!obj) 
	{ 
		return 1; 
	} 
	if (obj == (void*) -1)
	{
		return 1;
	}
	// int inited = __libcrunch_check_init (); 
	// if (unlikely(inited == -1))
	// { 
	//	 return 1; 
	// } 
	
	/* Null uniqtype means __is_aS got a bad typestring, OR we're not  
	 * linked with enough uniqtypes data. */ 
	if (unlikely(!uniqtype))
	{ 
	   __libcrunch_begun++; 
	   __libcrunch_aborted_typestr++; 
		 return 1; 
	} 
	/* No need for the char check in the CIL version */ 
	// now we're really started 
	__libcrunch_begun++; 
	int ret = __like_a_internal(obj, uniqtype); 
	return ret;
}

extern inline int (__attribute__((always_inline,gnu_inline)) __named_aU )(const void *obj, const char *s);
extern inline int (__attribute__((always_inline,gnu_inline)) __named_aU )(const void *obj, const char *s)
{
	if (!obj)
	{
		return 1;
	}
	if (obj == (void*) -1)
	{
		return 1;
	}

	/* Null uniqtype means __is_aS got a bad typestring, OR we're not  
	 * linked with enough uniqtypes data. */
	if (unlikely(!s))
	{
		__libcrunch_begun++;
		__libcrunch_aborted_typestr++;
		return 1;
	}
	/* No need for the char check in the CIL version */ 
	// now we're really started 
	__libcrunch_begun++;
	int ret = __named_a_internal(obj, s);
	return ret;
}
extern inline int (__attribute__((always_inline,gnu_inline)) __is_a_function_refiningU )(const void *obj, const void *uniqtype);
extern inline int (__attribute__((always_inline,gnu_inline)) __is_a_function_refiningU )(const void *obj, const void *uniqtype)
{
	if (!obj)
	{
		return 1;
	}
	if (obj == (void*) -1)
	{
		return 1;
	}
	// int inited = __libcrunch_check_init ();
	// if (unlikely(inited == -1))
	// {
	//	 return 1;
	// }
	
	/* Null uniqtype means __is_aS got a bad typestring, OR we're not 
	 * linked with enough uniqtypes data. */
	if (unlikely(!uniqtype))
	{
		__libcrunch_begun++;
		__libcrunch_aborted_typestr++;
		return 1;
	}
	// now we're really started
	__libcrunch_begun++;
	int ret = __is_a_function_refining_internal(obj, uniqtype);
	return ret;
}
extern inline int (__attribute__((always_inline,gnu_inline)) __is_a_pointer_of_degree)(const void *obj, int d);
extern inline int (__attribute__((always_inline,gnu_inline)) __is_a_pointer_of_degree)(const void *obj, int d)
{
	if (!obj)
	{
		return 1;
	}
	if (obj == (void*) -1)
	{
		return 1;
	}
	if (d == 0) return 1;
	
	__libcrunch_begun++;
	int ret = __is_a_pointer_of_degree_internal(obj, d);
	return ret;
}
extern inline int (__attribute__((always_inline,gnu_inline)) __can_hold_pointer)(const void *target, const void *value);
extern inline int (__attribute__((always_inline,gnu_inline)) __can_hold_pointer)(const void *target, const void *value)
{
	if (!target)
	{
		return 1;
	}
	if (target == (void*) -1)
	{
		return 1;
	}
	if (!value)
	{
		return 1;
	}
	if (value == (void*) -1)
	{
		return 1;
	}
	__libcrunch_begun++;
	int ret = __can_hold_pointer_internal(target, value);
	return ret;
}
extern inline void *(__attribute__((always_inline,gnu_inline)) __libcrunch_trap)(const void *ptr, unsigned short tag);
extern inline void *(__attribute__((always_inline,gnu_inline)) __libcrunch_trap)(const void *ptr, unsigned short tag)
{
	/* use XOR to allow kernel-mode pointers too (even though we generally don't support these) */
	return (void *)(((unsigned long long) ptr) ^ (((unsigned long long) tag) << LIBCRUNCH_TRAP_TAG_SHIFT));
}
/* ONLY use untrap if you're *sure* you have a trapped pointer! */
extern inline void *(__attribute__((always_inline,gnu_inline)) __libcrunch_untrap)(const void *trapptr, unsigned short tag);
extern inline void *(__attribute__((always_inline,gnu_inline)) __libcrunch_untrap)(const void *trapptr, unsigned short tag)
{
	/* XOR is handy like this */
	return __libcrunch_trap(trapptr, tag);
}
/* We only use this one in pointer differencing and cast-to-integer. 
 * We return an unsigned long to avoid creating a pointless cast *back* to pointer. 
 * Instead, when doing pointer differencing, crunchbound takes on the task 
 * of the scaling the difference by the pointer target type size. */
extern inline unsigned long (__attribute__((always_inline,gnu_inline)) __libcrunch_detrap)(const void *any_ptr);
extern inline unsigned long (__attribute__((always_inline,gnu_inline)) __libcrunch_detrap)(const void *any_ptr)
{
	/* Recall that traps work by XORing with the non-canonical bits of the pointer,
	 * which may be 0 or 1. So to de-trap, we can't just unconditionally "clear" 
	 * or "set" those bits; it depends on whether the pointer is positive or negative.
	 * First clear them, then OR in all the bits again if it's negative. */
	unsigned long val = (unsigned long) any_ptr;
	return 
			(val & ~LIBCRUNCH_TRAP_MASK)
			| (((signed long) any_ptr) < 0 ? LIBCRUNCH_TRAP_MASK : 0);
}

extern inline int (__attribute__((always_inline,gnu_inline)) __libcrunch_is_trap_ptr)(const void *maybe_trap, unsigned short tag);
extern inline int (__attribute__((always_inline,gnu_inline)) __libcrunch_is_trap_ptr)(const void *maybe_trap, unsigned short tag)
{
	/* FIXME: this is all very archdep */
	signed long long trapi = (signed long long) maybe_trap;
	return (trapi > 0 && trapi >= (1ull << LIBCRUNCH_TRAP_TAG_SHIFT))
			|| (trapi < 0 && 
				/* Two's complement: the "most bits flipped" negative numbers are *closer* to 0,
				 * e.g. all-Fs is -1 */
				trapi <= -(1ull << LIBCRUNCH_TRAP_TAG_SHIFT));
	/* i.e. trap values are the really-really-positive and really-really-negative addresses. */
}
/*
extern inline int (__attribute__((always_inline,gnu_inline)) __libcrunch_bounds_invalid)(const __libcrunch_bounds_t *in);
extern inline int (__attribute__((always_inline,gnu_inline)) __libcrunch_bounds_invalid)(const __libcrunch_bounds_t *in)
{
	return in->base == (void*) -1;
}
*/
extern inline __libcrunch_bounds_t (__attribute__((always_inline,gnu_inline)) __make_bounds)(unsigned long base, unsigned long limit);
extern inline __libcrunch_bounds_t (__attribute__((always_inline,gnu_inline)) __make_bounds)(unsigned long base, unsigned long limit)
{
#ifdef LIBCRUNCH_WORDSIZE_BOUNDS
	return (__libcrunch_bounds_t) {
			((unsigned long) base) >> 32,
			((unsigned long) limit) >> 32
	};
#else
	return (__libcrunch_bounds_t) { base, limit };
#endif
}

extern inline __libcrunch_bounds_t (__attribute__((always_inline,gnu_inline)) __libcrunch_max_bounds)(const void *ptr);
extern inline __libcrunch_bounds_t (__attribute__((always_inline,gnu_inline)) __libcrunch_max_bounds)(const void *ptr)
{
#ifdef LIBCRUNCH_WORDSIZE_BOUNDS
	/* Max bounds are when the two values are equal, 
	 * because this always represents a 4GB region.
	 * WHERE should we centre the bounds?
	 * Let's play it safe and make them obj += 2GB.
	 * Note that in extreme cases, this will actually not permit
	 * some accesses which the intention of "max bounds" is to include. */
	return __make_bounds(((unsigned long) obj) - 1ul<<31, ((unsigned long) obj) + 1ul<<31);
#else
	return (__libcrunch_bounds_t) { (unsigned long) 0, (unsigned long) -1 };
#endif
}

#define _CLEAR_UPPER_32(i) \
(((unsigned long) (i)) & ((1ul<<32)-1ul))
#define _CLEAR_LOWER_32(i) \
(((unsigned long) (i)) & ~((1ul<<32)-1ul))

extern inline void * (__attribute__((always_inline,gnu_inline)) __libcrunch_get_base)(__libcrunch_bounds_t *p_bounds, const void *derivedfrom);
extern inline void * (__attribute__((always_inline,gnu_inline)) __libcrunch_get_base)(__libcrunch_bounds_t *p_bounds, const void *derivedfrom)
{
#ifdef LIBCRUNCH_WORDSIZE_BOUNDS
	/* The bounds are storing only the lower bits of the base.
	 * If they're <= derivedfrom's lower 32, they are shared with the base.
	 * Otherwise, the base is 4GB lower.
	 * (base <= derivedfrom < limit)
	 */
	if (likely(p_bounds->base <= _CLEAR_UPPER_32(derivedfrom)))
	{
		return (void*) (_CLEAR_LOWER_32(derivedfrom) + p_bounds->base);
	} else return (void*) (_CLEAR_LOWER_32(derivedfrom) - 0x100000000ul + p_bounds->base);
#else
	return (void*) p_bounds->base;
#endif
}

extern inline void * (__attribute__((always_inline,gnu_inline)) __libcrunch_get_limit)(__libcrunch_bounds_t *p_bounds, const void *derivedfrom);
extern inline void * (__attribute__((always_inline,gnu_inline)) __libcrunch_get_limit)(__libcrunch_bounds_t *p_bounds, const void *derivedfrom)
{
#ifdef LIBCRUNCH_WORDSIZE_BOUNDS
	/* The bounds are storing only the lower bits of the limit.
	 * If derivedfrom's lower 32 < those bits, they are shared with the limit.
	 * Otherwise, the limit is 4GB higher.
	 * (base <= derivedfrom < limit)
	 */
	if (likely(_CLEAR_UPPER_32(derivedfrom) < limit))
	{
		return (void*) (_CLEAR_LOWER_32(derivedfrom) + p_bounds->limit);
	} else return (void*) (_CLEAR_LOWER_32(derivedfrom) + 0x100000000ul + p_bounds->limit);
#else
	return (void*) p_bounds->limit;
#endif
}

extern inline __libcrunch_bounds_t (__attribute__((always_inline,gnu_inline)) __libcrunch_make_invalid_bounds)(const void *ptr);
extern inline __libcrunch_bounds_t (__attribute__((always_inline,gnu_inline)) __libcrunch_make_invalid_bounds)(const void *ptr)
{
#ifdef LIBCRUNCH_WORDSIZE_BOUNDS
	/* Bounds/ptr orderings:
	 * low               high
	 *      B  P  L               normal
	 *      B  L  P               > 4 GB a.k.a. invalid
	 *      P  B  L               > 4 GB a.k.a. invalid
	 *      P  L  B               base is lower
	 *      L  B  P               limit is higher
	 *      L  P  B               > 4 GB
	 * 
	 * so, in short
	 *   try      p-2, p-1
	 *   and if   p == 0, we're okay because we get   P B L  which is also invalid
	 *   but if   p == 1  we also go for P B L, so try p-3, p-2
	 * 
	 * This means we can recognise invalid bounds as numbers just above p,
	 * or, for low p, high positive numbers.
	 */
	unsigned p_low = (unsigned) _CLEAR_UPPER_32(ptr);
	
	if (likely(p_low != 1))
	{
		   return (__libcrunch_bounds_t) { p_low - 2, p_low - 1 };
	} else return (__libcrunch_bounds_t) { p_low - 3, p_low - 2 };
#else
	return __make_bounds((unsigned long) -1l, 0ul);
#endif
}

extern inline _Bool (__attribute__((always_inline,gnu_inline)) __libcrunch_bounds_invalid)(__libcrunch_bounds_t bounds, const void *ptr);
extern inline _Bool (__attribute__((always_inline,gnu_inline)) __libcrunch_bounds_invalid)(__libcrunch_bounds_t bounds, const void *ptr)
{
#ifdef LIBCRUNCH_WORDSIZE_BOUNDS
	/* Bounds are invalid if
	 * base is above ptr_l but below limit, or
	 * limit is below ptr but above base.
	 * 
	 * These bounds are invalid because they denote an object bigger than 4GB.
	 */
	unsigned base = bounds.base;
	unsigned limit = bounds.limit;
	unsigned ptr_l = (unsigned) _CLEAR_UPPER_32(ptr);
	return (base > ptr_l && base < limit)
			|| (limit < ptr_l && limit > base);
#else
	return bounds.limit <= bounds.base;
#endif
}


#undef _CLEAR_LOWER_32
#undef _CLEAR_UPPER_32

extern inline __libcrunch_bounds_t (__attribute__((always_inline,gnu_inline)) __fetch_bounds)(const void *ptr, struct uniqtype *t);
extern inline __libcrunch_bounds_t (__attribute__((always_inline,gnu_inline)) __fetch_bounds)(const void *ptr, struct uniqtype *t)
{
	/* We understand trap ptrs */
	const void *testptr;
	if (unlikely(__libcrunch_is_trap_ptr(ptr, LIBCRUNCH_TRAP_ONE_PAST)))
	{
		testptr = (const char*) ptr - t->pos_maxoff;
	} else testptr = ptr;
	/* If we hit the cache, we can return an answer inline. */
	struct __libcrunch_is_a_cache_s *hit = __libcrunch_cache_lookup(testptr, t);
	if (hit)
	{
		/* Is ptr actually a t? If not, we're in trouble!
		 * Maybe it's one-past and we just de-trapped it? 
		 * NO, we handle that in caller. */
		//if (unlikely(!hit->result, 0))
		//{
		//	// loop: goto loop; /* internal error! this shouldn't happen! etc. */
		//	__asm__ volatile ("ud2");
		//}
		/* HMM. Commented this abort out since I'm now less sure that it's a good idea.
		 * See the bounds-toint test case. */
		/* Does "obj" include "derivedfrom"? */
		return __make_bounds((unsigned long) hit->obj_base, (unsigned long) hit->obj_limit);
	}
	/* Else if libcrunch is linked in, we can delegate to it. */
	return __fetch_bounds_internal(ptr, t);
}
extern inline int (__attribute__((always_inline,gnu_inline)) __check_derive_ptr)(void **p_derived, const void *derivedfrom, /* __libcrunch_bounds_t *opt_derived_bounds, */ __libcrunch_bounds_t *opt_derivedfrom_bounds, struct uniqtype *t);
extern inline int (__attribute__((always_inline,gnu_inline)) __check_derive_ptr)(void **p_derived, const void *derivedfrom, /* __libcrunch_bounds_t *opt_derived_bounds, */ __libcrunch_bounds_t *opt_derivedfrom_bounds, struct uniqtype *t)
{
	/* PRECONDITIONS (a.k.a. things we don't need to check here): 
	 * - derivedfrom is an in-bounds pointer that really does point to a T
	   ... OR is a trap value
	 * - the byte-address derived is a multiple of t->pos_maxoff away from derivedfrom
	   ... AFTER it is converted back from a trap value
	 */
	
	// FIXME: what if *p_derived is a trap? 
	// i.e. we just did pointer arithmetic on one?
	// Then we should de-trap it at the same time we de-trap derivedfrom, I think
	
	// deriving a null pointer or MAP_FAILED is okay, I suppose? don't allow it, for now
	// if (!derived || derived == (void*) -1) goto out;
	
	/* If we have derived-from bounds, we can handle it inline. Note that if derivedfrom
	 * is really a trapvalue, its bounds should still be the actual object bounds. 
	 * 
	 * ALSO note that we should always have a derivedfrom bounds *object*, because our
	 * instrumentation makes sure that pointer adjustments are local-to-local operations.
	 * If we load a pointer from the heap, then adjust it, it happens in two steps,
	 * and the latter is local-to-local. 
	 * What we might not have is valid derivedfrom bounds *information*. If the object
	 * has not been filled yet, it's our job to do it. */
	// assert(opt_derivedfrom_bounds);
	__libcrunch_bounds_t bounds;
	if (opt_derivedfrom_bounds && !__libcrunch_bounds_invalid(*opt_derivedfrom_bounds, derivedfrom))
	{
		bounds = *opt_derivedfrom_bounds;
	}
	else
	{
		bounds = __fetch_bounds(derivedfrom, t);
	}
	
	if (unlikely(__libcrunch_is_trap_ptr(derivedfrom, LIBCRUNCH_TRAP_ONE_PAST)))
	{
		/* de-trap derivedfrom */
		derivedfrom = __libcrunch_untrap(derivedfrom, LIBCRUNCH_TRAP_ONE_PAST);
	}
	
	// too low?
	if (unlikely((char*) *p_derived < (char*) __libcrunch_get_base(&bounds, derivedfrom))) { goto out_fail; }
	// NOTE: support for one-prev pointers as trap values goes here

	// too high?
	if (unlikely((char*) *p_derived > (char*) __libcrunch_get_limit(&bounds, derivedfrom))) { goto out_fail; }
	
	// FIXME: experiment with Austin et al's "unsigned subtraction" hack here

	// "one past"?
	if ((char*) *p_derived == (char*) __libcrunch_get_limit(&bounds, derivedfrom))
	{
		*p_derived = __libcrunch_trap(*p_derived, LIBCRUNCH_TRAP_ONE_PAST);
	}
	
	/* That's it! */
out:
	return 1;
out_fail:
	/* Don't fail here; print a warning and return a trapped pointer */
	__libcrunch_bounds_error(*p_derived, derivedfrom, bounds);
	*p_derived = __libcrunch_trap(*p_derived, LIBCRUNCH_TRAP_INVALID);
	return 1;
}

#ifdef __libcrunch_defined_unlikely
#undef unlikely
#endif
#ifdef __libcrunch_defined_likely
#undef likely
#endif

#endif
