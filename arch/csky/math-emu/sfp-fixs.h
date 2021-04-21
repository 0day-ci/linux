/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SFP_FIXS_H__
#define __SFP_FIXS_H__

#ifdef _FP_FRAC_CLZ_4
#undef _FP_FRAC_CLZ_4
#define _FP_FRAC_CLZ_4(R, X)                                                   \
	do {                                                                   \
		if (X##_f[3]) {                                                \
			__FP_CLZ(R, X##_f[3]);                                 \
		} else if (X##_f[2]) {                                         \
			__FP_CLZ(R, X##_f[2]);                                 \
			R += _FP_W_TYPE_SIZE;                                  \
		} else if (X##_f[1]) {                                         \
			__FP_CLZ(R, X##_f[1]);                                 \
			R += _FP_W_TYPE_SIZE * 2;                              \
		} else {                                                       \
			__FP_CLZ(R, X##_f[0]);                                 \
			R += _FP_W_TYPE_SIZE * 3;                              \
		}                                                              \
	} while (0)
#endif

#ifdef _FP_TO_INT_ROUND
#undef _FP_TO_INT_ROUND
#define _FP_TO_INT_ROUND(fs, wc, r, X, rsize, rsigned)                              \
	do {                                                                        \
		r = 0;                                                              \
		switch (X##_c) {                                                    \
		case FP_CLS_NORMAL:                                                 \
			if (X##_e >= _FP_FRACBITS_##fs - 1) {                       \
				if (X##_e < rsize - 1 + _FP_WFRACBITS_##fs) {       \
					if (X##_e >= _FP_WFRACBITS_##fs - 1) {      \
						_FP_FRAC_ASSEMBLE_##wc(r, X,        \
								       rsize);      \
						r <<= X##_e -                       \
						      _FP_WFRACBITS_##fs + 1;       \
					} else {                                    \
						_FP_FRAC_SRL_##wc(                  \
							X,                          \
							_FP_WORKBITS - X##_e +      \
								_FP_FRACBITS_##fs - \
								1);                 \
						_FP_FRAC_ASSEMBLE_##wc(r, X,        \
								       rsize);      \
					}                                           \
				}                                                   \
			} else {                                                    \
				int _lz0, _lz1;                                     \
				if (X##_e <= -_FP_WORKBITS - 1)                     \
					_FP_FRAC_SET_##wc(X,                        \
							  _FP_MINFRAC_##wc);        \
				else                                                \
					_FP_FRAC_SRS_##wc(X,                        \
							  _FP_FRACBITS_##fs -       \
								  1 - X##_e,        \
							  _FP_WFRACBITS_##fs);      \
				_FP_FRAC_CLZ_##wc(_lz0, X);                         \
				_FP_ROUND(wc, X);                                   \
				_FP_FRAC_CLZ_##wc(_lz1, X);                         \
				if (_lz1 < _lz0)                                    \
					X##_e++; /* For overflow detection.  */     \
				_FP_FRAC_SRL_##wc(X, _FP_WORKBITS);                 \
				_FP_FRAC_ASSEMBLE_##wc(r, X, rsize);                \
			}                                                           \
			if (rsigned && X##_s)                                       \
				r = -r;                                             \
			if ((rsigned > 0 || X##_s) && (X##_e == rsize - 1) &&       \
			    (r == (1 << (rsize - 1))))                              \
				break;                                              \
			if (X##_e >= rsize - (rsigned > 0 || X##_s) ||              \
			    (!rsigned && X##_s)) { /* overflow */                   \
			case FP_CLS_NAN:                                            \
			case FP_CLS_INF:                                            \
				if (!rsigned) {                                     \
					r = 0;                                      \
					if (!X##_s)                                 \
						r = ~r;                             \
				} else if (rsigned != 2) {                          \
					r = 1;                                      \
					r <<= rsize - 1;                            \
					r -= 1 - X##_s;                             \
				}                                                   \
				FP_SET_EXCEPTION(FP_EX_INVALID);                    \
			}                                                           \
			break;                                                      \
		case FP_CLS_ZERO:                                                   \
			break;                                                      \
		}                                                                   \
	} while (0)
#endif

#ifdef _FP_PACK_CANONICAL
#undef _FP_PACK_CANONICAL
#define _FP_PACK_CANONICAL(fs, wc, X)                                               \
	do {                                                                        \
		switch (X##_c) {                                                    \
		case FP_CLS_NORMAL:                                                 \
			X##_e += _FP_EXPBIAS_##fs;                                  \
			if (X##_e > 0) {                                            \
				_FP_ROUND(wc, X);                                   \
				if (_FP_FRAC_OVERP_##wc(fs, X)) {                   \
					_FP_FRAC_CLEAR_OVERP_##wc(fs, X);           \
					X##_e++;                                    \
				}                                                   \
				_FP_FRAC_SRL_##wc(X, _FP_WORKBITS);                 \
				if (X##_e >= _FP_EXPMAX_##fs) {                     \
					/* overflow */                              \
					switch (FP_ROUNDMODE) {                     \
					case FP_RND_NEAREST:                        \
						X##_c = FP_CLS_INF;                 \
						break;                              \
					case FP_RND_PINF:                           \
						if (!X##_s)                         \
							X##_c = FP_CLS_INF;         \
						break;                              \
					case FP_RND_MINF:                           \
						if (X##_s)                          \
							X##_c = FP_CLS_INF;         \
						break;                              \
					}                                           \
					if (X##_c == FP_CLS_INF) {                  \
						/* Overflow to infinity */          \
						X##_e = _FP_EXPMAX_##fs;            \
						_FP_FRAC_SET_##wc(                  \
							X, _FP_ZEROFRAC_##wc);      \
					} else {                                    \
						/* Overflow to maximum normal */    \
						X##_e = _FP_EXPMAX_##fs - 1;        \
						_FP_FRAC_SET_##wc(                  \
							X, _FP_MAXFRAC_##wc);       \
					}                                           \
					FP_SET_EXCEPTION(FP_EX_OVERFLOW);           \
					FP_SET_EXCEPTION(FP_EX_INEXACT);            \
				}                                                   \
			} else {                                                    \
				/* we've got a denormalized number */               \
				int max_inc =                                       \
					(FP_ROUNDMODE == FP_RND_NEAREST) ? 3 :      \
									   7;       \
				max_inc += (_FP_FRAC_LOW_##fs(X) & 0xf);            \
				bool is_tiny =                                      \
					(X##_e < 0) ||                              \
					!(max_inc & (_FP_WORK_LSB << 1));           \
				X##_e = -X##_e + 1;                                 \
				if (X##_e <= _FP_WFRACBITS_##fs) {                  \
					_FP_FRAC_SRS_##wc(X, X##_e,                 \
							  _FP_WFRACBITS_##fs);      \
					if (_FP_FRAC_HIGH_##fs(X) &                 \
					    (_FP_OVERFLOW_##fs >> 1)) {             \
						X##_e = 1;                          \
						_FP_FRAC_SET_##wc(                  \
							X, _FP_ZEROFRAC_##wc);      \
					} else {                                    \
						_FP_ROUND(wc, X);                   \
						if (_FP_FRAC_HIGH_##fs(X) &         \
						    (_FP_OVERFLOW_##fs >>           \
						     1)) {                          \
							X##_e = 1;                  \
							_FP_FRAC_SET_##wc(          \
								X,                  \
								_FP_ZEROFRAC_##wc); \
							FP_SET_EXCEPTION(           \
								FP_EX_INEXACT);     \
						} else {                            \
							X##_e = 0;                  \
							_FP_FRAC_SRL_##wc(          \
								X,                  \
								_FP_WORKBITS);      \
						}                                   \
					}                                           \
					if ((is_tiny || (X##_e == 0)) &&            \
					    ((FP_CUR_EXCEPTIONS &                   \
					      FP_EX_INEXACT) ||                     \
					     (FP_TRAPPING_EXCEPTIONS &              \
					      FP_EX_UNDERFLOW)))                    \
						FP_SET_EXCEPTION(                   \
							FP_EX_UNDERFLOW);           \
				} else {                                            \
					/* underflow to zero */                     \
					X##_e = 0;                                  \
					if (!_FP_FRAC_ZEROP_##wc(X)) {              \
						_FP_FRAC_SET_##wc(                  \
							X, _FP_MINFRAC_##wc);       \
						_FP_ROUND(wc, X);                   \
						_FP_FRAC_LOW_##wc(X) >>=            \
							(_FP_WORKBITS);             \
					}                                           \
					FP_SET_EXCEPTION(FP_EX_UNDERFLOW);          \
				}                                                   \
			}                                                           \
			break;                                                      \
		case FP_CLS_ZERO:                                                   \
			X##_e = 0;                                                  \
			_FP_FRAC_SET_##wc(X, _FP_ZEROFRAC_##wc);                    \
			break;                                                      \
		case FP_CLS_INF:                                                    \
			X##_e = _FP_EXPMAX_##fs;                                    \
			_FP_FRAC_SET_##wc(X, _FP_ZEROFRAC_##wc);                    \
			break;                                                      \
		case FP_CLS_NAN:                                                    \
			X##_e = _FP_EXPMAX_##fs;                                    \
			if (!_FP_KEEPNANFRACP) {                                    \
				_FP_FRAC_SET_##wc(X, _FP_NANFRAC_##fs);             \
				X##_s = _FP_NANSIGN_##fs;                           \
			} else                                                      \
				_FP_FRAC_HIGH_RAW_##fs(X) |= _FP_QNANBIT_##fs;      \
			break;                                                      \
		}                                                                   \
	} while (0)
#endif

#endif
