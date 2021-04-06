/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Helpers for expanding variadic macros in useful ways
 *
 * 2021 by Marek Behún <kabel@kernel.org>
 */

#ifndef __VARIADIC_MACRO_H__
#define __VARIADIC_MACRO_H__

/*
 * Helpers for variadic macros with up to 32 arguments.
 * Pretty simple to extend.
 */

#define _VM_GET_NTH_ARG(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, \
	_13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, \
	_27, _28, _29, _30, _31, _32, N, ...) N

#define _VM_HELP_0(_f, _d, ...)
#define _VM_HELP_1(_f, _d, x, ...) _f(x, _d)
#define _VM_HELP_2(_f, _d, x, ...) _f(x, _d) _VM_HELP_1(_f, _d, ##__VA_ARGS__)
#define _VM_HELP_3(_f, _d, x, ...) _f(x, _d) _VM_HELP_2(_f, _d, ##__VA_ARGS__)
#define _VM_HELP_4(_f, _d, x, ...) _f(x, _d) _VM_HELP_3(_f, _d, ##__VA_ARGS__)
#define _VM_HELP_5(_f, _d, x, ...) _f(x, _d) _VM_HELP_4(_f, _d, ##__VA_ARGS__)
#define _VM_HELP_6(_f, _d, x, ...) _f(x, _d) _VM_HELP_5(_f, _d, ##__VA_ARGS__)
#define _VM_HELP_7(_f, _d, x, ...) _f(x, _d) _VM_HELP_6(_f, _d, ##__VA_ARGS__)
#define _VM_HELP_8(_f, _d, x, ...) _f(x, _d) _VM_HELP_7(_f, _d, ##__VA_ARGS__)
#define _VM_HELP_9(_f, _d, x, ...) _f(x, _d) _VM_HELP_8(_f, _d, ##__VA_ARGS__)
#define _VM_HELP_10(_f, _d, x, ...) _f(x, _d) _VM_HELP_9(_f, _d, ##__VA_ARGS__)
#define _VM_HELP_11(_f, _d, x, ...) _f(x, _d) _VM_HELP_10(_f, _d, ##__VA_ARGS__)
#define _VM_HELP_12(_f, _d, x, ...) _f(x, _d) _VM_HELP_11(_f, _d, ##__VA_ARGS__)
#define _VM_HELP_13(_f, _d, x, ...) _f(x, _d) _VM_HELP_12(_f, _d, ##__VA_ARGS__)
#define _VM_HELP_14(_f, _d, x, ...) _f(x, _d) _VM_HELP_13(_f, _d, ##__VA_ARGS__)
#define _VM_HELP_15(_f, _d, x, ...) _f(x, _d) _VM_HELP_14(_f, _d, ##__VA_ARGS__)
#define _VM_HELP_16(_f, _d, x, ...) _f(x, _d) _VM_HELP_15(_f, _d, ##__VA_ARGS__)
#define _VM_HELP_17(_f, _d, x, ...) _f(x, _d) _VM_HELP_16(_f, _d, ##__VA_ARGS__)
#define _VM_HELP_18(_f, _d, x, ...) _f(x, _d) _VM_HELP_17(_f, _d, ##__VA_ARGS__)
#define _VM_HELP_19(_f, _d, x, ...) _f(x, _d) _VM_HELP_18(_f, _d, ##__VA_ARGS__)
#define _VM_HELP_20(_f, _d, x, ...) _f(x, _d) _VM_HELP_19(_f, _d, ##__VA_ARGS__)
#define _VM_HELP_21(_f, _d, x, ...) _f(x, _d) _VM_HELP_20(_f, _d, ##__VA_ARGS__)
#define _VM_HELP_22(_f, _d, x, ...) _f(x, _d) _VM_HELP_21(_f, _d, ##__VA_ARGS__)
#define _VM_HELP_23(_f, _d, x, ...) _f(x, _d) _VM_HELP_22(_f, _d, ##__VA_ARGS__)
#define _VM_HELP_24(_f, _d, x, ...) _f(x, _d) _VM_HELP_23(_f, _d, ##__VA_ARGS__)
#define _VM_HELP_25(_f, _d, x, ...) _f(x, _d) _VM_HELP_24(_f, _d, ##__VA_ARGS__)
#define _VM_HELP_26(_f, _d, x, ...) _f(x, _d) _VM_HELP_25(_f, _d, ##__VA_ARGS__)
#define _VM_HELP_27(_f, _d, x, ...) _f(x, _d) _VM_HELP_26(_f, _d, ##__VA_ARGS__)
#define _VM_HELP_28(_f, _d, x, ...) _f(x, _d) _VM_HELP_27(_f, _d, ##__VA_ARGS__)
#define _VM_HELP_29(_f, _d, x, ...) _f(x, _d) _VM_HELP_28(_f, _d, ##__VA_ARGS__)
#define _VM_HELP_30(_f, _d, x, ...) _f(x, _d) _VM_HELP_29(_f, _d, ##__VA_ARGS__)
#define _VM_HELP_31(_f, _d, x, ...) _f(x, _d) _VM_HELP_30(_f, _d, ##__VA_ARGS__)

/*
 * EXPAND_FOR_EACH(_functor, _data, args...) - expand a call to functor for
 *					       each argument
 * @_functor:	name of the functor to expand a call to
 * @_data:	private data to apply to the functor
 * @args...:	for each of these arguments a call to functor will be expanded
 *
 * This macro will expand to a call to _functor(arg, _data) for all arguments
 * after the _data argument, i.e.
 *   EXPAND_FOR_EACH(f, d, 1, 2, 3)
 * will expand to
 *   f(1, d) f(2, d) f(3, d)
 *
 * ATTENTION: the functor cannot call EXPAND_FOR_EACH again.
 * For recursive calls to EXPAND_FOR_EACH to be allowed, use EXPAND_FOR_EACH_R
 * together with EXPAND_EVAL.
 * Or you can call EXPAND_FOR_EACH in the functor for EXPAND_FOR_EACH_PASS_ARGS.
 *
 * Example:
 *
 * ::
 *
 *   #define __BIT_OR(x, dummy) | BIT(x)
 *   #define BIT_OR(...) (0 EXPAND_FOR_EACH(__BIT_OR, dummy, ##__VA_ARGS__))
 *
 *   int x = BIT_OR(1, 7, 9, 13);
 *   assert(x == (BIT(1) | BIT(7) | BIT(9) | BIT(13)));
 */
#define EXPAND_FOR_EACH(_functor, _data, ...)				 \
	_VM_GET_NTH_ARG("", ##__VA_ARGS__, _VM_HELP_31, _VM_HELP_30,	 \
	_VM_HELP_29, _VM_HELP_28, _VM_HELP_27, _VM_HELP_26, _VM_HELP_25, \
	_VM_HELP_24, _VM_HELP_23, _VM_HELP_22, _VM_HELP_21, _VM_HELP_20, \
	_VM_HELP_19, _VM_HELP_18, _VM_HELP_17, _VM_HELP_16, _VM_HELP_15, \
	_VM_HELP_14, _VM_HELP_13, _VM_HELP_12, _VM_HELP_11, _VM_HELP_10, \
	_VM_HELP_9, _VM_HELP_8, _VM_HELP_7, _VM_HELP_6, _VM_HELP_5,	 \
	_VM_HELP_4, _VM_HELP_3, _VM_HELP_2, _VM_HELP_1,			 \
	_VM_HELP_0)(_functor, _data, ##__VA_ARGS__)

/*
 * Black magic to prevent macros from creating disabling contexts
 */
#define _VM_EMPTY(...)
#define _VM_DEFER(...) __VA_ARGS__ _VM_EMPTY()

#define EXPAND_EVAL(...) _VM_EVAL1(_VM_EVAL1(_VM_EVAL1(__VA_ARGS__)))
#define _VM_EVAL1(...) _VM_EVAL2(_VM_EVAL2(_VM_EVAL2(__VA_ARGS__)))
#define _VM_EVAL2(...) _VM_EVAL3(_VM_EVAL3(_VM_EVAL3(__VA_ARGS__)))
#define _VM_EVAL3(...) __VA_ARGS__

#define _VM_APPLY(x, _functor, _data, ...) _functor(x, _data, ##__VA_ARGS__)
#define _VM_APPLY_INDIRECT() _VM_APPLY
#define _VM_APPLY_HELPER(x, __args) \
	_VM_DEFER(_VM_APPLY_INDIRECT)()(x, _VM_DEFER __args)

#define _VM_EXPAND_FOR_EACH_INDIRECT() EXPAND_FOR_EACH

/*
 * EXPAND_FOR_EACH_R(_functor, _data, args...) - expand a call to functor for
 *						 each argument, recursive calls
 *						 allowed
 * @_functor:	name of the functor to expand a call to
 * @_data:	private data to apply to the functor
 * @args...:	for each of these arguments a call to functor will be expanded
 *
 * Does the same as EXPAND_FOR_EACH(_functor, _data, args...), but _functor is
 * allowed to call EXPAND_FOR_EACH_R again.
 *
 * ATTENTION: in order for this to work, the outermost call to this macro must
 * be wrapped inside the EXPAND_EVAL macro.
 *
 * Example:
 *
 * ::
 *
 *   #define EXP_3(y, x) x ## y
 *   #define EXP_2(x, ...) EXPAND_FOR_EACH_R(EXP_3, x, ##__VA_ARGS__)
 *   #define EXP_1(x, dummy) EXP_2(x, 1, 2, 3)
 *   #define EXP(...) EXPAND_EVAL(EXPAND_FOR_EACH_R(EXP_1, dummy, ##__VA_ARGS__))
 *
 *   EXP(a, b, c)
 *   // expands to: a1 a2 a3 b1 b2 b3 c1 c2 c3
 */
#define EXPAND_FOR_EACH_R _VM_DEFER(_VM_EXPAND_FOR_EACH_INDIRECT)()

#define _VM_EXPAND_FOR_EACH_PASS_ARGS(_functor, _data, ...) \
	_VM_DEFER(_VM_EXPAND_FOR_EACH_INDIRECT)()(_VM_APPLY_HELPER, \
	(_functor, _data, ##__VA_ARGS__), ##__VA_ARGS__)

/*
 * EXPAND_FOR_EACH_PASS_ARGS(_functor, _data, args...) -
 *   expand a call to functor for each argument, pass all arguments to each call
 * @_functor:	name of the functor to expand a call to
 * @_data:	private data to apply to the functor
 * @args...:	for each of these arguments a call to functor will be expanded
 *
 * For each argument after the _data argument, a call to
 * _functor(arg, _data, args...) will be expanded.
 * This is similar to EXPAND_FOR_EACH, but each expanded call will also be
 * passed all the original arguments, i.e.
 *   EXPAND_FOR_EACH_PASS_ARGS(f, d, 1, 2, 3)
 * will expand to
 *   f(1, d, 1, 2, 3) f(2, d, 1, 2, 3) f(3, d, 1, 2, 3)
 *
 * This is useful when one needs to call EXPAND_FOR_EACH in the functor again,
 * to expand all the arguments in some way for each argument.
 *
 * ATTENTION: the functor cannot call EXPAND_FOR_EACH_PASS_ARGS again.
 * For recursive calls to EXPAND_FOR_EACH_PASS_ARGS to be allowed, use
 * EXPAND_FOR_EACH_PASS_ARGS_R together with EXPAND_EVAL.
 *
 * Example (a mechanism to compile time bitmap initialization):
 *
 * ::
 *
 *   #define __BIT_OR(x, dummy) | BIT(x)
 *   #define BIT_OR(...) (0 EXPAND_FOR_EACH(__BIT_OR, dummy, ##__VA_ARGS__))
 *   #define BM_MEMB(bit) ((bit) / BITS_PER_LONG)
 *   #define BM_OR(bit, member) \
 *     | (BM_MEMB(bit) == member ? BIT((bit) % BITS_PER_LONG) : 0)
 *   #define INIT_BITMAP_MEMBER(bit, dummy, ...) \
 *     [BM_MEMB(bit)] = (0 EXPAND_FOR_EACH(BM_OR, BM_MEMB(bit), ##__VA_ARGS__)),
 *   #define INIT_BITMAP(...) \
 *     { \
 *       EXPAND_FOR_EACH_PASS_ARGS(INIT_BITMAP_MEMBER, dummy, ##__VA_ARGS__) \
 *     }
 *
 *   static const DECLARE_BITMAP(bm, 90) =
 *     INIT_BITMAP(1, 2, 15, 32, 36, 80);
 */
#define EXPAND_FOR_EACH_PASS_ARGS(_functor, _data, ...) \
	EXPAND_EVAL(_VM_EXPAND_FOR_EACH_PASS_ARGS(_functor, \
						  _data, ##__VA_ARGS__))

#define _VM_EXPAND_FOR_EACH_PASS_ARGS_INDIRECT() _VM_EXPAND_FOR_EACH_PASS_ARGS

/*
 * EXPAND_FOR_EACH_PASS_ARGS_R(_functor, _data, args...) -
 *   expand a call to functor for each argument, pass all arguments to each
 *   call, recursive calls allowed
 * @_functor:	name of the functor to expand a call to
 * @_data:	private data to apply to the functor
 * @args...:	for each of these arguments a call to functor will be expanded
 *
 * Does the same as EXPAND_FOR_EACH_PASS_ARGS(_functor, _data, args...), but
 * _functor is allowed to call EXPAND_FOR_EACH_PASS_ARGS_R again.
 *
 * ATTENTION: in order for this to work, the outermost call to this macro must
 * be wrapped inside the EXPAND_EVAL macro.
 *
 * Example:
 *
 * ::
 *
 *   #define EXP_4(xy, z) z ## xy
 *   #define EXP_3(y, x, ...) EXPAND_FOR_EACH(EXP_4, x ## y, ##__VA_ARGS__)
 *   #define EXP_2(x, ...) EXPAND_FOR_EACH_PASS_ARGS_R(EXP_3, x, ##__VA_ARGS__)
 *   #define EXP_1(x, dummy, ...) EXP_2(x, __VA_ARGS__)
 *   #define EXP(...) \
 *     EXPAND_EVAL(EXPAND_FOR_EACH_PASS_ARGS_R(EXP_1, dummy, ##__VA_ARGS__))
 *
 *   EXP(a, b, c)
 *   // expands to: aaa aab aac aba abb abc aca acb acc
 *   //             baa bab bac bba bbb bbc bca bcb bcc
 *   //             caa cab cac cba cbb cbc cca ccb ccc
 */
#define EXPAND_FOR_EACH_PASS_ARGS_R \
	_VM_DEFER(_VM_EXPAND_FOR_EACH_PASS_ARGS_INDIRECT)()

#endif /* __VARIADIC_MACRO_H__ */
