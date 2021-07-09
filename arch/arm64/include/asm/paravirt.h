/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_ARM64_PARAVIRT_H
#define _ASM_ARM64_PARAVIRT_H

struct vcpu_state {
	bool	preempted;
	u8	reserved[63];
};

#ifdef CONFIG_PARAVIRT
#include <linux/static_call_types.h>

struct static_key;
extern struct static_key paravirt_steal_enabled;
extern struct static_key paravirt_steal_rq_enabled;

u64 dummy_steal_clock(int cpu);

DECLARE_STATIC_CALL(pv_steal_clock, dummy_steal_clock);

static inline u64 paravirt_steal_clock(int cpu)
{
	return static_call(pv_steal_clock)(cpu);
}

int __init pv_time_init(void);

bool dummy_vcpu_is_preempted(unsigned int cpu);

extern struct static_key pv_vcpu_is_preempted_enabled;
DECLARE_STATIC_CALL(pv_vcpu_is_preempted, dummy_vcpu_is_preempted);

static inline bool paravirt_vcpu_is_preempted(unsigned int cpu)
{
	return static_call(pv_vcpu_is_preempted)(cpu);
}

int __init pv_vcpu_state_init(void);

#else

#define pv_vcpu_state_init() do {} while (0)

#define pv_time_init() do {} while (0)

#endif // CONFIG_PARAVIRT

#endif
