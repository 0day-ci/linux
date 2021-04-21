/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Generic implementation of 64-bit atomics using spinlocks,
 * useful on processors that don't have 64-bit atomic instructions.
 *
 * Copyright © 2009 Paul Mackerras, IBM Corp. <paulus@au1.ibm.com>
 */
#ifndef _ASM_GENERIC_ATOMIC64_H
#define _ASM_GENERIC_ATOMIC64_H
#include <linux/types.h>

typedef struct {
	s64 counter;
} atomic64_t;

#define ATOMIC64_INIT(i)	{ (i) }

extern s64 atomic64_read(const atomic64_t *v);
extern void atomic64_set(atomic64_t *v, s64 i);

#define atomic64_set_release(v, i)	atomic64_set((v), (i))

#define ATOMIC64_OP(op)							\
extern void	 atomic64_##op(s64 a, atomic64_t *v);

#define ATOMIC64_OP_RETURN(op)						\
extern s64 atomic64_##op##_return(s64 a, atomic64_t *v);

#define ATOMIC64_FETCH_OP(op)						\
extern s64 atomic64_fetch_##op(s64 a, atomic64_t *v);

#define ATOMIC64_OPS(op)	ATOMIC64_OP(op) ATOMIC64_OP_RETURN(op) ATOMIC64_FETCH_OP(op)

ATOMIC64_OPS(add)
ATOMIC64_OPS(sub)

#define atomic64_add_relaxed atomic64_add
#define atomic64_add_acquire atomic64_add
#define atomic64_add_release atomic64_add

#define atomic64_add_return_relaxed atomic64_add_return
#define atomic64_add_return_acquire atomic64_add_return
#define atomic64_add_return_release atomic64_add_return

#define atomic64_fetch_add_relaxed atomic64_fetch_add
#define atomic64_fetch_add_acquire atomic64_fetch_add
#define atomic64_fetch_add_release atomic64_fetch_add

#undef ATOMIC64_OPS
#define ATOMIC64_OPS(op)	ATOMIC64_OP(op) ATOMIC64_FETCH_OP(op)

ATOMIC64_OPS(and)
ATOMIC64_OPS(or)
ATOMIC64_OPS(xor)

#undef ATOMIC64_OPS
#undef ATOMIC64_FETCH_OP
#undef ATOMIC64_OP_RETURN
#undef ATOMIC64_OP

extern s64 atomic64_dec_if_positive(atomic64_t *v);
#define atomic64_dec_if_positive atomic64_dec_if_positive
extern s64 atomic64_cmpxchg(atomic64_t *v, s64 o, s64 n);
#define atomic64_cmpxchg_relaxed atomic64_cmpxchg
#define atomic64_cmpxchg_acquire atomic64_cmpxchg
#define atomic64_cmpxchg_release atomic64_cmpxchg
extern s64 atomic64_xchg(atomic64_t *v, s64 new);
#define atomic64_xchg_relaxed atomic64_xchg
#define atomic64_xchg_acquire atomic64_xchg
#define atomic64_xchg_release atomic64_xchg
extern s64 atomic64_fetch_add_unless(atomic64_t *v, s64 a, s64 u);
#define atomic64_fetch_add_unless atomic64_fetch_add_unless

static __always_inline void
atomic64_inc(atomic64_t *v)
{
	atomic64_add(1, v);
}

static __always_inline s64
atomic64_inc_return(atomic64_t *v)
{
	return atomic64_add_return(1, v);
}

static __always_inline s64
atomic64_fetch_inc(atomic64_t *v)
{
	return atomic64_fetch_add(1, v);
}

static __always_inline void
atomic64_dec(atomic64_t *v)
{
	atomic64_sub(1, v);
}

static __always_inline s64
atomic64_dec_return(atomic64_t *v)
{
	return atomic64_sub_return(1, v);
}

static __always_inline s64
atomic64_fetch_dec(atomic64_t *v)
{
	return atomic64_fetch_sub(1, v);
}

static __always_inline void
atomic64_andnot(s64 i, atomic64_t *v)
{
	atomic64_and(~i, v);
}

static __always_inline s64
atomic64_fetch_andnot(s64 i, atomic64_t *v)
{
	return atomic64_fetch_and(~i, v);
}

static __always_inline bool
atomic64_sub_and_test(int i, atomic64_t *v)
{
	return atomic64_sub_return(i, v) == 0;
}

static __always_inline bool
atomic64_dec_and_test(atomic64_t *v)
{
	return atomic64_dec_return(v) == 0;
}

static __always_inline bool
atomic64_inc_and_test(atomic64_t *v)
{
	return atomic64_inc_return(v) == 0;
}

static __always_inline bool
atomic64_add_negative(s64 i, atomic64_t *v)
{
	return atomic64_add_return(i, v) < 0;
}
#endif  /*  _ASM_GENERIC_ATOMIC64_H  */
