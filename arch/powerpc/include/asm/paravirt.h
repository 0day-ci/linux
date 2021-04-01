/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _ASM_POWERPC_PARAVIRT_H
#define _ASM_POWERPC_PARAVIRT_H

#include <linux/jump_label.h>
#include <asm/smp.h>
#ifdef CONFIG_PPC64
#include <asm/paca.h>
#include <asm/hvcall.h>
#endif

#ifdef CONFIG_PPC_SPLPAR
#include <linux/smp.h>
#include <asm/kvm_guest.h>
#include <asm/cputhreads.h>

DECLARE_STATIC_KEY_FALSE(shared_processor);

static inline bool is_shared_processor(void)
{
	return static_branch_unlikely(&shared_processor);
}

/* If bit 0 is set, the cpu has been preempted */
static inline u32 yield_count_of(int cpu)
{
	__be32 yield_count = READ_ONCE(lppaca_of(cpu).yield_count);
	return be32_to_cpu(yield_count);
}

static inline void yield_to_preempted(int cpu, u32 yield_count)
{
	plpar_hcall_norets(H_CONFER, get_hard_smp_processor_id(cpu), yield_count);
}

static inline void prod_cpu(int cpu)
{
	plpar_hcall_norets(H_PROD, get_hard_smp_processor_id(cpu));
}

static inline void yield_to_any(void)
{
	plpar_hcall_norets(H_CONFER, -1, 0);
}

/* Find if the previous physical CPU of this vcpu is available_idle or not */
static inline void pcpu_available_instantly(int vcpu, unsigned long *is_idle)
{
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE];

	if (plpar_hcall(H_IDLE_HINT, retbuf, vcpu) == H_SUCCESS)
		*is_idle = retbuf[0];
}
#else
static inline bool is_shared_processor(void)
{
	return false;
}

static inline u32 yield_count_of(int cpu)
{
	return 0;
}

extern void ___bad_yield_to_preempted(void);
static inline void yield_to_preempted(int cpu, u32 yield_count)
{
	___bad_yield_to_preempted(); /* This would be a bug */
}

extern void ___bad_yield_to_any(void);
static inline void yield_to_any(void)
{
	___bad_yield_to_any(); /* This would be a bug */
}

extern void ___bad_prod_cpu(void);
static inline void prod_cpu(int cpu)
{
	___bad_prod_cpu(); /* This would be a bug */
}

#endif

#define vcpu_is_preempted vcpu_is_preempted
static inline bool vcpu_is_preempted(int cpu)
{
	unsigned long is_idle = 0;

	if (!is_shared_processor())
		return false;

#ifdef CONFIG_PPC_SPLPAR
	if (!is_kvm_guest()) {
		int first_cpu = cpu_first_thread_sibling(smp_processor_id());

		/*
		 * Preemption can only happen at core granularity. This CPU
		 * is not preempted if one of the CPU of this core is not
		 * preempted.
		 */
		if (cpu_first_thread_sibling(cpu) == first_cpu)
			return false;
	}
#endif

	if (yield_count_of(cpu) & 1) {
#ifdef CONFIG_PPC_SPLPAR
		pcpu_available_instantly(cpu, &is_idle);
#endif

		if (!is_idle)
			return true;
	}
	return false;
}

static inline bool pv_is_native_spin_unlock(void)
{
	return !is_shared_processor();
}

#endif /* _ASM_POWERPC_PARAVIRT_H */
