// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015 Pengutronix, Steffen Trumtrar <kernel@pengutronix.de>
 * Copyright (c) 2017 Pengutronix, Oleksij Rempel <kernel@pengutronix.de>
 */

#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/of_device.h>
#include <linux/regmap.h>

#define IMX6Q_SNVS_HPLR		0x00
#define IMX6Q_SNVS_LPLR		0x34
#define IMX6Q_SNVS_LPGPR	0x68

#define IMX7D_SNVS_HPLR		0x00
#define IMX7D_SNVS_LPLR		0x34
#define IMX7D_SNVS_LPGPR	0x90

#define IMX_GPR_SL		BIT(5)
#define IMX_GPR_HL		BIT(5)

#define REGMAP_FIELD_SIZE 16
#define REGMAP_FIELDS_PER_REG 2

struct snvs_lpgpr_cfg {
	int offset;
	int offset_hplr;
	int offset_lplr;
	int size;
};

struct snvs_lpgpr_priv {
	struct device_d			*dev;
	struct regmap			*regmap;
	struct nvmem_config		cfg;
	const struct snvs_lpgpr_cfg	*dcfg;
	struct regmap_field **reg_fields;
};

static const struct snvs_lpgpr_cfg snvs_lpgpr_cfg_imx6q = {
	.offset		= IMX6Q_SNVS_LPGPR,
	.offset_hplr	= IMX6Q_SNVS_HPLR,
	.offset_lplr	= IMX6Q_SNVS_LPLR,
	.size		= 4,
};

static const struct snvs_lpgpr_cfg snvs_lpgpr_cfg_imx7d = {
	.offset		= IMX7D_SNVS_LPGPR,
	.offset_hplr	= IMX7D_SNVS_HPLR,
	.offset_lplr	= IMX7D_SNVS_LPLR,
	.size		= 16,
};

static int snvs_lpgpr_write(void *context, unsigned int offset, void *val,
			    size_t bytes)
{
	struct snvs_lpgpr_priv *priv = context;
	const struct snvs_lpgpr_cfg *dcfg = priv->dcfg;
	unsigned int lock_reg;
	int ret;
	u32 regval;
	unsigned int field_id;

	if (offset + bytes > dcfg->size)
		return -EINVAL;

	ret = regmap_read(priv->regmap, dcfg->offset_hplr, &lock_reg);
	if (ret < 0)
		return ret;

	if (lock_reg & IMX_GPR_SL)
		return -EPERM;

	ret = regmap_read(priv->regmap, dcfg->offset_lplr, &lock_reg);
	if (ret < 0)
		return ret;

	if (lock_reg & IMX_GPR_HL)
		return -EPERM;

	if (bytes == (REGMAP_FIELD_SIZE >> 3)) {
		regval = *(u16 *)(val);
		field_id = offset / REGMAP_FIELDS_PER_REG;
		ret = regmap_field_write(priv->reg_fields[field_id], regval);
	} else {
		ret = regmap_bulk_write(priv->regmap, dcfg->offset + offset,
					val, bytes / priv->cfg.stride);
	}

	return ret;
}

static int snvs_lpgpr_read(void *context, unsigned int offset, void *val,
			   size_t bytes)
{
	struct snvs_lpgpr_priv *priv = context;
	const struct snvs_lpgpr_cfg *dcfg = priv->dcfg;
	int ret;
	u32 regval;
	unsigned int field_id;

	if (offset + bytes > dcfg->size)
		return -EINVAL;

	if (bytes == (REGMAP_FIELD_SIZE >> 3)) {
		field_id = offset / REGMAP_FIELDS_PER_REG;
		ret = regmap_field_read(priv->reg_fields[field_id], &regval);
		if (ret)
			return ret;

		*(u16 *)(val) = regval;
	} else {
		ret = regmap_bulk_read(priv->regmap, dcfg->offset + offset, val,
				       bytes / priv->cfg.stride);
		if (ret)
			return ret;
	}
	return 0;
}

static int snvs_lpgpr_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct device_node *syscon_node;
	struct snvs_lpgpr_priv *priv;
	struct nvmem_config *cfg;
	struct nvmem_device *nvmem;
	const struct snvs_lpgpr_cfg *dcfg;
	int i;
	int fields_count;

	if (!node)
		return -ENOENT;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	dcfg = of_device_get_match_data(dev);
	if (!dcfg)
		return -EINVAL;

	syscon_node = of_get_parent(node);
	if (!syscon_node)
		return -ENODEV;

	priv->regmap = syscon_node_to_regmap(syscon_node);
	of_node_put(syscon_node);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	priv->dcfg = dcfg;

	cfg = &priv->cfg;
	cfg->priv = priv;
	cfg->name = dev_name(dev);
	cfg->dev = dev;
	cfg->stride = 2;
	cfg->word_size = 2;
	cfg->size = dcfg->size;
	cfg->owner = THIS_MODULE;
	cfg->reg_read  = snvs_lpgpr_read;
	cfg->reg_write = snvs_lpgpr_write;

	fields_count = priv->dcfg->size / priv->cfg.stride;
	priv->reg_fields = devm_kzalloc(
		dev, sizeof(struct regmap_field *) * fields_count, GFP_KERNEL);
	if (!priv->reg_fields)
		return -ENOMEM;

	for (i = 0; i < fields_count; i++) {
		size_t field_start = i * REGMAP_FIELD_SIZE;
		size_t field_end = field_start + REGMAP_FIELD_SIZE - 1;
		const struct reg_field field =
			REG_FIELD(dcfg->offset, field_start, field_end);

		priv->reg_fields[i] =
			devm_regmap_field_alloc(dev, priv->regmap, field);
		if (IS_ERR(priv->reg_fields[i]))
			return PTR_ERR(priv->reg_fields[i]);
	}

	nvmem = devm_nvmem_register(dev, cfg);

	return PTR_ERR_OR_ZERO(nvmem);
}

static const struct of_device_id snvs_lpgpr_dt_ids[] = {
	{ .compatible = "fsl,imx6q-snvs-lpgpr", .data = &snvs_lpgpr_cfg_imx6q },
	{ .compatible = "fsl,imx6ul-snvs-lpgpr",
	  .data = &snvs_lpgpr_cfg_imx6q },
	{ .compatible = "fsl,imx7d-snvs-lpgpr",	.data = &snvs_lpgpr_cfg_imx7d },
	{ },
};
MODULE_DEVICE_TABLE(of, snvs_lpgpr_dt_ids);

static struct platform_driver snvs_lpgpr_driver = {
	.probe	= snvs_lpgpr_probe,
	.driver = {
		.name	= "snvs_lpgpr",
		.of_match_table = snvs_lpgpr_dt_ids,
	},
};
module_platform_driver(snvs_lpgpr_driver);

MODULE_AUTHOR("Oleksij Rempel <o.rempel@pengutronix.de>");
MODULE_DESCRIPTION("Low Power General Purpose Register in i.MX6 and i.MX7 Secure Non-Volatile Storage");
MODULE_LICENSE("GPL v2");
