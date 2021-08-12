// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/of_address.h>
#include <linux/panic_notifier.h>
#include <linux/reboot.h>
#include <linux/slab.h>

#define DLOAD_MODE_COOKIE1	0xe47b337d
#define DLOAD_MODE_COOKIE2	0xce14091a

struct qcom_dload_mode {
	void __iomem *dload_mode;
	void __iomem *sdi_disable;
	bool in_panic;
};

static struct qcom_dload_mode *dmode;

static int dload_mode_reboot_notifier(struct notifier_block *self,
			       unsigned long v, void *p)
{
	/*
	 * Don't enter download mode for normal reboot, so clear the
	 * download mode cookie and disable SDI.
	 */
	if (!dmode->in_panic) {
		writel(0, dmode->dload_mode);
		writel(1, dmode->sdi_disable);
	}

	return NOTIFY_DONE;
}

static int dload_mode_panic_notifier(struct notifier_block *self,
			       unsigned long v, void *p)
{
	dmode->in_panic = true;

	return NOTIFY_DONE;
}

static struct notifier_block dload_mode_reboot_nb = {
	.notifier_call = dload_mode_reboot_notifier,
};

static struct notifier_block dload_mode_panic_nb = {
	.notifier_call = dload_mode_panic_notifier,
};

static void qcom_unset_dload_mode(void)
{
	writel(0, dmode->dload_mode);
	writel(0, dmode->dload_mode + sizeof(u32));
}

static void qcom_set_dload_mode(void)
{
	writel(DLOAD_MODE_COOKIE1, dmode->dload_mode);
	writel(DLOAD_MODE_COOKIE2, dmode->dload_mode + sizeof(u32));
}

static int __init qcom_dload_mode_init(void)
{
	struct resource imem, sdi_base;
	struct of_phandle_args args;
	struct device_node *np;
	int ret;

	dmode = kzalloc(sizeof(*dmode), GFP_KERNEL);
	if (!dmode)
		return -ENOMEM;

	np = of_find_compatible_node(NULL, NULL, "qcom,dload-mode");
	if (!np)
		return -ENOENT;

	ret = of_address_to_resource(np, 0, &imem);
	if (ret < 0)
		return ret;

	ret = of_parse_phandle_with_fixed_args(np,
					       "qcom,sdi-disable-regs",
					       2, 0, &args);
	of_node_put(np);
	if (ret < 0) {
		pr_err("Failed to parse sdi-disable-regs\n");
		return -EINVAL;
	}

	ret = of_address_to_resource(args.np, 0, &sdi_base);
	of_node_put(args.np);
	if (ret < 0)
		return ret;

	dmode->dload_mode = ioremap(imem.start, resource_size(&imem));
	if (!dmode->dload_mode) {
		pr_err("Failed to map download mode region\n");
		return -ENOMEM;
	}

	dmode->sdi_disable = ioremap(sdi_base.start + args.args[0], args.args[1]);
	if (!dmode->sdi_disable) {
		pr_err("Failed to map sdi disable region\n");
		return -ENOMEM;
	}

	ret = atomic_notifier_chain_register(&panic_notifier_list,
					     &dload_mode_panic_nb);
	if (ret) {
		pr_err("Failed to register panic notifier: %d\n", ret);
		return ret;
	}

	ret = register_reboot_notifier(&dload_mode_reboot_nb);
	if (ret) {
		pr_err("Failed to register reboot notifier: %d\n", ret);
		return ret;
	}

	/*
	 * Set the download mode cookies here so that after this point on
	 * any crash handled either by kernel or other crashes such as
	 * watchdog bite handled by other entities like secure world,
	 * download mode is entered.
	 */
	qcom_set_dload_mode();

	return 0;
}
device_initcall(qcom_dload_mode_init);

static void __exit qcom_dload_mode_exit(void)
{
	qcom_unset_dload_mode();
	unregister_reboot_notifier(&dload_mode_reboot_nb);
	atomic_notifier_chain_unregister(&panic_notifier_list,
					 &dload_mode_panic_nb);
	iounmap(dmode->sdi_disable);
	iounmap(dmode->dload_mode);
	kfree(dmode);
	dmode = NULL;
}
module_exit(qcom_dload_mode_exit);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. Download Mode driver");
MODULE_LICENSE("GPL v2");
