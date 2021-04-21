/* SPDX-License-Identifier: GPL-2.0 */
#ifndef W_TYPE_SIZE
#define W_TYPE_SIZE 32
#endif

#define __BITS4 (W_TYPE_SIZE / 4)
#define __ll_B ((UWtype)1 << (W_TYPE_SIZE / 2))
#define __ll_lowpart(t) ((UWtype)(t) & (__ll_B - 1))
#define __ll_highpart(t) ((UWtype)(t) >> (W_TYPE_SIZE / 2))

#if !defined(add_ssaaaa)
#define add_ssaaaa(sh, sl, ah, al, bh, bl)                                     \
	do {                                                                   \
		UWtype __x;                                                    \
		__x = (al) + (bl);                                             \
		(sh) = (ah) + (bh) + (__x < (al));                             \
		(sl) = __x;                                                    \
	} while (0)
#endif

#if !defined(sub_ddmmss)
#define sub_ddmmss(sh, sl, ah, al, bh, bl)                                     \
	do {                                                                   \
		UWtype __x;                                                    \
		__x = (al) - (bl);                                             \
		(sh) = (ah) - (bh) - (__x > (al));                             \
		(sl) = __x;                                                    \
	} while (0)
#endif

#if !defined(umul_ppmm) && defined(smul_ppmm)
#define umul_ppmm(w1, w0, u, v)                                                \
	do {                                                                   \
		UWtype __w1;                                                   \
		UWtype __xm0 = (u), __xm1 = (v);                               \
		smul_ppmm(__w1, w0, __xm0, __xm1);                             \
		(w1) = __w1 + (-(__xm0 >> (W_TYPE_SIZE - 1)) & __xm1) +        \
		       (-(__xm1 >> (W_TYPE_SIZE - 1)) & __xm0);                \
	} while (0)
#endif

/* If we still don't have umul_ppmm, define it using plain C.  */
#if !defined(umul_ppmm)
#define umul_ppmm(w1, w0, u, v)                                                \
	do {                                                                   \
		UWtype __x0, __x1, __x2, __x3;                                 \
		UHWtype __ul, __vl, __uh, __vh;                                \
		__ul = __ll_lowpart(u);                                        \
		__uh = __ll_highpart(u);                                       \
		__vl = __ll_lowpart(v);                                        \
		__vh = __ll_highpart(v);                                       \
		__x0 = (UWtype)__ul * __vl;                                    \
		__x1 = (UWtype)__ul * __vh;                                    \
		__x2 = (UWtype)__uh * __vl;                                    \
		__x3 = (UWtype)__uh * __vh;                                    \
		__x1 += __ll_highpart(__x0); /* this can't give carry */       \
		__x1 += __x2; /* but this indeed can */                        \
		if (__x1 < __x2) /* did we get it? */                          \
			__x3 += __ll_B; /* yes, add it in the proper pos.  */  \
		(w1) = __x3 + __ll_highpart(__x1);                             \
		(w0) = __ll_lowpart(__x1) * __ll_B + __ll_lowpart(__x0);       \
	} while (0)
#endif

#define udiv_qrnnd(q, r, n1, n0, d)                                                 \
	do {                                                                        \
		UWtype __d1, __d0, __q1, __q0;                                      \
		UWtype __r1, __r0, __m;                                             \
		__d1 = __ll_highpart(d);                                            \
		__d0 = __ll_lowpart(d);                                             \
		__r1 = (n1) % __d1;                                                 \
		__q1 = (n1) / __d1;                                                 \
		__m = (UWtype)__q1 * __d0;                                          \
		__r1 = __r1 * __ll_B | __ll_highpart(n0);                           \
		if (__r1 < __m) {                                                   \
			__q1--, __r1 += (d);                                        \
			if (__r1 >=                                                 \
			    (d)) /* i.e. we didn't get carry when adding to __r1 */ \
				if (__r1 < __m)                                     \
					__q1--, __r1 += (d);                        \
		}                                                                   \
		__r1 -= __m;                                                        \
		__r0 = __r1 % __d1;                                                 \
		__q0 = __r1 / __d1;                                                 \
		__m = (UWtype)__q0 * __d0;                                          \
		__r0 = __r0 * __ll_B | __ll_lowpart(n0);                            \
		if (__r0 < __m) {                                                   \
			__q0--, __r0 += (d);                                        \
			if (__r0 >= (d))                                            \
				if (__r0 < __m)                                     \
					__q0--, __r0 += (d);                        \
		}                                                                   \
		__r0 -= __m;                                                        \
		(q) = (UWtype)__q1 * __ll_B | __q0;                                 \
		(r) = __r0;                                                         \
	} while (0)

#define abort() return 0
