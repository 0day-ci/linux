// SPDX-License-Identifier: GPL-2.0
/*
 * Broadcom STB PSCI based system wide PM support
 *
 * Copyright © 2018-2022 Broadcom
 */

#define pr_fmt(fmt) "brcmstb-pm-psci: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/arm-smccc.h>
#include <linux/panic_notifier.h>
#include <linux/psci.h>
#include <linux/suspend.h>
#include <linux/soc/brcmstb/brcmstb.h>
#include <linux/soc/brcmstb/brcmstb-smccc.h>
#include <linux/reboot.h>
#include <linux/kobject.h>

#include <uapi/linux/psci.h>

#include <asm/suspend.h>
#include <asm/system_misc.h>

#include "aon_defs.h"

static psci_fn *invoke_psci_fn;
static bool brcmstb_psci_system_reset2_supported;
static bool brcmstb_psci_system_suspend_supported;
static bool brcmstb_psci_cpu_retention = true;

static int brcmstb_psci_integ_region(unsigned long function_id,
				     unsigned long base,
				     unsigned long size)
{
	unsigned long end;

	if (!size)
		return -EINVAL;

	end = DIV_ROUND_UP(base + size, SIP_MIN_REGION_SIZE);
	base /= SIP_MIN_REGION_SIZE;
	size = end - base;

	return invoke_psci_fn(function_id, base, size, 0);
}

static int __maybe_unused brcmstb_psci_integ_region_set(unsigned long base,
							unsigned long size)
{
	return brcmstb_psci_integ_region(SIP_FUNC_INTEG_REGION_SET, base, size);
}

static int __maybe_unused brcmstb_psci_integ_region_del(unsigned long base,
							unsigned long size)
{
	return brcmstb_psci_integ_region(SIP_FUNC_INTEG_REGION_DEL, base, size);
}

static int brcmstb_psci_integ_region_reset_all(void)
{
	return invoke_psci_fn(SIP_FUNC_INTEG_REGION_RESET_ALL, 0, 0, 0);
}

static int brcmstb_psci_sys_reset(struct notifier_block *nb,
				  unsigned long action, void *data)
{
	const char *cmd = data;
	/*
	 * reset_type[31] = 0 (architectural)
	 * reset_type[30:0] = 0 (SYSTEM_WARM_RESET)
	 * cookie = 0 (ignored by the implementation)
	 */
	uint32_t reboot_type = 0;

	if ((action == REBOOT_COLD || action == REBOOT_WARM ||
	    action == REBOOT_SOFT) &&
	    brcmstb_psci_system_reset2_supported) {
		if (cmd && !strcmp(cmd, "powercycle"))
			reboot_type = BIT(31) | 1;
		invoke_psci_fn(PSCI_FN_NATIVE(1_1, SYSTEM_RESET2), reboot_type, 0, 0);
	} else {
		invoke_psci_fn(PSCI_0_2_FN_SYSTEM_RESET, 0, 0, 0);
	}

	return NOTIFY_DONE;
}

static struct notifier_block brcmstb_psci_sys_reset_nb = {
	.notifier_call	= brcmstb_psci_sys_reset,
	.priority	= 128,
};

void brcmstb_psci_sys_poweroff(void)
{
	invoke_psci_fn(PSCI_0_2_FN_SYSTEM_OFF, 0, 0, 0);
}

static int psci_features(u32 psci_func_id)
{
	u32 features_func_id;

	switch (ARM_SMCCC_OWNER_NUM(psci_func_id)) {
	case ARM_SMCCC_OWNER_SIP:
		features_func_id = SIP_FUNC_PSCI_FEATURES;
		break;
	case ARM_SMCCC_OWNER_STANDARD:
		features_func_id = PSCI_1_0_FN_PSCI_FEATURES;
		break;
	default:
		return PSCI_RET_NOT_SUPPORTED;
	}

	return invoke_psci_fn(features_func_id, psci_func_id, 0, 0);
}

static int brcmstb_psci_enter(suspend_state_t state)
{
	/* Request a SYSTEM level power state with retention */
	u32 pstate = 2 << PSCI_0_2_POWER_STATE_AFFL_SHIFT |
		     !brcmstb_psci_cpu_retention << PSCI_0_2_POWER_STATE_TYPE_SHIFT;
	int ret = -EINVAL;

	switch (state) {
	case PM_SUSPEND_STANDBY:
		ret = psci_cpu_suspend_enter(pstate);
		break;
	case PM_SUSPEND_MEM:
		ret = brcmstb_psci_system_suspend_supported ?
			 psci_system_suspend_enter(state) : -EINVAL;
		break;
	}

	return ret;
}

static int brcmstb_psci_valid(suspend_state_t state)
{
	switch (state) {
	case PM_SUSPEND_STANDBY:
		return true;
	case PM_SUSPEND_MEM:
		return brcmstb_psci_system_suspend_supported;
	default:
		return false;
	}
}

static const struct platform_suspend_ops brcmstb_psci_ops = {
	.enter	= brcmstb_psci_enter,
	.valid	= brcmstb_psci_valid,
};

static int brcmstb_psci_panic_notify(struct notifier_block *nb,
				     unsigned long action, void *data)
{
	int ret;

	ret = invoke_psci_fn(SIP_FUNC_PANIC_NOTIFY, BRCMSTB_PANIC_MAGIC, 0, 0);
	if (ret != PSCI_RET_SUCCESS)
		return NOTIFY_BAD;

	return NOTIFY_DONE;
}

static struct notifier_block brcmstb_psci_nb = {
	.notifier_call = brcmstb_psci_panic_notify,
};

static ssize_t brcmstb_psci_version_show(struct kobject *kobj,
					 struct kobj_attribute *attr,
					 char *buf)
{
	struct arm_smccc_res res = { };
	u32 version;

	if (invoke_psci_fn == __invoke_psci_fn_hvc)
		arm_smccc_hvc(SIP_FUNC_PSCI_BRCMSTB_VERSION,
			      0, 0, 0, 0, 0, 0, 0, &res);
	else
		arm_smccc_smc(SIP_FUNC_PSCI_BRCMSTB_VERSION,
			      0, 0, 0, 0, 0, 0, 0, &res);

	if (res.a0 != PSCI_RET_SUCCESS)
		return -EOPNOTSUPP;

	version = res.a1;

	return sprintf(buf, "%d.%d.%d.%d\n",
		       (version >> 24) & 0xff, (version >> 16) & 0xff,
		       (version >> 8) & 0xff, version & 0xff);
}

static struct kobj_attribute brcmstb_psci_version_attr =
	__ATTR(mon_version, 0400, brcmstb_psci_version_show, NULL);

static ssize_t brcmstb_psci_cpu_retention_show(struct kobject *kobj,
					       struct kobj_attribute *attr,
					       char *buf)
{
	return sprintf(buf, "%d\n", brcmstb_psci_cpu_retention);
}

static ssize_t brcmstb_psci_cpu_retention_store(struct kobject *kobj,
						struct kobj_attribute *attr,
						const char *buf, size_t count)
{
	int ret, val;

	ret = kstrtoint(buf, 10, &val);
	if (ret < 0)
		return ret;

	if (val != 0 && val != 1)
		return -EINVAL;

	brcmstb_psci_cpu_retention = !!val;

	return count;
}

static struct kobj_attribute brcmstb_psci_cpu_retention_attr =
	__ATTR(cpu_retention, 0644, brcmstb_psci_cpu_retention_show,
	       brcmstb_psci_cpu_retention_store);

static const struct attribute *brcmstb_psci_attributes[] = {
	&brcmstb_psci_version_attr.attr,
	&brcmstb_psci_cpu_retention_attr.attr,
	NULL,
};

static int brcmstb_pm_psci_init(void)
{
	unsigned long funcs_id[] = {
		PSCI_0_2_FN_SYSTEM_OFF,
		SIP_FUNC_INTEG_REGION_SET,
		SIP_FUNC_INTEG_REGION_DEL,
		SIP_FUNC_INTEG_REGION_RESET_ALL,
	};
	struct arm_smccc_res res = { };
	struct kobject *brcmstb_kobj;
	unsigned int i;
	int ret;

	switch (arm_smccc_1_1_get_conduit()) {
	case SMCCC_CONDUIT_HVC:
		invoke_psci_fn = __invoke_psci_fn_hvc;
		break;
	case SMCCC_CONDUIT_SMC:
		invoke_psci_fn = __invoke_psci_fn_smc;
		break;
	default:
		return -EINVAL;
	}

	/* Check the revision of monitor */
	if (invoke_psci_fn == __invoke_psci_fn_hvc)
		arm_smccc_hvc(SIP_SVC_REVISION,
			      0, 0, 0, 0, 0, 0, 0, &res);
	else
		arm_smccc_smc(SIP_SVC_REVISION,
			      0, 0, 0, 0, 0, 0, 0, &res);

	/* Test for our supported features */
	for (i = 0; i < ARRAY_SIZE(funcs_id); i++) {
		ret = psci_features(funcs_id[i]);
		if (ret == PSCI_RET_NOT_SUPPORTED) {
			pr_err("Firmware does not support function 0x%lx\n",
			       funcs_id[i]);
			return -EOPNOTSUPP;
		}
	}

	ret = psci_features(PSCI_FN_NATIVE(1_1, SYSTEM_RESET2));
	if (ret != PSCI_RET_NOT_SUPPORTED)
		brcmstb_psci_system_reset2_supported = true;

	ret = psci_features(PSCI_FN_NATIVE(1_0, SYSTEM_SUSPEND));
	if (ret != PSCI_RET_NOT_SUPPORTED)
		brcmstb_psci_system_suspend_supported = true;

	ret = brcmstb_psci_integ_region_reset_all();
	if (ret != PSCI_RET_SUCCESS) {
		pr_err("Error resetting all integrity checking regions\n");
		return -EIO;
	}

	if (res.a0 == SIP_REVISION_MAJOR && res.a1 < SIP_REVISION_MINOR) {
		pr_info("Firmware is too old! Please update\n");
		return -EOPNOTSUPP;
	}

	brcmstb_kobj = kobject_create_and_add("brcmstb", firmware_kobj);
	if (brcmstb_kobj) {
		ret = sysfs_create_files(brcmstb_kobj, brcmstb_psci_attributes);
		if (ret)
			return ret;
	}

	pm_power_off = brcmstb_psci_sys_poweroff;
	register_restart_handler(&brcmstb_psci_sys_reset_nb);
	suspend_set_ops(&brcmstb_psci_ops);
	atomic_notifier_chain_register(&panic_notifier_list,
				       &brcmstb_psci_nb);

	pr_info("Using PSCI based system PM (full featured)\n");

	return 0;
}
module_init(brcmstb_pm_psci_init);

MODULE_AUTHOR("Broadcom");
MODULE_DESCRIPTION("Broadcom STB PM PSCI operations");
MODULE_LICENSE("GPL v2");
