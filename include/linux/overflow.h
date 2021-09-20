/* SPDX-License-Identifier: GPL-2.0 OR MIT */
#ifndef __LINUX_OVERFLOW_H
#define __LINUX_OVERFLOW_H

#include <linux/compiler.h>
#include <linux/limits.h>

/*
 * We need to compute the minimum and maximum values representable in a given
 * type. These macros may also be useful elsewhere. It would seem more obvious
 * to do something like:
 *
 * #define type_min(T) (T)(is_signed_type(T) ? (T)1 << (8*sizeof(T)-1) : 0)
 * #define type_max(T) (T)(is_signed_type(T) ? ((T)1 << (8*sizeof(T)-1)) - 1 : ~(T)0)
 *
 * Unfortunately, the middle expressions, strictly speaking, have
 * undefined behaviour, and at least some versions of gcc warn about
 * the type_max expression (but not if -fsanitize=undefined is in
 * effect; in that case, the warning is deferred to runtime...).
 *
 * The slightly excessive casting in type_min is to make sure the
 * macros also produce sensible values for the exotic type _Bool. [The
 * overflow checkers only almost work for _Bool, but that's
 * a-feature-not-a-bug, since people shouldn't be doing arithmetic on
 * _Bools. Besides, the gcc builtins don't allow _Bool* as third
 * argument.]
 *
 * Idea stolen from
 * https://mail-index.netbsd.org/tech-misc/2007/02/05/0000.html -
 * credit to Christian Biere.
 */
#define is_signed_type(type)       (((type)(-1)) < (type)1)
#define __type_half_max(type) ((type)1 << (8*sizeof(type) - 1 - is_signed_type(type)))
#define type_max(T) ((T)((__type_half_max(T) - 1) + __type_half_max(T)))
#define type_min(T) ((T)((T)-type_max(T)-(T)1))

/*
 * Avoids triggering -Wtype-limits compilation warning,
 * while using unsigned data types to check a < 0.
 */
#define is_non_negative(a) ((a) > 0 || (a) == 0)
#define is_negative(a) (!(is_non_negative(a)))

/*
 * Allows for effectively applying __must_check to a macro so we can have
 * both the type-agnostic benefits of the macros while also being able to
 * enforce that the return value is, in fact, checked.
 */
static inline bool __must_check __must_check_overflow(bool overflow)
{
	return unlikely(overflow);
}

/*
 * For simplicity and code hygiene, the fallback code below insists on
 * a, b and *d having the same type (similar to the min() and max()
 * macros), whereas gcc's type-generic overflow checkers accept
 * different types. Hence we don't just make check_add_overflow an
 * alias for __builtin_add_overflow, but add type checks similar to
 * below.
 */
#define check_add_overflow(a, b, d) __must_check_overflow(({	\
	typeof(a) __a = (a);			\
	typeof(b) __b = (b);			\
	typeof(d) __d = (d);			\
	(void) (&__a == &__b);			\
	(void) (&__a == __d);			\
	__builtin_add_overflow(__a, __b, __d);	\
}))

#define check_sub_overflow(a, b, d) __must_check_overflow(({	\
	typeof(a) __a = (a);			\
	typeof(b) __b = (b);			\
	typeof(d) __d = (d);			\
	(void) (&__a == &__b);			\
	(void) (&__a == __d);			\
	__builtin_sub_overflow(__a, __b, __d);	\
}))

#define check_mul_overflow(a, b, d) __must_check_overflow(({	\
	typeof(a) __a = (a);			\
	typeof(b) __b = (b);			\
	typeof(d) __d = (d);			\
	(void) (&__a == &__b);			\
	(void) (&__a == __d);			\
	__builtin_mul_overflow(__a, __b, __d);	\
}))

/** check_shl_overflow() - Calculate a left-shifted value and check overflow
 *
 * @a: Value to be shifted
 * @s: How many bits left to shift
 * @d: Pointer to where to store the result
 *
 * Computes *@d = (@a << @s)
 *
 * Returns true if '*d' cannot hold the result or when 'a << s' doesn't
 * make sense. Example conditions:
 * - 'a << s' causes bits to be lost when stored in *d.
 * - 's' is garbage (e.g. negative) or so large that the result of
 *   'a << s' is guaranteed to be 0.
 * - 'a' is negative.
 * - 'a << s' sets the sign bit, if any, in '*d'.
 *
 * '*d' will hold the results of the attempted shift, but is not
 * considered "safe for use" if true is returned.
 */
#define check_shl_overflow(a, s, d) __must_check_overflow(({		\
	typeof(a) _a = a;						\
	typeof(s) _s = s;						\
	typeof(d) _d = d;						\
	u64 _a_full = _a;						\
	unsigned int _to_shift =					\
		is_non_negative(_s) && _s < 8 * sizeof(*d) ? _s : 0;	\
	*_d = (_a_full << _to_shift);					\
	(_to_shift != _s || is_negative(*_d) || is_negative(_a) ||	\
	(*_d >> _to_shift) != _a);					\
}))

/*
 * As with __must_check_overflow() above, this is used to wrap the
 * size_t output from a statement expression macro.
 */
static inline size_t __must_check __must_check_size(size_t size)
{
	return size;
}

/*
 * Internal logic for size_mul(). Takes variable names from UNIQUE_ID
 * so that the local variables here will never collide with other local
 * variables (for example, with itself).
 */
#define __size_mul(factor1, factor2, __factor1, __factor2, __product)	\
({									\
	size_t __product;						\
	size_t __factor1 = (factor1);					\
	size_t __factor2 = (factor2);					\
	if (check_mul_overflow(__factor1, __factor2, &__product))	\
		__product = SIZE_MAX;					\
	__product;							\
})

/**
 * size_mul() - Calculate size_t multiplication with saturation at SIZE_MAX
 *
 * @factor1: first factor
 * @factor2: second factor
 *
 * Returns: calculate @factor1 * @factor2, where both values are
 * evaluated as size_t, with any overflow causing the return value to
 * be SIZE_MAX.
 */
#define size_mul(factor1, factor2)					\
	__must_check_size(__size_mul(factor1, factor2,			\
				     __UNIQUE_ID(__factor1_),		\
				     __UNIQUE_ID(__factor2_),		\
				     __UNIQUE_ID(__product_)))

/*
 * Internal logic for size_add(). Takes variable names from UNIQUE_ID
 * so that the local variables here will never collide with other local
 * variables (for example, with itself).
 */
#define __size_add(addend1, addend2, __addend1, __addend2, __sum)	\
({									\
	size_t __sum;							\
	size_t __addend1 = (addend1);					\
	size_t __addend2 = (addend2);					\
	if (check_add_overflow(__addend1, __addend2, &__sum))		\
		__sum = SIZE_MAX;					\
	__sum;								\
})

/**
 * size_add() - Calculate size_t addition with saturation at SIZE_MAX
 *
 * @addend1: first addend
 * @addend2: second addend
 *
 * Returns: calculate @addend1 + @addend2, where both values are
 * evaluated as size_t, with any overflow causing the return value to
 * be SIZE_MAX.
 */
#define size_add(addend1, addend2)					\
	__must_check_size(__size_add(addend1, addend2,			\
				     __UNIQUE_ID(__addend1_),		\
				     __UNIQUE_ID(__addend2_),		\
				     __UNIQUE_ID(__sum_)))

/**
 * array_size() - Calculate size of 2-dimensional array.
 *
 * @a: dimension one
 * @b: dimension two
 *
 * Calculates size of 2-dimensional array: @a * @b.
 *
 * Returns: number of bytes needed to represent the array or SIZE_MAX on
 * overflow.
 */
#define array_size(a, b)	size_mul(a, b)

/**
 * array3_size() - Calculate size of 3-dimensional array.
 *
 * @a: dimension one
 * @b: dimension two
 * @c: dimension three
 *
 * Calculates size of 3-dimensional array: @a * @b * @c.
 *
 * Returns: number of bytes needed to represent the array or SIZE_MAX on
 * overflow.
 */
#define array3_size(a, b, c)	size_mul(size_mul(a, b), c)

/**
 * flex_array_size() - Calculate size of a flexible array member
 *                     within an enclosing structure.
 *
 * @p: Pointer to the structure.
 * @member: Name of the flexible array member.
 * @count: Number of elements in the array.
 *
 * Calculates size of a flexible array of @count number of @member
 * elements, at the end of structure @p.
 *
 * Return: number of bytes needed or SIZE_MAX on overflow.
 */
#define flex_array_size(p, member, count)				\
	size_mul(count,							\
		 sizeof(*(p)->member) + __must_be_array((p)->member))

/**
 * struct_size() - Calculate size of structure with trailing flexible array.
 *
 * @p: Pointer to the structure.
 * @member: Name of the array member.
 * @count: Number of elements in the array.
 *
 * Calculates size of memory needed for structure @p followed by an
 * array of @count number of @member elements.
 *
 * Return: number of bytes needed or SIZE_MAX on overflow.
 */
#define struct_size(p, member, count)					\
	size_add(sizeof(*(p)), flex_array_size(p, member, count))

#endif /* __LINUX_OVERFLOW_H */
