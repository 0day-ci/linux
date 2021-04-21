/* SPDX-License-Identifier: GPL-2.0
 *
 * CSKY  860  MATHEMU
 *
 * Copyright (C) 2021 Hangzhou C-SKY Microsystems co.,ltd.
 *
 *    Authors: Li Weiwei <liweiwei@iscas.ac.cn>
 *             Wang Junqiang <wangjunqiang@iscas.ac.cn>
 *
 */
#ifndef __CSKY_FP_MATH_H__
#define __CSKY_FP_MATH_H__

struct inst_data {
	unsigned int inst;
	struct pt_regs *regs;
};

union float64_components {
	unsigned long long f64;
	unsigned int i[2];
	double f;
};

union fd_data {
	unsigned long long n;
	double d;
};

union fs_data {
	unsigned int n;
	float f;
};

#define DPFROMINIT(sp, x)                                                      \
	unsigned long long x##_val;                                            \
	x##_val = 0LL;                                                         \
	sp = &x##_val
#define SPFROMINIT(sp, x)                                                      \
	unsigned int x##_val;                                                  \
	x##_val = 0;                                                           \
	sp = &x##_val
#define DPFROMREG(dp, x)                                                       \
	unsigned long long x##_val;                                            \
	x##_val = get_float64(x);                                              \
	dp = &x##_val
#define SPFROMREG(sp, x)                                                       \
	unsigned int x##_val;                                                  \
	x##_val = get_float32(x);                                              \
	sp = &x##_val

#define SP_CONST_DATA(sp, ind)                                                 \
	unsigned int sp##_val;                                                 \
	sp##_val = get_single_constant(ind);                                   \
	sp = &sp##_val
#define DP_CONST_DATA(dp, ind)                                                 \
	unsigned long long dp##_val;                                           \
	dp##_val = get_double_constant(ind);                                   \
	dp = &dp##_val

#define REG_INIT_SN

#define REG_INIT_SI(reg)                                                       \
		void *vr##reg;                                                 \
		SPFROMINIT(vr##reg, reg)

#define REG_INIT_SR(reg)                                                       \
		void *vr##reg;                                                 \
		SPFROMREG(vr##reg, reg)

#define REG_INIT_DN

#define REG_INIT_DI(reg)                                                       \
		void *vr##reg;                                                 \
		DPFROMINIT(vr##reg, reg)

#define REG_INIT_DR(reg)                                                       \
		void *vr##reg;                                                 \
		DPFROMREG(vr##reg, reg)

#define FPU_INSN_START(t1, t2, t3)                                             \
		REG_INIT_##t1(x);                                              \
		REG_INIT_##t2(y);                                              \
		REG_INIT_##t3(z)

#define FPU_INSN_DP_END                                                        \
	do {                                                                   \
		set_float64(*(unsigned long long *)vrz, z);                    \
		if (FP_CUR_EXCEPTIONS)                                         \
			raise_float_exception(FP_CUR_EXCEPTIONS);              \
	} while (0)

#define FPU_INSN_SP_END                                                        \
	do {                                                                   \
		set_float32(*(unsigned int *)vrz, z);                          \
		if (FP_CUR_EXCEPTIONS)                                         \
			raise_float_exception(FP_CUR_EXCEPTIONS);              \
	} while (0)

#define SET_FLAG_END                                                           \
	do {                                                                   \
		set_fsr_c(result, inst_data->regs);                            \
		if (FP_CUR_EXCEPTIONS)                                         \
			raise_float_exception(FP_CUR_EXCEPTIONS);              \
	} while (0)

#define SET_AND_SAVE_RM(mode)                                                  \
	unsigned int saved_mode;                                               \
	saved_mode = get_round_mode();                                         \
	set_round_mode(mode << 24)
#define RESTORE_ROUND_MODE set_round_mode(saved_mode)

#define MAC_INTERNAL_ROUND_DP                                                  \
	do {                                                                   \
		FP_PACK_DP(vrz, T);                                            \
		FP_UNPACK_DP(T, vrz);                                          \
	} while (0)

#define MAC_INTERNAL_ROUND_SP                                                  \
	do {                                                                   \
		FP_PACK_SP(vrz, T);                                            \
		FP_UNPACK_SP(T, vrz);                                          \
	} while (0)

#define SET_FSR_C(val, reg)                                                    \
	do {                                                                   \
		if (val) {                                                     \
			regs->sr |= 0x1;                                       \
		} else {                                                       \
			regs->sr &= 0xfffffffe;                                \
		}                                                              \
	} while (0)

inline void raise_float_exception(unsigned int exception);

inline char get_fsr_c(struct pt_regs *regs);
inline void set_fsr_c(unsigned int val, struct pt_regs *regs);
inline unsigned long long read_fpr64(int reg_num);
inline unsigned int read_fpr32l(int reg_num);
inline unsigned int read_fpr32h(int reg_num);
inline void write_fpr64(unsigned long long val, int reg_num);
inline void write_fpr32l(unsigned int val, int reg_num);
inline void write_fpr32h(unsigned int val, int reg_num);
inline unsigned int read_gr(int reg_num, struct pt_regs *regs);
inline void write_gr(unsigned int val, int reg_num, struct pt_regs *regs);
inline unsigned int read_fpr(int reg_num);
inline void write_fpr(unsigned int val, int reg_num);
inline unsigned int read_fpsr(void);
inline void write_fpsr(unsigned int val);
inline unsigned int get_fpvalue32(unsigned int addr);
inline void set_fpvalue32(unsigned int val, unsigned int addr);
inline unsigned long long get_fpvalue64(unsigned int addr);
inline void set_fpvalue64(unsigned long long val, unsigned int addr);
inline unsigned int read_fpcr(void);
inline void write_fpcr(unsigned int val);
inline unsigned int read_fpesr(void);
inline void write_fpesr(unsigned int val);
inline unsigned long long get_double_constant(const unsigned int index);
inline unsigned int get_single_constant(const unsigned int index);
inline unsigned int get_round_mode(void);
inline void set_round_mode(unsigned int val);
inline void clear_fesr(unsigned int fesr);
inline unsigned long long get_float64(int reg_num);
inline unsigned int get_float32(int reg_num);
inline void set_float64(unsigned long long val, int reg_num);
inline void set_float32(unsigned int val, int reg_num);
inline void set_float32h(unsigned int val, int reg_num);
inline unsigned int get_uint32(int reg_num, struct inst_data *inst_data);
inline void set_uint32(unsigned int val, int reg_num,
		       struct inst_data *inst_data);
inline unsigned long long get_float64_from_memory(unsigned long addr);
inline void set_float64_to_memory(unsigned long long val, unsigned long addr);
inline unsigned int get_float32_from_memory(unsigned long addr);
inline void set_float32_to_memory(unsigned int val, unsigned long addr);

#endif
