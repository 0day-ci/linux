// SPDX-License-Identifier: GPL-2.0-only OR MIT
/*
 * Apple SoC MCC memory controller performance control driver
 *
 * Copyright The Asahi Linux Contributors
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_opp.h>

#define APPLE_MCC_PERF_CONFIG1  0xdc4
#define APPLE_MCC_PERF_CONFIG2  0xdbc
#define APPLE_MCC_CHANNEL(x)	((x) * 0x40000)

struct apple_mcc {
	struct device *dev;
	struct generic_pm_domain genpd;
	void __iomem *reg_base;
	u32 num_channels;
};

#define to_apple_mcc(_genpd) container_of(_genpd, struct apple_mcc, genpd)

static int apple_mcc_set_performance_state(struct generic_pm_domain *genpd, unsigned int state)
{
	struct apple_mcc *mcc = to_apple_mcc(genpd);
	struct dev_pm_opp *opp;
	struct device_node *np;
	u32 perf_config[2];
	unsigned int i;

	dev_dbg(mcc->dev, "switching to perf state %d\n", state);

	opp = dev_pm_opp_find_level_exact(&mcc->genpd.dev, state);
	if (IS_ERR(opp))
		return PTR_ERR(opp);

	np = dev_pm_opp_get_of_node(opp);
	if (of_property_read_u32_array(np, "apple,memory-perf-config",
		perf_config, ARRAY_SIZE(perf_config))) {
		dev_err(mcc->dev, "missing apple,memory-perf-config property");
		of_node_put(np);
		return -EINVAL;
	}
	of_node_put(np);

	for (i = 0; i < mcc->num_channels; i++) {
		writel_relaxed(perf_config[0],
			       mcc->reg_base + APPLE_MCC_CHANNEL(i) + APPLE_MCC_PERF_CONFIG1);
		writel_relaxed(perf_config[1],
			       mcc->reg_base + APPLE_MCC_CHANNEL(i) + APPLE_MCC_PERF_CONFIG2);
	}

	return 0;
}

static unsigned int apple_mcc_opp_to_performance_state(struct generic_pm_domain *genpd,
						       struct dev_pm_opp *opp)
{
	return dev_pm_opp_get_level(opp);
}

static int apple_mcc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct apple_mcc *mcc;
	int ret;

	mcc = devm_kzalloc(dev, sizeof(*mcc), GFP_KERNEL);
	if (!mcc)
		return -ENOMEM;

	mcc->reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mcc->reg_base))
		return PTR_ERR(mcc->reg_base);

	if (of_property_read_u32(node, "apple,num-channels", &mcc->num_channels)) {
		dev_err(dev, "missing apple,num-channels property\n");
		return -ENOENT;
	}

	mcc->dev = dev;
	mcc->genpd.name = "apple-mcc-perf";
	mcc->genpd.opp_to_performance_state = apple_mcc_opp_to_performance_state;
	mcc->genpd.set_performance_state = apple_mcc_set_performance_state;

	ret = pm_genpd_init(&mcc->genpd, NULL, false);
	if (ret < 0) {
		dev_err(dev, "pm_genpd_init failed\n");
		return ret;
	}

	ret = of_genpd_add_provider_simple_noclk(node, &mcc->genpd);
	if (ret < 0) {
		dev_err(dev, "of_genpd_add_provider_simple failed\n");
		return ret;
	}

	dev_info(dev, "Apple MCC performance driver initialized\n");

	return 0;
}

static const struct of_device_id apple_mcc_of_match[] = {
	{ .compatible = "apple,mcc" },
	{}
};

MODULE_DEVICE_TABLE(of, apple_mcc_of_match);

static struct platform_driver apple_mcc_driver = {
	.probe = apple_mcc_probe,
	.driver = {
		.name = "apple-mcc",
		.of_match_table = apple_mcc_of_match,
	},
};

MODULE_AUTHOR("Hector Martin <marcan@marcan.st>");
MODULE_DESCRIPTION("MCC memory controller performance tuning driver for Apple SoCs");
MODULE_LICENSE("GPL v2");

module_platform_driver(apple_mcc_driver);
