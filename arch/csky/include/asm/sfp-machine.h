/* SPDX-License-Identifier: GPL-2.0 */

#define _FP_W_TYPE_SIZE 32
#define _FP_W_TYPE unsigned long
#define _FP_WS_TYPE signed long
#define _FP_I_TYPE long

#define _FP_MUL_MEAT_S(R, X, Y)                                                \
	_FP_MUL_MEAT_1_wide(_FP_WFRACBITS_S, R, X, Y, umul_ppmm)
#define _FP_MUL_MEAT_D(R, X, Y)                                                \
	_FP_MUL_MEAT_2_wide(_FP_WFRACBITS_D, R, X, Y, umul_ppmm)
#define _FP_MUL_MEAT_Q(R, X, Y)                                                \
	_FP_MUL_MEAT_4_wide(_FP_WFRACBITS_Q, R, X, Y, umul_ppmm)

#define _FP_MUL_MEAT_DW_S(R, X, Y)                                             \
	_FP_MUL_MEAT_DW_1_wide(_FP_WFRACBITS_S, R, X, Y, umul_ppmm)
#define _FP_MUL_MEAT_DW_D(R, X, Y)                                             \
	_FP_MUL_MEAT_DW_2_wide(_FP_WFRACBITS_D, R, X, Y, umul_ppmm)
#define _FP_MUL_MEAT_DW_Q(R, X, Y)                                             \
	_FP_MUL_MEAT_DW_4_wide(_FP_WFRACBITS_Q, R, X, Y, umul_ppmm)

#define _FP_DIV_MEAT_S(R, X, Y) _FP_DIV_MEAT_1_udiv_norm(S, R, X, Y)
#define _FP_DIV_MEAT_D(R, X, Y) _FP_DIV_MEAT_2_udiv(D, R, X, Y)
#define _FP_DIV_MEAT_Q(R, X, Y) _FP_DIV_MEAT_4_udiv(Q, R, X, Y)

#define _FP_NANFRAC_S _FP_QNANBIT_S
#define _FP_NANFRAC_D _FP_QNANBIT_D, 0
#define _FP_NANFRAC_Q _FP_QNANBIT_Q, 0, 0, 0
#define _FP_NANSIGN_S (0)
#define _FP_NANSIGN_D (0)
#define _FP_NANSIGN_Q (0)

#define _FP_KEEPNANFRACP 1
#define _FP_QNANNEGATEDP 0

#define _FP_CHOOSENAN(fs, wc, R, X, Y, OP)                                     \
	do {                                                                   \
		if ((_FP_FRAC_HIGH_RAW_##fs(X) & _FP_QNANBIT_##fs) &&          \
		    !(_FP_FRAC_HIGH_RAW_##fs(Y) & _FP_QNANBIT_##fs)) {         \
			R##_s = Y##_s;                                         \
			_FP_FRAC_COPY_##wc(R, Y);                              \
		} else {                                                       \
			R##_s = X##_s;                                         \
			_FP_FRAC_COPY_##wc(R, X);                              \
		}                                                              \
		R##_c = FP_CLS_NAN;                                            \
	} while (0)

#define __FPU_FPCSR (current->thread.user_fp.fcr)
#define __FPU_FPCSR_RM (get_round_mode())
#define _FP_TININESS_AFTER_ROUNDING (0)

/* Obtain the current rounding mode. */
#define FP_ROUNDMODE ({ (__FPU_FPCSR_RM & 0x3000000) >> 24; })

#define FP_RND_NEAREST 0
#define FP_RND_ZERO 1
#define FP_RND_PINF 2
#define FP_RND_MINF 3

#define FP_EX_INVALID (1 << 0)
#define FP_EX_DIVZERO (1 << 1)
#define FP_EX_OVERFLOW (1 << 2)
#define FP_EX_UNDERFLOW (1 << 3)
#define FP_EX_INEXACT (1 << 4)
#define FP_EX_DENORM (1 << 5)

#define SF_CEQ 2
#define SF_CLT 1
#define SF_CGT 3
#define SF_CUN 4

#include <asm/byteorder.h>

#ifdef __BIG_ENDIAN__
#define __BYTE_ORDER __BIG_ENDIAN
#define __LITTLE_ENDIAN 0
#else
#define __BYTE_ORDER __LITTLE_ENDIAN
#define __BIG_ENDIAN 0
#endif

#if _FP_W_TYPE_SIZE < 64
#define _FP_FRACTBITS_DW_D (4 * _FP_W_TYPE_SIZE)
#define _FP_FRACTBITS_DW_S (2 * _FP_W_TYPE_SIZE)
#define _FP_FRAC_LOW_D(X) _FP_FRAC_LOW_2(X)
#define _FP_FRAC_HIGH_DW_D(X) _FP_FRAC_HIGH_4(X)
#define _FP_FRAC_HIGH_DW_S(X) _FP_FRAC_HIGH_2(X)
#else
#define _FP_FRACTBITS_DW_D (2 * _FP_W_TYPE_SIZE)
#define _FP_FRACTBITS_DW_S _FP_W_TYPE_SIZE
#define _FP_FRAC_LOW_D(X) _FP_FRAC_LOW_1(X)
#define _FP_FRAC_HIGH_DW_D(X) _FP_FRAC_HIGH_2(X)
#define _FP_FRAC_HIGH_DW_S(X) _FP_FRAC_HIGH_1(X)
#endif

#define _FP_FRAC_LOW_S(X) _FP_FRAC_LOW_1(X)

#define _FP_FRAC_HIGHBIT_DW_1(fs, X) (X##_f & _FP_HIGHBIT_DW_##fs)
#define _FP_FRAC_HIGHBIT_DW_2(fs, X)                                           \
	(_FP_FRAC_HIGH_DW_##fs(X) & _FP_HIGHBIT_DW_##fs)
#define _FP_FRAC_HIGHBIT_DW_4(fs, X)                                           \
	(_FP_FRAC_HIGH_DW_##fs(X) & _FP_HIGHBIT_DW_##fs)

#define _FP_QNANBIT_SH_D                                                       \
	((_FP_W_TYPE)1 << (_FP_FRACBITS_D - 2 + _FP_WORKBITS) % _FP_W_TYPE_SIZE)

#define _FP_IMPLBIT_SH_D                                                       \
	((_FP_W_TYPE)1 << (_FP_FRACBITS_D - 1 + _FP_WORKBITS) % _FP_W_TYPE_SIZE)

#define _FP_WFRACBITS_DW_D (2 * _FP_WFRACBITS_D)
#define _FP_WFRACXBITS_DW_D (_FP_FRACTBITS_DW_D - _FP_WFRACBITS_DW_D)
#define _FP_HIGHBIT_DW_D                                                       \
	((_FP_W_TYPE)1 << (_FP_WFRACBITS_DW_D - 1) % _FP_W_TYPE_SIZE)

#define _FP_WFRACBITS_DW_S (2 * _FP_WFRACBITS_S)
#define _FP_WFRACXBITS_DW_S (_FP_FRACTBITS_DW_S - _FP_WFRACBITS_DW_S)
#define _FP_HIGHBIT_DW_S                                                       \
	((_FP_W_TYPE)1 << (_FP_WFRACBITS_DW_S - 1) % _FP_W_TYPE_SIZE)

#define _FP_MUL_MEAT_DW_1_wide(wfracbits, R, X, Y, doit)                       \
	do {                                                                   \
		doit(R##_f1, R##_f0, X##_f, Y##_f);                            \
	} while (0)

#define _FP_MUL_MEAT_DW_2_wide(wfracbits, R, X, Y, doit)                       \
	do {                                                                   \
		_FP_FRAC_DECL_2(_FP_MUL_MEAT_DW_2_wide_b);                     \
		_FP_FRAC_DECL_2(_FP_MUL_MEAT_DW_2_wide_c);                     \
		doit(_FP_FRAC_WORD_4(R, 1), _FP_FRAC_WORD_4(R, 0), X##_f0,     \
		     Y##_f0);                                                  \
		doit(_FP_MUL_MEAT_DW_2_wide_b_f1, _FP_MUL_MEAT_DW_2_wide_b_f0, \
		     X##_f0, Y##_f1);                                          \
		doit(_FP_MUL_MEAT_DW_2_wide_c_f1, _FP_MUL_MEAT_DW_2_wide_c_f0, \
		     X##_f1, Y##_f0);                                          \
		doit(_FP_FRAC_WORD_4(R, 3), _FP_FRAC_WORD_4(R, 2), X##_f1,     \
		     Y##_f1);                                                  \
		__FP_FRAC_ADD_3(_FP_FRAC_WORD_4(R, 3), _FP_FRAC_WORD_4(R, 2),  \
				_FP_FRAC_WORD_4(R, 1), 0,                      \
				_FP_MUL_MEAT_DW_2_wide_b_f1,                   \
				_FP_MUL_MEAT_DW_2_wide_b_f0,                   \
				_FP_FRAC_WORD_4(R, 3), _FP_FRAC_WORD_4(R, 2),  \
				_FP_FRAC_WORD_4(R, 1));                        \
		__FP_FRAC_ADD_3(_FP_FRAC_WORD_4(R, 3), _FP_FRAC_WORD_4(R, 2),  \
				_FP_FRAC_WORD_4(R, 1), 0,                      \
				_FP_MUL_MEAT_DW_2_wide_c_f1,                   \
				_FP_MUL_MEAT_DW_2_wide_c_f0,                   \
				_FP_FRAC_WORD_4(R, 3), _FP_FRAC_WORD_4(R, 2),  \
				_FP_FRAC_WORD_4(R, 1));                        \
	} while (0)

#define _FP_FRAC_COPY_1_2(D, S) (D##_f = S##_f0)
#define _FP_FRAC_COPY_2_1(D, S) ((D##_f0 = S##_f), (D##_f1 = 0))
#define _FP_FRAC_COPY_2_2(D, S) _FP_FRAC_COPY_2(D, S)
#define _FP_FRAC_COPY_1_4(D, S) (D##_f = S##_f[0])
#define _FP_FRAC_COPY_2_4(D, S)                                                \
	do {                                                                   \
		D##_f0 = S##_f[0];                                             \
		D##_f1 = S##_f[1];                                             \
	} while (0)
#define _FP_FRAC_COPY_4_1(D, S)                                                \
	do {                                                                   \
		D##_f[0] = S##_f;                                              \
		D##_f[1] = D##_f[2] = D##_f[3] = 0;                            \
	} while (0)
#define _FP_FRAC_COPY_4_2(D, S)                                                \
	do {                                                                   \
		D##_f[0] = S##_f0;                                             \
		D##_f[1] = S##_f1;                                             \
		D##_f[2] = D##_f[3] = 0;                                       \
	} while (0)
#define _FP_FRAC_COPY_4_4(D, S) _FP_FRAC_COPY_4(D, S)

/* fma (Inf, 0, c).  */
#ifndef FP_EX_INVALID_IMZ_FMA
#define FP_EX_INVALID_IMZ_FMA 0
#endif

#define __FP_FRAC_SUB_3(r2, r1, r0, x2, x1, x0, y2, y1, y0)                    \
	do {                                                                   \
		int _c1, _c2;                                                  \
		r0 = x0 - y0;                                                  \
		_c1 = r0 > x0;                                                 \
		r1 = x1 - y1;                                                  \
		_c2 = r1 > x1;                                                 \
		r1 -= _c1;                                                     \
		_c2 |= _c1 && (y1 == x1);                                      \
		r2 = x2 - y2 - _c2;                                            \
	} while (0)

#define __FP_FRAC_SUB_4(r3, r2, r1, r0, x3, x2, x1, x0, y3, y2, y1, y0)        \
	do {                                                                   \
		int _c1, _c2, _c3;                                             \
		r0 = x0 - y0;                                                  \
		_c1 = r0 > x0;                                                 \
		r1 = x1 - y1;                                                  \
		_c2 = r1 > x1;                                                 \
		r1 -= _c1;                                                     \
		_c2 |= _c1 && (y1 == x1);                                      \
		r2 = x2 - y2;                                                  \
		_c3 = r2 > x2;                                                 \
		r2 -= _c2;                                                     \
		_c3 |= _c2 && (y2 == x2);                                      \
		r3 = x3 - y3 - _c3;                                            \
	} while (0)

#define _FP_FMA(fs, wc, dwc, R, X, Y, Z)                                                \
	do {                                                                            \
		__label__ done_fma;                                                     \
		FP_DECL_##fs(_FP_FMA_T);                                                \
		_FP_FMA_T##_s = X##_s ^ Y##_s;                                          \
		_FP_FMA_T##_e = X##_e + Y##_e + 1;                                      \
		switch (_FP_CLS_COMBINE(X##_c, Y##_c)) {                                \
		case _FP_CLS_COMBINE(FP_CLS_NORMAL, FP_CLS_NORMAL):                     \
			switch (Z##_c) {                                                \
			case FP_CLS_INF:                                                \
			case FP_CLS_NAN:                                                \
				R##_s = Z##_s;                                          \
				_FP_FRAC_COPY_##wc(R, Z);                               \
				R##_c = Z##_c;                                          \
				break;                                                  \
			case FP_CLS_ZERO:                                               \
				R##_c = FP_CLS_NORMAL;                                  \
				R##_s = _FP_FMA_T##_s;                                  \
				R##_e = _FP_FMA_T##_e;                                  \
				_FP_MUL_MEAT_##fs(R, X, Y);                             \
				if (_FP_FRAC_OVERP_##wc(fs, R))                         \
					_FP_FRAC_SRS_##wc(R, 1,                         \
							  _FP_WFRACBITS_##fs);          \
				else                                                    \
					R##_e--;                                        \
				break;                                                  \
			case FP_CLS_NORMAL:;                                            \
				_FP_FRAC_DECL_##dwc(_FP_FMA_TD);                        \
				_FP_FRAC_DECL_##dwc(_FP_FMA_ZD);                        \
				_FP_FRAC_DECL_##dwc(_FP_FMA_RD);                        \
				_FP_MUL_MEAT_DW_##fs(_FP_FMA_TD, X, Y);                 \
				R##_e = _FP_FMA_T##_e;                                  \
				int _FP_FMA_tsh =                                       \
					_FP_FRAC_HIGHBIT_DW_##dwc(                      \
						fs, _FP_FMA_TD) == 0;                   \
				_FP_FMA_T##_e -= _FP_FMA_tsh;                           \
				int _FP_FMA_ediff = _FP_FMA_T##_e - Z##_e;              \
				if (_FP_FMA_ediff >= 0) {                               \
					int _FP_FMA_shift =                             \
						_FP_WFRACBITS_##fs -                    \
						_FP_FMA_tsh - _FP_FMA_ediff;            \
					if (_FP_FMA_shift <=                            \
					    -_FP_WFRACBITS_##fs)                        \
						_FP_FRAC_SET_##dwc(                     \
							_FP_FMA_ZD,                     \
							_FP_MINFRAC_##dwc);             \
					else {                                          \
						_FP_FRAC_COPY_##dwc##_##wc(             \
							_FP_FMA_ZD, Z);                 \
						if (_FP_FMA_shift < 0)                  \
							_FP_FRAC_SRS_##dwc(             \
								_FP_FMA_ZD,             \
								-_FP_FMA_shift,         \
								_FP_WFRACBITS_DW_##fs); \
						else if (_FP_FMA_shift > 0)             \
							_FP_FRAC_SLL_##dwc(             \
								_FP_FMA_ZD,             \
								_FP_FMA_shift);         \
					}                                               \
					R##_s = _FP_FMA_T##_s;                          \
					if (_FP_FMA_T##_s == Z##_s)                     \
						_FP_FRAC_ADD_##dwc(                     \
							_FP_FMA_RD,                     \
							_FP_FMA_TD,                     \
							_FP_FMA_ZD);                    \
					else {                                          \
						_FP_FRAC_SUB_##dwc(                     \
							_FP_FMA_RD,                     \
							_FP_FMA_TD,                     \
							_FP_FMA_ZD);                    \
						if (_FP_FRAC_NEGP_##dwc(                \
							    _FP_FMA_RD)) {              \
							R##_s = Z##_s;                  \
							_FP_FRAC_SUB_##dwc(             \
								_FP_FMA_RD,             \
								_FP_FMA_ZD,             \
								_FP_FMA_TD);            \
						}                                       \
					}                                               \
				} else {                                                \
					R##_e = Z##_e;                                  \
					R##_s = Z##_s;                                  \
					_FP_FRAC_COPY_##dwc##_##wc(_FP_FMA_ZD,          \
								   Z);                  \
					_FP_FRAC_SLL_##dwc(                             \
						_FP_FMA_ZD,                             \
						_FP_WFRACBITS_##fs);                    \
					int _FP_FMA_shift =                             \
						-_FP_FMA_ediff - _FP_FMA_tsh;           \
					if (_FP_FMA_shift >=                            \
					    _FP_WFRACBITS_DW_##fs)                      \
						_FP_FRAC_SET_##dwc(                     \
							_FP_FMA_TD,                     \
							_FP_MINFRAC_##dwc);             \
					else if (_FP_FMA_shift > 0)                     \
						_FP_FRAC_SRS_##dwc(                     \
							_FP_FMA_TD,                     \
							_FP_FMA_shift,                  \
							_FP_WFRACBITS_DW_##fs);         \
					if (Z##_s == _FP_FMA_T##_s)                     \
						_FP_FRAC_ADD_##dwc(                     \
							_FP_FMA_RD,                     \
							_FP_FMA_ZD,                     \
							_FP_FMA_TD);                    \
					else                                            \
						_FP_FRAC_SUB_##dwc(                     \
							_FP_FMA_RD,                     \
							_FP_FMA_ZD,                     \
							_FP_FMA_TD);                    \
				}                                                       \
				if (_FP_FRAC_ZEROP_##dwc(_FP_FMA_RD)) {                 \
					if (_FP_FMA_T##_s == Z##_s)                     \
						R##_s = Z##_s;                          \
					else                                            \
						R##_s = (FP_ROUNDMODE ==                \
							 FP_RND_MINF);                  \
					_FP_FRAC_SET_##wc(R,                            \
							  _FP_ZEROFRAC_##wc);           \
					R##_c = FP_CLS_ZERO;                            \
				} else {                                                \
					int _FP_FMA_rlz;                                \
					_FP_FRAC_CLZ_##dwc(_FP_FMA_rlz,                 \
							   _FP_FMA_RD);                 \
					_FP_FMA_rlz -= _FP_WFRACXBITS_DW_##fs;          \
					R##_e -= _FP_FMA_rlz;                           \
					int _FP_FMA_shift =                             \
						_FP_WFRACBITS_##fs -                    \
						_FP_FMA_rlz;                            \
					if (_FP_FMA_shift > 0)                          \
						_FP_FRAC_SRS_##dwc(                     \
							_FP_FMA_RD,                     \
							_FP_FMA_shift,                  \
							_FP_WFRACBITS_DW_##fs);         \
					else if (_FP_FMA_shift < 0)                     \
						_FP_FRAC_SLL_##dwc(                     \
							_FP_FMA_RD,                     \
							-_FP_FMA_shift);                \
					_FP_FRAC_COPY_##wc##_##dwc(                     \
						R, _FP_FMA_RD);                         \
					R##_c = FP_CLS_NORMAL;                          \
				}                                                       \
				break;                                                  \
			}                                                               \
			goto done_fma;                                                  \
		case _FP_CLS_COMBINE(FP_CLS_NAN, FP_CLS_NAN):                           \
			_FP_CHOOSENAN(fs, wc, _FP_FMA_T, X, Y, '*');                    \
			break;                                                          \
		case _FP_CLS_COMBINE(FP_CLS_NAN, FP_CLS_NORMAL):                        \
		case _FP_CLS_COMBINE(FP_CLS_NAN, FP_CLS_INF):                           \
		case _FP_CLS_COMBINE(FP_CLS_NAN, FP_CLS_ZERO):                          \
			_FP_FMA_T##_s = X##_s;                                          \
		case _FP_CLS_COMBINE(FP_CLS_INF, FP_CLS_INF):                           \
		case _FP_CLS_COMBINE(FP_CLS_INF, FP_CLS_NORMAL):                        \
		case _FP_CLS_COMBINE(FP_CLS_ZERO, FP_CLS_NORMAL):                       \
		case _FP_CLS_COMBINE(FP_CLS_ZERO, FP_CLS_ZERO):                         \
			_FP_FRAC_COPY_##wc(_FP_FMA_T, X);                               \
			_FP_FMA_T##_c = X##_c;                                          \
			break;                                                          \
		case _FP_CLS_COMBINE(FP_CLS_NORMAL, FP_CLS_NAN):                        \
		case _FP_CLS_COMBINE(FP_CLS_INF, FP_CLS_NAN):                           \
		case _FP_CLS_COMBINE(FP_CLS_ZERO, FP_CLS_NAN):                          \
			_FP_FMA_T##_s = Y##_s;                                          \
		case _FP_CLS_COMBINE(FP_CLS_NORMAL, FP_CLS_INF):                        \
		case _FP_CLS_COMBINE(FP_CLS_NORMAL, FP_CLS_ZERO):                       \
			_FP_FRAC_COPY_##wc(_FP_FMA_T, Y);                               \
			_FP_FMA_T##_c = Y##_c;                                          \
			break;                                                          \
		case _FP_CLS_COMBINE(FP_CLS_INF, FP_CLS_ZERO):                          \
		case _FP_CLS_COMBINE(FP_CLS_ZERO, FP_CLS_INF):                          \
			_FP_FMA_T##_s = _FP_NANSIGN_##fs;                               \
			_FP_FMA_T##_c = FP_CLS_NAN;                                     \
			_FP_FRAC_SET_##wc(_FP_FMA_T, _FP_NANFRAC_##fs);                 \
			FP_SET_EXCEPTION(FP_EX_INVALID |                                \
					 FP_EX_INVALID_IMZ_FMA);                        \
			break;                                                          \
		default:                                                                \
			abort();                                                        \
		}                                                                       \
		/* T = X * Y is zero, infinity or NaN.  */                              \
		switch (_FP_CLS_COMBINE(_FP_FMA_T##_c, Z##_c)) {                        \
		case _FP_CLS_COMBINE(FP_CLS_NAN, FP_CLS_NAN):                           \
			_FP_CHOOSENAN(fs, wc, R, _FP_FMA_T, Z, '+');                    \
			break;                                                          \
		case _FP_CLS_COMBINE(FP_CLS_NAN, FP_CLS_NORMAL):                        \
		case _FP_CLS_COMBINE(FP_CLS_NAN, FP_CLS_INF):                           \
		case _FP_CLS_COMBINE(FP_CLS_NAN, FP_CLS_ZERO):                          \
		case _FP_CLS_COMBINE(FP_CLS_INF, FP_CLS_NORMAL):                        \
		case _FP_CLS_COMBINE(FP_CLS_INF, FP_CLS_ZERO):                          \
			R##_s = _FP_FMA_T##_s;                                          \
			_FP_FRAC_COPY_##wc(R, _FP_FMA_T);                               \
			R##_c = _FP_FMA_T##_c;                                          \
			break;                                                          \
		case _FP_CLS_COMBINE(FP_CLS_INF, FP_CLS_NAN):                           \
		case _FP_CLS_COMBINE(FP_CLS_ZERO, FP_CLS_NAN):                          \
		case _FP_CLS_COMBINE(FP_CLS_ZERO, FP_CLS_NORMAL):                       \
		case _FP_CLS_COMBINE(FP_CLS_ZERO, FP_CLS_INF):                          \
			R##_s = Z##_s;                                                  \
			_FP_FRAC_COPY_##wc(R, Z);                                       \
			R##_c = Z##_c;                                                  \
			R##_e = Z##_e;                                                  \
			break;                                                          \
		case _FP_CLS_COMBINE(FP_CLS_INF, FP_CLS_INF):                           \
			if (_FP_FMA_T##_s == Z##_s) {                                   \
				R##_s = Z##_s;                                          \
				_FP_FRAC_COPY_##wc(R, Z);                               \
				R##_c = Z##_c;                                          \
			} else {                                                        \
				R##_s = _FP_NANSIGN_##fs;                               \
				R##_c = FP_CLS_NAN;                                     \
				_FP_FRAC_SET_##wc(R, _FP_NANFRAC_##fs);                 \
				FP_SET_EXCEPTION(FP_EX_INVALID |                        \
						 FP_EX_INVALID_ISI);                    \
			}                                                               \
			break;                                                          \
		case _FP_CLS_COMBINE(FP_CLS_ZERO, FP_CLS_ZERO):                         \
			if (_FP_FMA_T##_s == Z##_s)                                     \
				R##_s = Z##_s;                                          \
			else                                                            \
				R##_s = (FP_ROUNDMODE == FP_RND_MINF);                  \
			_FP_FRAC_COPY_##wc(R, Z);                                       \
			R##_c = Z##_c;                                                  \
			break;                                                          \
		default:                                                                \
			abort();                                                        \
		}                                                                       \
	done_fma:;                                                                      \
	} while (0)

#if _FP_W_TYPE_SIZE < 64
#define FP_FMA_S(R, X, Y, Z) _FP_FMA(S, 1, 2, R, X, Y, Z)
#define FP_FMA_D(R, X, Y, Z) _FP_FMA(D, 2, 4, R, X, Y, Z)
#else
#define FP_FMA_S(R, X, Y, Z) _FP_FMA(S, 1, 1, R, X, Y, Z)
#define FP_FMA_D(R, X, Y, Z) _FP_FMA(D, 1, 2, R, X, Y, Z)
#endif
