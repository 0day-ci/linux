// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014, The Linux Foundation. All rights reserved.
 */

#include <linux/device.h>
#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spmi.h>
#include <linux/regmap.h>
#include <linux/of_platform.h>
#include <soc/qcom/qcom-pmic.h>

#define PMIC_REV2		0x101
#define PMIC_REV3		0x102
#define PMIC_REV4		0x103
#define PMIC_TYPE		0x104
#define PMIC_SUBTYPE		0x105
#define PMIC_FAB_ID		0x1f2
#define PMIC_TYPE_VALUE		0x51

static const struct of_device_id pmic_spmi_id_table[] = {
	{ .compatible = "qcom,pm660",     .data = (void *)PM660_SUBTYPE },
	{ .compatible = "qcom,pm660l",    .data = (void *)PM660L_SUBTYPE },
	{ .compatible = "qcom,pm8004",    .data = (void *)PM8004_SUBTYPE },
	{ .compatible = "qcom,pm8005",    .data = (void *)PM8005_SUBTYPE },
	{ .compatible = "qcom,pm8019",    .data = (void *)PM8019_SUBTYPE },
	{ .compatible = "qcom,pm8028",    .data = (void *)PM8028_SUBTYPE },
	{ .compatible = "qcom,pm8110",    .data = (void *)PM8110_SUBTYPE },
	{ .compatible = "qcom,pm8150",    .data = (void *)PM8150_SUBTYPE },
	{ .compatible = "qcom,pm8150b",   .data = (void *)PM8150B_SUBTYPE },
	{ .compatible = "qcom,pm8150c",   .data = (void *)PM8150C_SUBTYPE },
	{ .compatible = "qcom,pm8150l",   .data = (void *)PM8150L_SUBTYPE },
	{ .compatible = "qcom,pm8226",    .data = (void *)PM8226_SUBTYPE },
	{ .compatible = "qcom,pm8841",    .data = (void *)PM8841_SUBTYPE },
	{ .compatible = "qcom,pm8901",    .data = (void *)PM8901_SUBTYPE },
	{ .compatible = "qcom,pm8909",    .data = (void *)PM8909_SUBTYPE },
	{ .compatible = "qcom,pm8916",    .data = (void *)PM8916_SUBTYPE },
	{ .compatible = "qcom,pm8941",    .data = (void *)PM8941_SUBTYPE },
	{ .compatible = "qcom,pm8950",    .data = (void *)PM8950_SUBTYPE },
	{ .compatible = "qcom,pm8994",    .data = (void *)PM8994_SUBTYPE },
	{ .compatible = "qcom,pm8998",    .data = (void *)PM8998_SUBTYPE },
	{ .compatible = "qcom,pma8084",   .data = (void *)PMA8084_SUBTYPE },
	{ .compatible = "qcom,pmd9635",   .data = (void *)PMD9635_SUBTYPE },
	{ .compatible = "qcom,pmi8950",   .data = (void *)PMI8950_SUBTYPE },
	{ .compatible = "qcom,pmi8962",   .data = (void *)PMI8962_SUBTYPE },
	{ .compatible = "qcom,pmi8994",   .data = (void *)PMI8994_SUBTYPE },
	{ .compatible = "qcom,pmi8998",   .data = (void *)PMI8998_SUBTYPE },
	{ .compatible = "qcom,pmk8002",   .data = (void *)PMK8002_SUBTYPE },
	{ .compatible = "qcom,smb2351",   .data = (void *)SMB2351_SUBTYPE },
	{ .compatible = "qcom,spmi-pmic", .data = (void *)COMMON_SUBTYPE },
	{ }
};

static int pmic_spmi_load_revid(struct regmap *map, struct device *dev,
				 struct qcom_spmi_pmic *pmic)
{
	int ret, i;

	ret = regmap_read(map, PMIC_TYPE, &pmic->type);
	if (ret < 0)
		return ret;

	if (pmic->type != PMIC_TYPE_VALUE)
		return ret;

	ret = regmap_read(map, PMIC_SUBTYPE, &pmic->subtype);
	if (ret < 0)
		return ret;

	for (i = 0; i < ARRAY_SIZE(pmic_spmi_id_table); i++) {
		if (pmic->subtype == (unsigned long)pmic_spmi_id_table[i].data)
			break;
	}

	if (i != ARRAY_SIZE(pmic_spmi_id_table))
		pmic->name = devm_kstrdup_const(dev, pmic_spmi_id_table[i].compatible, GFP_KERNEL);

	ret = regmap_read(map, PMIC_REV2, &pmic->rev2);
	if (ret < 0)
		return ret;

	ret = regmap_read(map, PMIC_REV3, &pmic->minor);
	if (ret < 0)
		return ret;

	ret = regmap_read(map, PMIC_REV4, &pmic->major);
	if (ret < 0)
		return ret;

	if (pmic->subtype == PMI8998_SUBTYPE || pmic->subtype == PM660_SUBTYPE) {
		ret = regmap_read(map, PMIC_FAB_ID, &pmic->fab_id);
		if (ret < 0)
			return ret;
	}

	/*
	 * In early versions of PM8941 and PM8226, the major revision number
	 * started incrementing from 0 (eg 0 = v1.0, 1 = v2.0).
	 * Increment the major revision number here if the chip is an early
	 * version of PM8941 or PM8226.
	 */
	if ((pmic->subtype == PM8941_SUBTYPE || pmic->subtype == PM8226_SUBTYPE) &&
	    pmic->major < 0x02)
		pmic->major++;

	if (pmic->subtype == PM8110_SUBTYPE)
		pmic->minor = pmic->rev2;

	return 0;
}

static const struct regmap_config spmi_regmap_config = {
	.reg_bits	= 16,
	.val_bits	= 8,
	.max_register	= 0xffff,
	.fast_io	= true,
};

static int pmic_spmi_probe(struct spmi_device *sdev)
{
	struct regmap *regmap;
	struct qcom_spmi_pmic *pmic;

	regmap = devm_regmap_init_spmi_ext(sdev, &spmi_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	pmic = devm_kzalloc(&sdev->dev, sizeof(*pmic), GFP_KERNEL);
	if (!pmic)
		return -ENOMEM;

	/* Only the first slave id for a PMIC contains this information */
	if (sdev->usid % 2 == 0) {
		pmic_spmi_load_revid(regmap, &sdev->dev, pmic);
		spmi_device_set_drvdata(sdev, pmic);
		qcom_pmic_print_info(&sdev->dev, pmic);
	}

	return devm_of_platform_populate(&sdev->dev);
}

static void pmic_spmi_remove(struct spmi_device *sdev)
{
	struct qcom_spmi_pmic *pmic = spmi_device_get_drvdata(sdev);

	kfree(pmic->name);
}

MODULE_DEVICE_TABLE(of, pmic_spmi_id_table);

static struct spmi_driver pmic_spmi_driver = {
	.probe = pmic_spmi_probe,
	.remove = pmic_spmi_remove,
	.driver = {
		.name = "pmic-spmi",
		.of_match_table = pmic_spmi_id_table,
	},
};
module_spmi_driver(pmic_spmi_driver);

MODULE_DESCRIPTION("Qualcomm SPMI PMIC driver");
MODULE_ALIAS("spmi:spmi-pmic");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Josh Cartwright <joshc@codeaurora.org>");
MODULE_AUTHOR("Stanimir Varbanov <svarbanov@mm-sol.com>");
