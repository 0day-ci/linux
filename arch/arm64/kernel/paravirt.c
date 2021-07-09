// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 * Copyright (C) 2013 Citrix Systems
 *
 * Author: Stefano Stabellini <stefano.stabellini@eu.citrix.com>
 */

#define pr_fmt(fmt) "arm-pv: " fmt

#include <linux/arm-smccc.h>
#include <linux/cpuhotplug.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/jump_label.h>
#include <linux/printk.h>
#include <linux/psci.h>
#include <linux/reboot.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/static_call.h>

#include <asm/paravirt.h>
#include <asm/pvclock-abi.h>
#include <asm/smp_plat.h>

struct static_key paravirt_steal_enabled;
struct static_key paravirt_steal_rq_enabled;

static u64 native_steal_clock(int cpu)
{
	return 0;
}

DEFINE_STATIC_CALL(pv_steal_clock, native_steal_clock);

struct pv_time_stolen_time_region {
	struct pvclock_vcpu_stolen_time *kaddr;
};

static DEFINE_PER_CPU(struct pv_time_stolen_time_region, stolen_time_region);

static DEFINE_PER_CPU(struct vcpu_state, vcpus_states);
struct static_key pv_vcpu_is_preempted_enabled;

DEFINE_STATIC_CALL(pv_vcpu_is_preempted, dummy_vcpu_is_preempted);

static bool steal_acc = true;
static int __init parse_no_stealacc(char *arg)
{
	steal_acc = false;
	return 0;
}

early_param("no-steal-acc", parse_no_stealacc);

/* return stolen time in ns by asking the hypervisor */
static u64 para_steal_clock(int cpu)
{
	struct pv_time_stolen_time_region *reg;

	reg = per_cpu_ptr(&stolen_time_region, cpu);

	/*
	 * paravirt_steal_clock() may be called before the CPU
	 * online notification callback runs. Until the callback
	 * has run we just return zero.
	 */
	if (!reg->kaddr)
		return 0;

	return le64_to_cpu(READ_ONCE(reg->kaddr->stolen_time));
}

static int stolen_time_cpu_down_prepare(unsigned int cpu)
{
	struct pv_time_stolen_time_region *reg;

	reg = this_cpu_ptr(&stolen_time_region);
	if (!reg->kaddr)
		return 0;

	memunmap(reg->kaddr);
	memset(reg, 0, sizeof(*reg));

	return 0;
}

static int stolen_time_cpu_online(unsigned int cpu)
{
	struct pv_time_stolen_time_region *reg;
	struct arm_smccc_res res;

	reg = this_cpu_ptr(&stolen_time_region);

	arm_smccc_1_1_invoke(ARM_SMCCC_HV_PV_TIME_ST, &res);

	if (res.a0 == SMCCC_RET_NOT_SUPPORTED)
		return -EINVAL;

	reg->kaddr = memremap(res.a0,
			      sizeof(struct pvclock_vcpu_stolen_time),
			      MEMREMAP_WB);

	if (!reg->kaddr) {
		pr_warn("Failed to map stolen time data structure\n");
		return -ENOMEM;
	}

	if (le32_to_cpu(reg->kaddr->revision) != 0 ||
	    le32_to_cpu(reg->kaddr->attributes) != 0) {
		pr_warn_once("Unexpected revision or attributes in stolen time data\n");
		return -ENXIO;
	}

	return 0;
}

static int __init pv_time_init_stolen_time(void)
{
	int ret;

	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN,
				"hypervisor/arm/pvtime:online",
				stolen_time_cpu_online,
				stolen_time_cpu_down_prepare);
	if (ret < 0)
		return ret;
	return 0;
}

static bool __init has_pv_steal_clock(void)
{
	struct arm_smccc_res res;

	/* To detect the presence of PV time support we require SMCCC 1.1+ */
	if (arm_smccc_1_1_get_conduit() == SMCCC_CONDUIT_NONE)
		return false;

	arm_smccc_1_1_invoke(ARM_SMCCC_ARCH_FEATURES_FUNC_ID,
			     ARM_SMCCC_HV_PV_TIME_FEATURES, &res);

	if (res.a0 != SMCCC_RET_SUCCESS)
		return false;

	arm_smccc_1_1_invoke(ARM_SMCCC_HV_PV_TIME_FEATURES,
			     ARM_SMCCC_HV_PV_TIME_ST, &res);

	return (res.a0 == SMCCC_RET_SUCCESS);
}

int __init pv_time_init(void)
{
	int ret;

	if (!has_pv_steal_clock())
		return 0;

	ret = pv_time_init_stolen_time();
	if (ret)
		return ret;

	static_call_update(pv_steal_clock, para_steal_clock);

	static_key_slow_inc(&paravirt_steal_enabled);
	if (steal_acc)
		static_key_slow_inc(&paravirt_steal_rq_enabled);

	pr_info("using stolen time PV\n");

	return 0;
}

bool dummy_vcpu_is_preempted(unsigned int cpu)
{
	return false;
}

static bool __vcpu_is_preempted(unsigned int cpu)
{
	struct vcpu_state *st;

	st = &per_cpu(vcpus_states, cpu);
	return READ_ONCE(st->preempted);
}

static bool has_pv_vcpu_state(void)
{
	struct arm_smccc_res res;

	/* To detect the presence of PV time support we require SMCCC 1.1+ */
	if (arm_smccc_1_1_get_conduit() == SMCCC_CONDUIT_NONE)
		return false;

	arm_smccc_1_1_invoke(ARM_SMCCC_ARCH_FEATURES_FUNC_ID,
			     ARM_SMCCC_HV_PV_VCPU_STATE_FEATURES,
			     &res);

	if (res.a0 != SMCCC_RET_SUCCESS)
		return false;
	return true;
}

static int __pv_vcpu_state_hook(unsigned int cpu, int event)
{
	struct arm_smccc_res res;
	struct vcpu_state *st;

	st = &per_cpu(vcpus_states, cpu);
	arm_smccc_1_1_invoke(event, virt_to_phys(st), &res);
	if (res.a0 != SMCCC_RET_SUCCESS)
		return -EINVAL;
	return 0;
}

static int vcpu_state_init(unsigned int cpu)
{
	int ret = __pv_vcpu_state_hook(cpu, ARM_SMCCC_HV_PV_VCPU_STATE_INIT);

	if (ret)
		pr_warn("Unable to ARM_SMCCC_HV_PV_STATE_INIT\n");
	return ret;
}

static int vcpu_state_release(unsigned int cpu)
{
	int ret = __pv_vcpu_state_hook(cpu, ARM_SMCCC_HV_PV_VCPU_STATE_RELEASE);

	if (ret)
		pr_warn("Unable to ARM_SMCCC_HV_PV_STATE_RELEASE\n");
	return ret;
}

static int pv_vcpu_state_register_hooks(void)
{
	int ret;

	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN,
				"hypervisor/arm/pvstate:starting",
				vcpu_state_init,
				vcpu_state_release);
	if (ret < 0)
		pr_warn("Failed to register CPU hooks\n");
	return 0;
}

int __init pv_vcpu_state_init(void)
{
	int ret;

	if (!has_pv_vcpu_state())
		return 0;

	ret = pv_vcpu_state_register_hooks();
	if (ret)
		return ret;

	static_call_update(pv_vcpu_is_preempted, __vcpu_is_preempted);
	static_key_slow_inc(&pv_vcpu_is_preempted_enabled);
	return 0;
}
