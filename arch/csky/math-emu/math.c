// SPDX-License-Identifier: GPL-2.0
/*
 * CSKY  860  MATHEMU
 *
 * Copyright (C) 2021 Hangzhou C-SKY Microsystems co.,ltd.
 *
 *    Authors: Li Weiwei <liweiwei@iscas.ac.cn>
 *             Wang Junqiang <wangjunqiang@iscas.ac.cn>
 *
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/sched/signal.h>
#include <linux/signal.h>
#include <linux/perf_event.h>
#include <linux/uaccess.h>
#include <asm/processor.h>
#include <asm/io.h>
#include <asm/ptrace.h>
#include <abi/fpu.h>
#include <asm/reg_ops.h>

#include "math.h"

#if defined(CONFIG_CPU_CK810)
#include "fp810.h"
#define FPR_L_IDX(x) (x * 4)
#define FPR_H_IDX(x) ((x * 4) + 1)
#ifdef __CSKYBE__
#define FPR_IDX(x) FPR_H_IDX(x)
#else
#define FPR_IDX(x) FPR_L_IDX(x)
#endif
#elif defined(CONFIG_CPU_CK860)
#include "fp860.h"
#define FPR_L_IDX(x) (x * 2)
#define FPR_H_IDX(x) ((x * 2) + 1)
#ifdef __CSKYBE__
#define FPR_IDX(x) FPR_H_IDX(x)
#else
#define FPR_IDX(x) FPR_L_IDX(x)
#endif
#else
#error cpu not support mathfpu
#endif

#define FP_INST_MASK 0xF4000000
#define FP_INST_OP_MASK 0xFC000000
#define ROUND_MODE_MASK (0x3 << 24)

//TODO:use
#define __FPU_FPCR (current->thread.user_fp.fcr)
#define __FPU_FPESR (current->thread.user_fp.fesr)
#define __FPU_FPCR_U (current->thread.user_fp.user_fcr)
#define __FPU_FPESR_U (current->thread.user_fp.user_fesr)

#define INST_IS_FP(x) (((x & FP_INST_OP_MASK) == FP_INST_MASK) ? 1 : 0)

const unsigned long long float64_constant[] = {
	0x0000000000000000ULL, /* double 0.0 */
	0x3ff0000000000000ULL, /* double 1.0 */
	0x4000000000000000ULL, /* double 2.0 */
	0x4008000000000000ULL, /* double 3.0 */
	0x4010000000000000ULL, /* double 4.0 */
	0x4014000000000000ULL, /* double 5.0 */
	0x3fe0000000000000ULL, /* double 0.5 */
	0x4024000000000000ULL /* double 10.0 */
};

const unsigned int float32_constant[] = {
	0x00000000, /* single 0.0 */
	0x3f800000, /* single 1.0 */
	0x40000000, /* single 2.0 */
	0x40400000, /* single 3.0 */
	0x40800000, /* single 4.0 */
	0x40a00000, /* single 5.0 */
	0x3f000000, /* single 0.5 */
	0x41200000 /* single 10.0 */
};

inline unsigned int read_gr(int reg_num, struct pt_regs *regs)
{
	switch (reg_num) {
	case 0:
		return regs->orig_a0;
	case 1:
		return regs->a1;
	case 2:
		return regs->a2;
	case 3:
		return regs->a3;
	case 4 ... 13:
		return regs->regs[reg_num - 4];
	case 14:
		return regs->usp;
	case 15:
		return regs->lr;
#if defined(__CSKYABIV2__)
	case 16 ... 30:
		return regs->exregs[reg_num - 16];
#endif
	default:
		break;
	}
	return 0;
}

inline void write_gr(unsigned int val, int reg_num, struct pt_regs *regs)
{
	switch (reg_num) {
	case 0:
		regs->a0 = val;
		break;
	case 1:
		regs->a1 = val;
		break;
	case 2:
		regs->a2 = val;
		break;
	case 3:
		regs->a3 = val;
		break;
	case 4 ... 13:
		regs->regs[reg_num - 4] = val;
		break;
	case 14:
		regs->usp = val;
		break;
	case 15:
		regs->lr = val;
		break;
#if defined(__CSKYABIV2__)
	case 16 ... 30:
		regs->exregs[reg_num - 16] = val;
		break;
#endif
	default:
		break;
	}
}

inline unsigned int get_fpvalue32(unsigned int addr)
{
	unsigned int result;

	get_user(result, (unsigned int *)addr);
	return result;
}

inline void set_fpvalue32(unsigned int val, unsigned int addr)
{
	unsigned int result = (unsigned int)val;

	put_user(result, (unsigned int *)addr);
}

inline unsigned long long get_fpvalue64(unsigned int addr)
{
	union float64_components result;

#ifdef __CSKYBE__
	get_user(result.i[1], (unsigned int *)addr);
	get_user(result.i[0], (unsigned int *)(addr + 4));
#else
	get_user(result.i[1], (unsigned int *)(addr + 4));
	get_user(result.i[0], (unsigned int *)addr);
#endif

	return result.f64;
}

inline void set_fpvalue64(unsigned long long val, unsigned int addr)
{
	union float64_components result;

	result.f64 = val;
#ifdef __CSKYBE__
	put_user(result.i[1], (unsigned int *)addr);
	put_user(result.i[0], (unsigned int *)(addr + 4));
#else
	put_user(result.i[1], (unsigned int *)(addr + 4));
	put_user(result.i[0], (unsigned int *)addr);
#endif
}

inline unsigned long long read_fpr64(int reg_num)
{
	union float64_components result;

	int reg_id0 = FPR_IDX(reg_num);
	int reg_id1 = reg_id0 % 2 ? (reg_id0 - 1) : (reg_id0 + 1);

	result.i[0] = current->thread.user_fp.vr[reg_id0];
	result.i[1] = current->thread.user_fp.vr[reg_id1];

	return result.f64;
}

inline void write_fpr64(unsigned long long val, int reg_num)
{
	union float64_components result;
	int reg_id0, reg_id1;

	reg_id0 = FPR_IDX(reg_num);
	reg_id1 = reg_id0 % 2 ? (reg_id0 - 1) : (reg_id0 + 1);
	result.f64 = val;
	current->thread.user_fp.vr[reg_id0] = result.i[0];
	current->thread.user_fp.vr[reg_id1] = result.i[1];
}

inline unsigned int read_fpr32l(int reg_num)
{
	return current->thread.user_fp.vr[FPR_L_IDX(reg_num)];
}

inline unsigned int read_fpr32h(int reg_num)
{
	return current->thread.user_fp.vr[FPR_H_IDX(reg_num)];
}

inline void write_fpr32l(unsigned int val, int reg_num)
{
	current->thread.user_fp.vr[FPR_L_IDX(reg_num)] = (unsigned long)val;
}

inline void write_fpr32h(unsigned int val, int reg_num)
{
	current->thread.user_fp.vr[FPR_H_IDX(reg_num)] = (unsigned long)val;
}

inline char get_fsr_c(struct pt_regs *regs)
{
	char result = regs->sr & 0x1;
	return result;
}

inline void set_fsr_c(unsigned int val, struct pt_regs *regs)
{
	if (val)
		regs->sr |= 0x1;
	else
		regs->sr &= 0xfffffffe;
}

unsigned long long get_double_constant(const unsigned int index)
{
	return float64_constant[index];
}

unsigned int get_single_constant(const unsigned int index)
{
	return float32_constant[index];
}

inline unsigned int read_fpcr(void)
{
	return current->thread.user_fp.fcr;
}

inline void write_fpcr(unsigned int val)
{
	current->thread.user_fp.fcr = val;
}

inline unsigned int read_fpesr(void)
{
	return current->thread.user_fp.fesr;
}

inline void write_fpesr(unsigned int val)
{
	current->thread.user_fp.user_fesr |= val;
	current->thread.user_fp.fesr = current->thread.user_fp.user_fesr;
}

inline unsigned int get_round_mode(void)
{
	unsigned int result = read_fpcr();

	return result & ROUND_MODE_MASK;
}

inline void set_round_mode(unsigned int val)
{
	write_fpcr((read_fpcr() & ~ROUND_MODE_MASK) | (val & ROUND_MODE_MASK));
}

inline void clear_fesr(unsigned int fesr)
{
	write_fpesr(0);
}

inline unsigned long long get_float64(int reg_num)
{
	unsigned long long result;

	result = read_fpr64(reg_num);
	return result;
}

inline unsigned int get_float32(int reg_num)
{
	unsigned int result;

	result = read_fpr32l(reg_num);
	return result;
}

inline void set_float64(unsigned long long val, int reg_num)
{
	write_fpr64(val, reg_num);
}

inline void set_float32(unsigned int val, int reg_num)
{
	write_fpr32l(val, reg_num);
}

inline unsigned int get_float32h(int reg_num)
{
	unsigned int result;

	result = read_fpr32h(reg_num);
	return result;
}

inline void set_float32h(unsigned int val, int reg_num)
{
	write_fpr32h(val, reg_num);
}

inline unsigned int get_uint32(int reg_num, struct inst_data *inst_data)
{
	unsigned int result;

	result = read_gr(reg_num, inst_data->regs);
	return result;
}

inline void set_uint32(unsigned int val, int reg_num,
		       struct inst_data *inst_data)
{
	write_gr(val, reg_num, inst_data->regs);
}

inline unsigned long long get_float64_from_memory(unsigned long addr)
{
	return get_fpvalue64(addr);
}

inline void set_float64_to_memory(unsigned long long val, unsigned long addr)
{
	set_fpvalue64(val, addr);
}

inline unsigned int get_float32_from_memory(unsigned long addr)
{
	return get_fpvalue32(addr);
}

inline void set_float32_to_memory(unsigned int val, unsigned long addr)
{
	set_fpvalue32(val, addr);
}

static unsigned int fpe_exception_pc;
inline void raise_float_exception(unsigned int exception)
{
	int sig;
	kernel_siginfo_t info;

	unsigned int enable_ex = exception & current->thread.user_fp.user_fcr &
				 FPE_REGULAR_EXCEPTION;

	if (!enable_ex) {
		if (exception)
			write_fpesr(0x8000 | (exception << 8));
		return;
	}

	if (!(exception & FPE_REGULAR_EXCEPTION)) {
		info.si_code = __SI_FAULT;
		goto send_sigfpe;
	}

	if (enable_ex & FPE_IOC)
		info.si_code = FPE_FLTINV;
	else if (enable_ex & FPE_DZC)
		info.si_code = FPE_FLTDIV;
	else if (enable_ex & FPE_UFC)
		info.si_code = FPE_FLTUND;
	else if (enable_ex & FPE_OFC)
		info.si_code = FPE_FLTOVF;
	else if (enable_ex & FPE_IXC)
		info.si_code = FPE_FLTRES;

send_sigfpe:
	sig = SIGFPE;
	info.si_signo = SIGFPE;
	info.si_errno = 0;
	info.si_addr = (void *)(fpe_exception_pc);
	send_sig_info(sig, &info, current);
}

inline unsigned int get_fpu_insn(struct pt_regs *regs)
{
	unsigned short inst_low, inst_high;
	unsigned int result, inst;
	unsigned int inst_ptr = instruction_pointer(regs);

	result = 0;
	inst = 0;
	inst_low = 0;
	inst_high = 0;

	if (__get_user(inst_low, (unsigned short *)inst_ptr) == 0) {
		if (__get_user(inst_high, (unsigned short *)(inst_ptr + 2)) ==
		    0) {
			inst = inst_high | ((unsigned int)inst_low << 16);
			if (INST_IS_FP(inst))
				result = inst;
		}
	}

	return result;
}

inline int do_fpu_insn(unsigned int inst, struct pt_regs *regs)
{
	int index;
	int sop, pcode;
	int vrx, vry, vrz;
	struct inst_data inst_data;

	fpe_exception_pc = regs->pc;
	inst_data.inst = inst;
	inst_data.regs = regs;
	save_to_user_fp(&current->thread.user_fp);

#if defined(CONFIG_CPU_CK810)
	/* array1's 13 bit is 0, array2's is 1 */
	if (inst & FPUV2_LDST_MASK) {
		index = FPUV2_LDST_INSN_INDEX(inst);
		vrx = CSKY_INSN_RX(inst);
		vry = CSKY_INSN_VRY(inst);
		vrz = CSKY_INSN_VRZ(inst);

		if (likely(inst_op2[index].fn != NULL))
			inst_op2[index].fn(vrx, vry, vrz, &inst_data);
		else
			goto fault;
	} else {
		index = FPUV2_INSN_INDEX(inst);
		vrx = CSKY_INSN_VRX(inst);
		vry = CSKY_INSN_VRY(inst);
		vrz = CSKY_INSN_VRZ(inst);

		if (likely(inst_op1[index].fn != NULL))
			inst_op1[index].fn(vrx, vry, vrz, &inst_data);
		else
			goto fault;
	}
#elif defined(CONFIG_CPU_CK860)
	index = CSKY_INSN_OP(inst);
	sop = CSKY_INSN_SOP(inst);
	pcode = CSKY_INSN_PCODE(inst);

	if (likely(fpu_vfp_insn[index].sop[sop].pcode[pcode].func.fpu !=
		   NULL)) {
		vrx = CSKY_INSN_VRX(inst);
		vry = CSKY_INSN_VRY(inst);
		vrz = CSKY_INSN_VRZ(inst);
		fpu_vfp_insn[index].sop[sop].pcode[pcode].func.fpu(
			vrx, vry, vrz, &inst_data);

	} else {
		goto fault;
	}
#else
	return 0;
#endif

	restore_from_user_fp(&current->thread.user_fp);
	return 0;
fault:
	restore_from_user_fp(&current->thread.user_fp);
	return 1;
}
