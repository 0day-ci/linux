// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2021, Michael Srba

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_clock.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/reset.h>

/* AXI Halt Register Offsets */
#define AXI_HALTREQ_REG			0x0
#define AXI_HALTACK_REG			0x4
#define AXI_IDLE_REG			0x8

static const char *qcom_ssc_block_pd_names[] = {
	"ssc_cx",
	"ssc_mx"
};

struct qcom_ssc_block_bus_data {
	int num_pds;
	const char **pd_names;
	struct device *pds[ARRAY_SIZE(qcom_ssc_block_pd_names)];
	char __iomem *reg_mpm_sscaon_config0; // MPM - msm power manager; AON - always-on
	char __iomem *reg_mpm_sscaon_config1; // that's as much as we know about these
	struct regmap *halt_map;
	u32 ssc_axi_halt;
	struct clk *xo_clk;
	struct clk *aggre2_clk;
	struct clk *gcc_im_sleep_clk;
	struct clk *aggre2_north_clk;
	struct clk *ssc_xo_clk;
	struct clk *ssc_ahbs_clk;
	struct reset_control *ssc_bcr;
	struct reset_control *ssc_reset;
};

static void reg32_set_bits(char __iomem *reg, u32 value)
{
	u32 tmp = ioread32(reg);

	iowrite32(tmp | value, reg);
}

static void reg32_clear_bits(char __iomem *reg, u32 value)
{
	u32 tmp = ioread32(reg);

	iowrite32(tmp & (~value), reg);
}


static int qcom_ssc_block_bus_init(struct device *dev)
{
	int ret;

	struct qcom_ssc_block_bus_data *data = dev_get_drvdata(dev);

	clk_prepare_enable(data->xo_clk);
	clk_prepare_enable(data->aggre2_clk);

	clk_prepare_enable(data->gcc_im_sleep_clk);

	reg32_clear_bits(data->reg_mpm_sscaon_config0, BIT(4) | BIT(5));
	reg32_clear_bits(data->reg_mpm_sscaon_config1, BIT(31));

	clk_disable(data->aggre2_north_clk);

	ret = reset_control_deassert(data->ssc_reset);
	if (ret) {
		dev_err(dev, "error deasserting ssc_reset: %d\n", ret);
		return ret;
	}

	clk_prepare_enable(data->aggre2_north_clk);

	ret = reset_control_deassert(data->ssc_bcr);
	if (ret) {
		dev_err(dev, "error deasserting ssc_bcr: %d\n", ret);
		return ret;
	}

	regmap_write(data->halt_map, data->ssc_axi_halt + AXI_HALTREQ_REG, 0);

	clk_prepare_enable(data->ssc_xo_clk);

	clk_prepare_enable(data->ssc_ahbs_clk);

	return 0;
}

static int qcom_ssc_block_bus_deinit(struct device *dev)
{
	int ret;

	struct qcom_ssc_block_bus_data *data = dev_get_drvdata(dev);

	clk_disable(data->ssc_xo_clk);
	clk_disable(data->ssc_ahbs_clk);

	ret = reset_control_assert(data->ssc_bcr);
	if (ret) {
		dev_err(dev, "error asserting ssc_bcr: %d\n", ret);
		return ret;
	}

	regmap_write(data->halt_map, data->ssc_axi_halt + AXI_HALTREQ_REG, 1);

	reg32_set_bits(data->reg_mpm_sscaon_config1, BIT(31));
	reg32_set_bits(data->reg_mpm_sscaon_config0, BIT(4) | BIT(5));

	ret = reset_control_assert(data->ssc_reset);
	if (ret) {
		dev_err(dev, "error asserting ssc_reset: %d\n", ret);
		return ret;
	}

	clk_disable(data->gcc_im_sleep_clk);

	clk_disable(data->aggre2_north_clk);

	clk_disable(data->aggre2_clk);
	clk_disable(data->xo_clk);

	return 0;
}


static int qcom_ssc_block_bus_pds_attach(struct device *dev, struct device **pds,
					 const char **pd_names, size_t num_pds)
{
	int ret;
	int i;

	for (i = 0; i < num_pds; i++) {
		pds[i] = dev_pm_domain_attach_by_name(dev, pd_names[i]);
		if (IS_ERR_OR_NULL(pds[i])) {
			ret = PTR_ERR(pds[i]) ? : -ENODATA;
			goto unroll_attach;
		}
	}

	return num_pds;

unroll_attach:
	for (i--; i >= 0; i--)
		dev_pm_domain_detach(pds[i], false);

	return ret;
};

static void qcom_ssc_block_bus_pds_detach(struct device *dev, struct device **pds, size_t num_pds)
{
	int i;

	for (i = 0; i < num_pds; i++)
		dev_pm_domain_detach(pds[i], false);
}

static int qcom_ssc_block_bus_pds_enable(struct device **pds, size_t num_pds)
{
	int ret;
	int i;

	for (i = 0; i < num_pds; i++) {
		dev_pm_genpd_set_performance_state(pds[i], INT_MAX);
		ret = pm_runtime_get_sync(pds[i]);
		if (ret < 0)
			goto unroll_pd_votes;
	}

	return 0;

unroll_pd_votes:
	for (i--; i >= 0; i--) {
		dev_pm_genpd_set_performance_state(pds[i], 0);
		pm_runtime_put(pds[i]);
	}

	return ret;
};

static void qcom_ssc_block_bus_pds_disable(struct device **pds, size_t num_pds)
{
	int i;

	for (i = 0; i < num_pds; i++) {
		dev_pm_genpd_set_performance_state(pds[i], 0);
		pm_runtime_put(pds[i]);
	}
}

static int qcom_ssc_block_bus_probe(struct platform_device *pdev)
{
	struct qcom_ssc_block_bus_data *data;
	struct device_node *np = pdev->dev.of_node;
	struct of_phandle_args halt_args;
	struct resource *res;
	int ret;

	if (np)
		of_platform_populate(np, NULL, NULL, &pdev->dev);

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	platform_set_drvdata(pdev, data);

	data->pd_names = qcom_ssc_block_pd_names;
	data->num_pds = ARRAY_SIZE(qcom_ssc_block_pd_names);

	ret = qcom_ssc_block_bus_pds_attach(&pdev->dev, data->pds, data->pd_names, data->num_pds);
	if (ret < 0) {
		dev_err(&pdev->dev, "error when attaching power domains: %d\n", ret);
		return ret;
	}

	ret = qcom_ssc_block_bus_pds_enable(data->pds, data->num_pds);
	if (ret < 0) {
		dev_err(&pdev->dev, "error when enabling power domains: %d\n", ret);
		return ret;
	}

	// the meaning of the bits in these two registers is sadly not documented,
	// the set/clear operations are just copying what qcom does
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "mpm_sscaon_config0");
	data->reg_mpm_sscaon_config0 = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(data->reg_mpm_sscaon_config0)) {
		ret = PTR_ERR(data->reg_mpm_sscaon_config0);
		dev_err(&pdev->dev, "failed to ioremap mpm_sscaon_config0 (err: %d)\n", ret);
		return ret;
	}
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "mpm_sscaon_config0");
	data->reg_mpm_sscaon_config1 = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(data->reg_mpm_sscaon_config1)) {
		ret = PTR_ERR(data->reg_mpm_sscaon_config1);
		dev_err(&pdev->dev, "failed to ioremap mpm_sscaon_config1 (err: %d)\n", ret);
		return ret;
	}

	data->ssc_bcr = devm_reset_control_get_exclusive(&pdev->dev, "ssc_bcr");
	if (IS_ERR(data->ssc_bcr)) {
		ret = PTR_ERR(data->ssc_bcr);
		dev_err(&pdev->dev, "failed to acquire reset: scc_bcr (err: %d)\n", ret);
		return ret;
	}
	data->ssc_reset = devm_reset_control_get_exclusive(&pdev->dev, "ssc_reset");
	if (IS_ERR(data->ssc_reset)) {
		ret = PTR_ERR(data->ssc_reset);
		dev_err(&pdev->dev, "failed to acquire reset: ssc_reset: (err: %d)\n", ret);
		return ret;
	}

	data->xo_clk = devm_clk_get(&pdev->dev, "xo");
	if (IS_ERR(data->xo_clk)) {
		ret = PTR_ERR(data->xo_clk);
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Failed to get clock: xo (err: %d)\n", ret);
		return ret;
	}

	data->aggre2_clk = devm_clk_get(&pdev->dev, "aggre2");
	if (IS_ERR(data->aggre2_clk)) {
		ret = PTR_ERR(data->aggre2_clk);
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Failed to get clock: aggre2 (err: %d)\n", ret);
		return ret;
	}

	data->gcc_im_sleep_clk = devm_clk_get(&pdev->dev, "gcc_im_sleep");
	if (IS_ERR(data->gcc_im_sleep_clk)) {
		ret = PTR_ERR(data->gcc_im_sleep_clk);
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Failed to get clock: gcc_im_sleep (err: %d)\n", ret);
		return ret;
	}

	data->aggre2_north_clk = devm_clk_get(&pdev->dev, "aggre2_north");
	if (IS_ERR(data->aggre2_north_clk)) {
		ret = PTR_ERR(data->aggre2_north_clk);
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Failed to get clock: aggre2_north (err: %d)\n", ret);
		return ret;
	}

	data->ssc_xo_clk = devm_clk_get(&pdev->dev, "ssc_xo");
	if (IS_ERR(data->ssc_xo_clk)) {
		ret = PTR_ERR(data->ssc_xo_clk);
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Failed to get clock: ssc_xo (err: %d)\n", ret);
		return ret;
	}

	data->ssc_ahbs_clk = devm_clk_get(&pdev->dev, "ssc_ahbs");
	if (IS_ERR(data->ssc_ahbs_clk)) {
		ret = PTR_ERR(data->ssc_ahbs_clk);
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Failed to get clock: ssc_ahbs (err: %d)\n", ret);
		return ret;
	}

	ret = of_parse_phandle_with_fixed_args(pdev->dev.of_node, "qcom,halt-regs", 1, 0,
					       &halt_args);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to parse qcom,halt-regs\n");
		return -EINVAL;
	}

	data->halt_map = syscon_node_to_regmap(halt_args.np);
	of_node_put(halt_args.np);
	if (IS_ERR(data->halt_map))
		return PTR_ERR(data->halt_map);

	data->ssc_axi_halt = halt_args.args[0];

	qcom_ssc_block_bus_init(&pdev->dev);

	return 0;
}

static int qcom_ssc_block_bus_remove(struct platform_device *pdev)
{
	struct qcom_ssc_block_bus_data *data = platform_get_drvdata(pdev);

	qcom_ssc_block_bus_deinit(&pdev->dev);

	iounmap(data->reg_mpm_sscaon_config0);
	iounmap(data->reg_mpm_sscaon_config1);

	qcom_ssc_block_bus_pds_disable(data->pds, data->num_pds);
	qcom_ssc_block_bus_pds_detach(&pdev->dev, data->pds, data->num_pds);
	pm_runtime_disable(&pdev->dev);
	pm_clk_destroy(&pdev->dev);

	return 0;
}

static const struct of_device_id qcom_ssc_block_bus_of_match[] = {
	{ .compatible = "qcom,ssc-block-bus", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, qcom_ssc_block_bus_of_match);

static struct platform_driver qcom_ssc_block_bus_driver = {
	.probe = qcom_ssc_block_bus_probe,
	.remove = qcom_ssc_block_bus_remove,
	.driver = {
		.name = "qcom-ssc-block-bus",
		.of_match_table = qcom_ssc_block_bus_of_match,
	},
};

module_platform_driver(qcom_ssc_block_bus_driver);

MODULE_DESCRIPTION("A driver for handling the init sequence needed for accessing the SSC block on (some) qcom SoCs over AHB");
MODULE_AUTHOR("Michael Srba <Michael.Srba@seznam.cz>");
MODULE_LICENSE("GPL v2");
