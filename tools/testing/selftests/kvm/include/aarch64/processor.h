/* SPDX-License-Identifier: GPL-2.0 */
/*
 * AArch64 processor specific defines
 *
 * Copyright (C) 2018, Red Hat, Inc.
 */
#ifndef SELFTEST_KVM_PROCESSOR_H
#define SELFTEST_KVM_PROCESSOR_H

#include "kvm_util.h"
#include <linux/stringify.h>


#define ARM64_CORE_REG(x) (KVM_REG_ARM64 | KVM_REG_SIZE_U64 | \
			   KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(x))

#define CPACR_EL1               3, 0,  1, 0, 2
#define TCR_EL1                 3, 0,  2, 0, 2
#define MAIR_EL1                3, 0, 10, 2, 0
#define TTBR0_EL1               3, 0,  2, 0, 0
#define SCTLR_EL1               3, 0,  1, 0, 0
#define VBAR_EL1                3, 0, 12, 0, 0

#define ID_AA64DFR0_EL1         3, 0,  0, 5, 0

/*
 * Default MAIR
 *                  index   attribute
 * DEVICE_nGnRnE      0     0000:0000
 * DEVICE_nGnRE       1     0000:0100
 * DEVICE_GRE         2     0000:1100
 * NORMAL_NC          3     0100:0100
 * NORMAL             4     1111:1111
 * NORMAL_WT          5     1011:1011
 */
#define DEFAULT_MAIR_EL1 ((0x00ul << (0 * 8)) | \
			  (0x04ul << (1 * 8)) | \
			  (0x0cul << (2 * 8)) | \
			  (0x44ul << (3 * 8)) | \
			  (0xfful << (4 * 8)) | \
			  (0xbbul << (5 * 8)))

static inline void get_reg(struct kvm_vm *vm, uint32_t vcpuid, uint64_t id, uint64_t *addr)
{
	struct kvm_one_reg reg;
	reg.id = id;
	reg.addr = (uint64_t)addr;
	vcpu_ioctl(vm, vcpuid, KVM_GET_ONE_REG, &reg);
}

static inline void set_reg(struct kvm_vm *vm, uint32_t vcpuid, uint64_t id, uint64_t val)
{
	struct kvm_one_reg reg;
	reg.id = id;
	reg.addr = (uint64_t)&val;
	vcpu_ioctl(vm, vcpuid, KVM_SET_ONE_REG, &reg);
}

void aarch64_vcpu_setup(struct kvm_vm *vm, int vcpuid, struct kvm_vcpu_init *init);
void aarch64_vcpu_add_default(struct kvm_vm *vm, uint32_t vcpuid,
			      struct kvm_vcpu_init *init, void *guest_code);

struct ex_regs {
	u64 regs[31];
	u64 sp;
	u64 pc;
	u64 pstate;
};

#define VECTOR_NUM	16

enum {
	VECTOR_SYNC_CURRENT_SP0,
	VECTOR_IRQ_CURRENT_SP0,
	VECTOR_FIQ_CURRENT_SP0,
	VECTOR_ERROR_CURRENT_SP0,

	VECTOR_SYNC_CURRENT,
	VECTOR_IRQ_CURRENT,
	VECTOR_FIQ_CURRENT,
	VECTOR_ERROR_CURRENT,

	VECTOR_SYNC_LOWER_64,
	VECTOR_IRQ_LOWER_64,
	VECTOR_FIQ_LOWER_64,
	VECTOR_ERROR_LOWER_64,

	VECTOR_SYNC_LOWER_32,
	VECTOR_IRQ_LOWER_32,
	VECTOR_FIQ_LOWER_32,
	VECTOR_ERROR_LOWER_32,
};

#define VECTOR_IS_SYNC(v) ((v) == VECTOR_SYNC_CURRENT_SP0 || \
			   (v) == VECTOR_SYNC_CURRENT     || \
			   (v) == VECTOR_SYNC_LOWER_64    || \
			   (v) == VECTOR_SYNC_LOWER_32)

#define ESR_EC_NUM		64
#define ESR_EC_SHIFT		26
#define ESR_EC_MASK		(ESR_EC_NUM - 1)

#define ESR_EC_SVC64		0x15
#define ESR_EC_HW_BP_CURRENT	0x31
#define ESR_EC_SSTEP_CURRENT	0x33
#define ESR_EC_WP_CURRENT	0x35
#define ESR_EC_BRK_INS		0x3c

void vm_init_descriptor_tables(struct kvm_vm *vm);
void vcpu_init_descriptor_tables(struct kvm_vm *vm, uint32_t vcpuid);

typedef void(*handler_fn)(struct ex_regs *);
void vm_install_exception_handler(struct kvm_vm *vm,
		int vector, handler_fn handler);
void vm_install_sync_handler(struct kvm_vm *vm,
		int vector, int ec, handler_fn handler);

/*
 * ARMv8 ARM reserves the following encoding for system registers:
 * (Ref: ARMv8 ARM, Section: "System instruction class encoding overview",
 *  C5.2, version:ARM DDI 0487A.f)
 *	[20-19] : Op0
 *	[18-16] : Op1
 *	[15-12] : CRn
 *	[11-8]  : CRm
 *	[7-5]   : Op2
 */
#define Op0_shift	19
#define Op0_mask	0x3
#define Op1_shift	16
#define Op1_mask	0x7
#define CRn_shift	12
#define CRn_mask	0xf
#define CRm_shift	8
#define CRm_mask	0xf
#define Op2_shift	5
#define Op2_mask	0x7

/*
 * When accessed from guests, the ARM64_SYS_REG() doesn't work since it
 * generates a different encoding for additional KVM processing, and is
 * only suitable for userspace to access the register via ioctls.
 * Hence, define a 'pure' sys_reg() here to generate the encodings as per spec.
 */
#define sys_reg(op0, op1, crn, crm, op2) \
	(((op0) << Op0_shift) | ((op1) << Op1_shift) | \
	 ((crn) << CRn_shift) | ((crm) << CRm_shift) | \
	 ((op2) << Op2_shift))

asm(
"	.irp	num,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30\n"
"	.equ	.L__reg_num_x\\num, \\num\n"
"	.endr\n"
"	.equ	.L__reg_num_xzr, 31\n"
"\n"
"	.macro	mrs_s, rt, sreg\n"
"	.inst	0xd5200000|(\\sreg)|(.L__reg_num_\\rt)\n"
"	.endm\n"
"\n"
"	.macro	msr_s, sreg, rt\n"
"	.inst	0xd5000000|(\\sreg)|(.L__reg_num_\\rt)\n"
"	.endm\n"
);

/*
 * read_sysreg_s() and write_sysreg_s()'s 'reg' has to be encoded via sys_reg()
 */
#define read_sysreg_s(reg) ({						\
	u64 __val;							\
	asm volatile("mrs_s %0, "__stringify(reg) : "=r" (__val));	\
	__val;								\
})

#define write_sysreg_s(reg, val) do {					\
	u64 __val = (u64)val;						\
	asm volatile("msr_s "__stringify(reg) ", %x0" : : "rZ" (__val));\
} while (0)

#define write_sysreg(reg, val)						  \
({									  \
	u64 __val = (u64)(val);						  \
	asm volatile("msr " __stringify(reg) ", %x0" : : "rZ" (__val));	  \
})

#define read_sysreg(reg)						  \
({	u64 val;							  \
	asm volatile("mrs %0, "__stringify(reg) : "=r"(val) : : "memory");\
	val;								  \
})

static inline void cpu_relax(void)
{
	asm volatile("yield" ::: "memory");
}

#define isb()		asm volatile("isb" : : : "memory")
#define dsb(opt)	asm volatile("dsb " #opt : : : "memory")
#define dmb(opt)	asm volatile("dmb " #opt : : : "memory")

#define dma_wmb()	dmb(oshst)
#define __iowmb()	dma_wmb()

#define dma_rmb()	dmb(oshld)

#define __iormb(v)							\
({									\
	unsigned long tmp;						\
									\
	dma_rmb();							\
									\
	/*								\
	 * Courtesy of arch/arm64/include/asm/io.h:			\
	 * Create a dummy control dependency from the IO read to any	\
	 * later instructions. This ensures that a subsequent call	\
	 * to udelay() will be ordered due to the ISB in __delay().	\
	 */								\
	asm volatile("eor	%0, %1, %1\n"				\
		     "cbnz	%0, ."					\
		     : "=r" (tmp) : "r" ((unsigned long)(v))		\
		     : "memory");					\
})

static __always_inline void __raw_writel(u32 val, volatile void *addr)
{
	asm volatile("str %w0, [%1]" : : "rZ" (val), "r" (addr));
}

static __always_inline u32 __raw_readl(const volatile void *addr)
{
	u32 val;
	asm volatile("ldr %w0, [%1]" : "=r" (val) : "r" (addr));
	return val;
}

#define writel_relaxed(v,c)	((void)__raw_writel((__force u32)cpu_to_le32(v),(c)))
#define readl_relaxed(c)	({ u32 __r = le32_to_cpu((__force __le32)__raw_readl(c)); __r; })

#define writel(v,c)		({ __iowmb(); writel_relaxed((v),(c));})
#define readl(c)		({ u32 __v = readl_relaxed(c); __iormb(__v); __v; })

static inline void local_irq_enable(void)
{
	asm volatile("msr daifclr, #3" : : : "memory");
}

static inline void local_irq_disable(void)
{
	asm volatile("msr daifset, #3" : : : "memory");
}

#define MPIDR_LEVEL_BITS 8
#define MPIDR_LEVEL_SHIFT(level) (MPIDR_LEVEL_BITS * level)
#define MPIDR_LEVEL_MASK ((1 << MPIDR_LEVEL_BITS) - 1)
#define MPIDR_AFFINITY_LEVEL(mpidr, level) \
	((mpidr >> MPIDR_LEVEL_SHIFT(level)) & MPIDR_LEVEL_MASK)

static inline uint32_t get_vcpuid(void)
{
	uint32_t vcpuid = 0;
	uint64_t mpidr = read_sysreg(mpidr_el1);

	/* KVM limits only 16 vCPUs at level 0 */
	vcpuid = mpidr & 0x0f;
	vcpuid |= MPIDR_AFFINITY_LEVEL(mpidr, 1) << 4;
	vcpuid |= MPIDR_AFFINITY_LEVEL(mpidr, 2) << 12;

	return vcpuid;
}

#endif /* SELFTEST_KVM_PROCESSOR_H */
