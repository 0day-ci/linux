// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2021 NXP.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/pm_domain.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include "blk-ctl.h"

static inline struct imx_blk_ctl_domain *to_imx_blk_ctl_pd(struct generic_pm_domain *genpd)
{
	return container_of(genpd, struct imx_blk_ctl_domain, genpd);
}

static int imx_blk_ctl_enable_hsk(struct device *dev)
{
	struct imx_blk_ctl *blk_ctl = dev_get_drvdata(dev);
	const struct imx_blk_ctl_hw *hw = blk_ctl->dev_data->hw_hsk;
	struct regmap *regmap = blk_ctl->regmap;
	int ret;

	if (hw->flags & IMX_BLK_CTL_PD_RESET) {
		ret = regmap_update_bits(regmap, hw->rst_offset, hw->rst_mask, hw->rst_mask);
		if (ret)
			return ret;
	}

	ret = regmap_update_bits(regmap, hw->offset, hw->mask, hw->mask);

	/* Wait for handshake */
	udelay(5);

	return ret;
}

static int imx_blk_ctl_power_on(struct generic_pm_domain *domain)
{
	struct imx_blk_ctl_domain *pd = to_imx_blk_ctl_pd(domain);
	struct imx_blk_ctl *blk_ctl = pd->blk_ctl;
	struct regmap *regmap = blk_ctl->regmap;
	const struct imx_blk_ctl_hw *hw = &blk_ctl->dev_data->pds[pd->id];
	int ret;

	mutex_lock(&blk_ctl->lock);

	ret = clk_bulk_prepare_enable(blk_ctl->num_clks, blk_ctl->clks);
	if (ret) {
		mutex_unlock(&blk_ctl->lock);
		return ret;
	}

	if (hw->flags & IMX_BLK_CTL_PD_HANDSHAKE) {
		ret = imx_blk_ctl_enable_hsk(blk_ctl->dev);
		if (ret)
			dev_err(blk_ctl->dev, "Handshake failed when power on\n");

		/* Expected, handshake already handle reset*/
		goto disable_clk;
	}

	if (hw->flags & IMX_BLK_CTL_PD_RESET) {
		ret = regmap_clear_bits(regmap, hw->rst_offset, hw->rst_mask);
		if (ret)
			goto disable_clk;

		/* Wait for reset propagate */
		udelay(5);

		ret = regmap_update_bits(regmap, hw->rst_offset, hw->rst_mask, hw->rst_mask);
		if (ret)
			goto disable_clk;
	}

	ret = regmap_update_bits(regmap, hw->offset, hw->mask, hw->mask);

disable_clk:
	clk_bulk_disable_unprepare(blk_ctl->num_clks, blk_ctl->clks);

	mutex_unlock(&blk_ctl->lock);

	return ret;
}

static int imx_blk_ctl_power_off(struct generic_pm_domain *domain)
{
	struct imx_blk_ctl_domain *pd = to_imx_blk_ctl_pd(domain);
	struct imx_blk_ctl *blk_ctl = pd->blk_ctl;
	struct regmap *regmap = blk_ctl->regmap;
	const struct imx_blk_ctl_hw *hw = &blk_ctl->dev_data->pds[pd->id];
	int ret = 0;

	mutex_lock(&blk_ctl->lock);

	ret = clk_bulk_prepare_enable(blk_ctl->num_clks, blk_ctl->clks);
	if (ret) {
		mutex_unlock(&blk_ctl->lock);
		return ret;
	}

	if (!(hw->flags & IMX_BLK_CTL_PD_HANDSHAKE)) {
		ret = regmap_clear_bits(regmap, hw->offset, hw->mask);
		if (ret)
			goto disable_clk;

		if (hw->flags & IMX_BLK_CTL_PD_RESET) {
			ret = regmap_clear_bits(regmap, hw->rst_offset, hw->rst_mask);
			if (ret)
				goto disable_clk;
		}
	} else {
		ret = imx_blk_ctl_enable_hsk(blk_ctl->dev);
		if (ret)
			dev_err(blk_ctl->dev, "Handshake failed when power off\n");
	}

disable_clk:
	clk_bulk_disable_unprepare(blk_ctl->num_clks, blk_ctl->clks);

	mutex_unlock(&blk_ctl->lock);

	return ret;
}

static int imx_blk_ctl_probe(struct platform_device *pdev)
{
	struct imx_blk_ctl_domain *domain = pdev->dev.platform_data;
	struct imx_blk_ctl *blk_ctl = domain->blk_ctl;
	struct generic_pm_domain *parent_genpd;
	struct device *dev = &pdev->dev;
	struct device *active_pd;
	int ret;

	pdev->dev.of_node = blk_ctl->dev->of_node;

	if (domain->hw->active_pd_name) {
		active_pd = dev_pm_domain_attach_by_name(dev, domain->hw->active_pd_name);
		if (IS_ERR_OR_NULL(active_pd)) {
			ret = PTR_ERR(active_pd) ? : -ENODATA;
			pdev->dev.of_node = NULL;
			return ret;
		}

		domain->active_pd = active_pd;
	} else {
		if (!blk_ctl->bus_domain) {
			pdev->dev.of_node = NULL;
			return -EPROBE_DEFER;
		}
	}

	if (domain->hw->active_pd_name)
		parent_genpd = pd_to_genpd(active_pd->pm_domain);
	else
		parent_genpd = blk_ctl->bus_domain;

	if (pm_genpd_add_subdomain(parent_genpd, &domain->genpd)) {
		dev_warn(dev, "failed to add subdomain: %s\n", domain->genpd.name);
	} else {
		mutex_lock(&blk_ctl->lock);
		domain->hooked = true;
		mutex_unlock(&blk_ctl->lock);
	}

	return 0;
}

static int imx_blk_ctl_remove(struct platform_device *pdev)
{
	struct imx_blk_ctl_domain *domain = pdev->dev.platform_data;
	struct imx_blk_ctl *blk_ctl = domain->blk_ctl;
	struct generic_pm_domain *parent_genpd;
	struct device *active_pd;

	if (domain->hw->active_pd_name)
		parent_genpd = pd_to_genpd(active_pd->pm_domain);
	else
		parent_genpd = blk_ctl->bus_domain;

	pm_genpd_remove_subdomain(parent_genpd, &domain->genpd);

	mutex_lock(&blk_ctl->lock);
	domain->hooked = false;
	mutex_unlock(&blk_ctl->lock);

	if (domain->hw->active_pd_name)
		dev_pm_domain_detach(domain->active_pd, false);

	return 0;
}

static const struct platform_device_id imx_blk_ctl_id[] = {
	{ "imx-vpumix-blk-ctl", },
	{ "imx-dispmix-blk-ctl", },
	{ },
};

static struct platform_driver imx_blk_ctl_driver = {
	.driver = {
		.name = "imx-blk-ctl",
	},
	.probe    = imx_blk_ctl_probe,
	.remove   = imx_blk_ctl_remove,
	.id_table = imx_blk_ctl_id,
};
builtin_platform_driver(imx_blk_ctl_driver)

static struct generic_pm_domain *imx_blk_ctl_genpd_xlate(struct of_phandle_args *genpdspec,
							 void *data)
{
	struct genpd_onecell_data *genpd_data = data;
	unsigned int idx = genpdspec->args[0];
	struct imx_blk_ctl_domain *domain;
	struct generic_pm_domain *genpd = ERR_PTR(-EPROBE_DEFER);

	if (genpdspec->args_count != 1)
		return ERR_PTR(-EINVAL);

	if (idx >= genpd_data->num_domains)
		return ERR_PTR(-EINVAL);

	if (!genpd_data->domains[idx])
		return ERR_PTR(-ENOENT);

	domain = to_imx_blk_ctl_pd(genpd_data->domains[idx]);

	mutex_lock(&domain->blk_ctl->lock);
	if (domain->hooked)
		genpd = genpd_data->domains[idx];
	mutex_unlock(&domain->blk_ctl->lock);

	return genpd;
}

int imx_blk_ctl_register(struct device *dev)
{
	struct imx_blk_ctl *blk_ctl = dev_get_drvdata(dev);
	const struct imx_blk_ctl_dev_data *dev_data = blk_ctl->dev_data;
	int num = dev_data->pds_num;
	struct imx_blk_ctl_domain *domain;
	struct generic_pm_domain *genpd;
	struct platform_device *pd_pdev;
	int domain_index;
	int i, ret;

	blk_ctl->onecell_data.num_domains = num;
	blk_ctl->onecell_data.xlate = imx_blk_ctl_genpd_xlate;
	blk_ctl->onecell_data.domains = devm_kcalloc(dev, num, sizeof(struct generic_pm_domain *),
						     GFP_KERNEL);
	if (!blk_ctl->onecell_data.domains)
		return -ENOMEM;

	for (i = 0; i < num; i++) {
		domain_index = dev_data->pds[i].id;
		if (domain_index >= num) {
			dev_warn(dev, "Domain index %d is out of bounds\n", domain_index);
			continue;
		}

		domain = devm_kzalloc(dev, sizeof(struct imx_blk_ctl_domain), GFP_KERNEL);
		if (!domain)
			goto error;

		pd_pdev = platform_device_alloc(dev_data->name, domain_index);
		if (!pd_pdev) {
			dev_err(dev, "Failed to allocate platform device\n");
			goto error;
		}

		pd_pdev->dev.platform_data = domain;

		domain->blk_ctl = blk_ctl;
		domain->hw = &dev_data->pds[i];
		domain->id = domain_index;
		domain->genpd.name = dev_data->pds[i].name;
		domain->genpd.power_off = imx_blk_ctl_power_off;
		domain->genpd.power_on = imx_blk_ctl_power_on;
		domain->dev = &pd_pdev->dev;
		domain->hooked = false;

		ret = pm_genpd_init(&domain->genpd, NULL, true);
		pd_pdev->dev.parent = dev;

		if (domain->hw->flags & IMX_BLK_CTL_PD_HANDSHAKE)
			blk_ctl->bus_domain = &domain->genpd;

		ret = platform_device_add(pd_pdev);
		if (ret) {
			platform_device_put(pd_pdev);
			goto error;
		}
		blk_ctl->onecell_data.domains[i] = &domain->genpd;
	}

	return of_genpd_add_provider_onecell(dev->of_node, &blk_ctl->onecell_data);

error:
	for (; i >= 0; i--) {
		genpd = blk_ctl->onecell_data.domains[i];
		if (!genpd)
			continue;
		domain = to_imx_blk_ctl_pd(genpd);
		if (domain->dev)
			platform_device_put(to_platform_device(domain->dev));
	}
	return ret;
}
EXPORT_SYMBOL_GPL(imx_blk_ctl_register);

const struct dev_pm_ops imx_blk_ctl_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend, pm_runtime_force_resume)
};
EXPORT_SYMBOL_GPL(imx_blk_ctl_pm_ops);
