// SPDX-License-Identifier: GPL-2.0
/*
 * Cortex A57 and A53 EDAC L1 and L2 cache error detection
 *
 * Copyright (c) 2020 Pengutronix, Sascha Hauer <s.hauer@pengutronix.de>
 *
 * Based on Code from:
 * Copyright (c) 2018, NXP Semiconductor
 * Author: York Sun <york.sun@nxp.com>
 *
 */

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/of_device.h>
#include <linux/bitfield.h>
#include <asm/smp_plat.h>

#include "edac_module.h"

#define DRVNAME				"cortex-arm64-edac"

#define CPUMERRSR_EL1_RAMID		GENMASK(30, 24)

#define CPUMERRSR_EL1_VALID		BIT(31)
#define CPUMERRSR_EL1_FATAL		BIT(63)

#define L1_I_TAG_RAM			0x00
#define L1_I_DATA_RAM			0x01
#define L1_D_TAG_RAM			0x08
#define L1_D_DATA_RAM			0x09
#define L1_D_DIRTY_RAM			0x14
#define TLB_RAM				0x18

#define L2MERRSR_EL1_VALID		BIT(31)
#define L2MERRSR_EL1_FATAL		BIT(63)

struct merrsr {
	u64 cpumerr;
	u64 l2merr;
};

#define MESSAGE_SIZE 64

#define SYS_CPUMERRSR_EL1			sys_reg(3, 1, 15, 2, 2)
#define SYS_L2MERRSR_EL1			sys_reg(3, 1, 15, 2, 3)

static struct cpumask compat_mask;

static void report_errors(struct edac_device_ctl_info *edac_ctl, int cpu,
			  struct merrsr *merrsr)
{
	char msg[MESSAGE_SIZE];
	u64 cpumerr = merrsr->cpumerr;
	u64 l2merr = merrsr->l2merr;

	if (cpumerr & CPUMERRSR_EL1_VALID) {
		const char *str;
		bool fatal = cpumerr & CPUMERRSR_EL1_FATAL;

		switch (FIELD_GET(CPUMERRSR_EL1_RAMID, cpumerr)) {
		case L1_I_TAG_RAM:
			str = "L1-I Tag RAM";
			break;
		case L1_I_DATA_RAM:
			str = "L1-I Data RAM";
			break;
		case L1_D_TAG_RAM:
			str = "L1-D Tag RAM";
			break;
		case L1_D_DATA_RAM:
			str = "L1-D Data RAM";
			break;
		case L1_D_DIRTY_RAM:
			str = "L1 Dirty RAM";
			break;
		case TLB_RAM:
			str = "TLB RAM";
			break;
		default:
			str = "unknown";
			break;
		}

		snprintf(msg, MESSAGE_SIZE, "%s %s error(s) on CPU %d",
			 str, fatal ? "fatal" : "correctable", cpu);

		if (fatal)
			edac_device_handle_ue(edac_ctl, cpu, 0, msg);
		else
			edac_device_handle_ce(edac_ctl, cpu, 0, msg);
	}

	if (l2merr & L2MERRSR_EL1_VALID) {
		bool fatal = l2merr & L2MERRSR_EL1_FATAL;

		snprintf(msg, MESSAGE_SIZE, "L2 %s error(s) on CPU %d",
			 fatal ? "fatal" : "correctable", cpu);
		if (fatal)
			edac_device_handle_ue(edac_ctl, cpu, 1, msg);
		else
			edac_device_handle_ce(edac_ctl, cpu, 1, msg);
	}
}

static void read_errors(void *data)
{
	struct merrsr *merrsr = data;

	merrsr->cpumerr = read_sysreg_s(SYS_CPUMERRSR_EL1);
	write_sysreg_s(0, SYS_CPUMERRSR_EL1);
	merrsr->l2merr = read_sysreg_s(SYS_L2MERRSR_EL1);
	write_sysreg_s(0, SYS_L2MERRSR_EL1);
}

static void cortex_arm64_edac_check(struct edac_device_ctl_info *edac_ctl)
{
	struct merrsr merrsr;
	int cpu;

	for_each_cpu_and(cpu, cpu_online_mask, &compat_mask) {
		smp_call_function_single(cpu, read_errors, &merrsr, true);
		report_errors(edac_ctl, cpu, &merrsr);
	}
}

static int cortex_arm64_edac_probe(struct platform_device *pdev)
{
	struct edac_device_ctl_info *edac_ctl;
	struct device *dev = &pdev->dev;
	int rc;

	edac_ctl = edac_device_alloc_ctl_info(0, "cpu",
					      num_possible_cpus(), "L", 2, 1, NULL, 0,
					      edac_device_alloc_index());
	if (!edac_ctl)
		return -ENOMEM;

	edac_ctl->edac_check = cortex_arm64_edac_check;
	edac_ctl->dev = dev;
	edac_ctl->mod_name = dev_name(dev);
	edac_ctl->dev_name = dev_name(dev);
	edac_ctl->ctl_name = DRVNAME;
	dev_set_drvdata(dev, edac_ctl);

	rc = edac_device_add_device(edac_ctl);
	if (rc)
		goto out_dev;

	return 0;

out_dev:
	edac_device_free_ctl_info(edac_ctl);

	return rc;
}

static int cortex_arm64_edac_remove(struct platform_device *pdev)
{
	struct edac_device_ctl_info *edac_ctl = dev_get_drvdata(&pdev->dev);

	edac_device_del_device(edac_ctl->dev);
	edac_device_free_ctl_info(edac_ctl);

	return 0;
}

static const struct of_device_id cortex_arm64_edac_of_match[] = {
	{ .compatible = "arm,cortex-a53" },
	{ .compatible = "arm,cortex-a57" },
	{}
};
MODULE_DEVICE_TABLE(of, cortex_arm64_edac_of_match);

static struct platform_driver cortex_arm64_edac_driver = {
	.probe = cortex_arm64_edac_probe,
	.remove = cortex_arm64_edac_remove,
	.driver = {
		.name = DRVNAME,
	},
};

static int __init cortex_arm64_edac_driver_init(void)
{
	struct device_node *np;
	int cpu;
	struct platform_device *pdev;
	int err;

	for_each_possible_cpu(cpu) {
		np = of_get_cpu_node(cpu, NULL);

		if (!of_match_node(cortex_arm64_edac_of_match, np))
			continue;
		if (!of_property_read_bool(np, "edac-enabled"))
			continue;
		cpumask_set_cpu(cpu, &compat_mask);
	}

	if (cpumask_empty(&compat_mask))
		return 0;

	err = platform_driver_register(&cortex_arm64_edac_driver);
        if (err)
                return err;

	pdev = platform_device_register_simple(DRVNAME, -1, NULL, 0);
	if (IS_ERR(pdev)) {
		pr_err("failed to register cortex arm64 edac device\n");
		platform_driver_unregister(&cortex_arm64_edac_driver);
		return PTR_ERR(pdev);
	}

	return 0;
}

device_initcall(cortex_arm64_edac_driver_init);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Sascha Hauer <s.hauer@pengutronix.de>");
MODULE_DESCRIPTION("Cortex A57 and A53 L1 and L2 cache EDAC driver");
