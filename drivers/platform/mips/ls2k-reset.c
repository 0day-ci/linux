// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 2021, Qing Zhang <zhangqing@loongson.cn>
 *  Loongson-2K1000 reset support
 */

#include <linux/of_address.h>
#include <linux/pm.h>
#include <asm/reboot.h>

static char *pm_reg_name[] = {"pm1_sts", "pm1_cnt", "rst_cnt"};

static void __iomem *get_reg_byname(struct device_node *node, const char *name)
{
	int index = of_property_match_string(node, "reg-names", name);

	if (index < 0)
		return NULL;

	return of_iomap(node, index);
}

static void ls2k_restart(char *command)
{
	writel(0x1, (void *)pm_reg_name[2]);
}

static void ls2k_poweroff(void)
{
	/* Clear */
	writel((readl((void *)pm_reg_name[0]) & 0xffffffff), (void *)pm_reg_name[0]);
	/* Sleep Enable | Soft Off*/
	writel(GENMASK(12, 10)|BIT(13), (void *)pm_reg_name[1]);
}

static int ls2k_reset_init(void)
{
	struct device_node *np;
	int i;

	np = of_find_node_by_type(NULL, "power management");
	if (!np) {
		pr_info("Failed to get PM node\n");
		return -ENODEV;
	}

	for (i = 0; i < sizeof(pm_reg_name)/sizeof(char *); i++) {
		pm_reg_name[i] = get_reg_byname(np, pm_reg_name[i]);
		if (!pm_reg_name[i])
			iounmap(pm_reg_name[i]);
	}

	_machine_restart = ls2k_restart;
	pm_power_off = ls2k_poweroff;

	of_node_put(np);
	return 0;
}

arch_initcall(ls2k_reset_init);
