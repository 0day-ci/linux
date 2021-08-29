// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, Linaro Ltd.
 *
 * Author: Dmitry Baryshkov <dmitry.baryshkov@linaro.org>
 *
 * Power Sequencer for Qualcomm WiFi + BT SoCs
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/mod_devicetable.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/pwrseq/driver.h>
#include <linux/pwrseq/fallback.h>
#include <linux/regulator/consumer.h>

/* susclk rate */
#define SUSCLK_RATE_32KHZ	32768

/*
 * Voltage regulator information required for configuring the
 * QCA WiFi+Bluetooth chipset
 */
struct qca_vreg {
	const char *name;
	unsigned int load_uA;
};

struct qca_device_data {
	bool has_enable_gpios;

	/*
	 * VDDIO has to be enabled before the rest of regulators, so we treat
	 * it separately
	 */
	struct qca_vreg vddio;

	size_t num_vregs;
	struct qca_vreg vregs[];
};

struct pwrseq_qca_common {
	struct gpio_desc *sw_ctrl;
	struct clk *susclk;

	/*
	 * Again vddio is separate so that it can be enabled before enabling
	 * other regulators.
	 */
	struct regulator *vddio;
	int num_vregs;
	struct regulator_bulk_data vregs[];
};

struct pwrseq_qca_one {
	struct pwrseq_qca_common *common;
	struct gpio_desc *enable;
};

#define PWRSEQ_QCA_WIFI 0
#define PWRSEQ_QCA_BT 1

#define PWRSEQ_QCA_MAX 2

struct pwrseq_qca {
	struct pwrseq_qca_one pwrseq_qcas[PWRSEQ_QCA_MAX];
	struct pwrseq_qca_common common;
};

static int pwrseq_qca_pre_power_on(struct pwrseq *pwrseq)
{
	struct pwrseq_qca_one *qca_one = pwrseq_get_data(pwrseq);

	if (qca_one->enable) {
		gpiod_set_value_cansleep(qca_one->enable, 0);
		msleep(50);
	}

	return 0;
}

static int pwrseq_qca_power_on(struct pwrseq *pwrseq)
{
	struct pwrseq_qca_one *qca_one = pwrseq_get_data(pwrseq);
	int ret;

	if (qca_one->common->vddio) {
		ret = regulator_enable(qca_one->common->vddio);
		if (ret)
			return ret;
	}

	ret = regulator_bulk_enable(qca_one->common->num_vregs, qca_one->common->vregs);
	if (ret)
		goto err_bulk;

	ret = clk_prepare_enable(qca_one->common->susclk);
	if (ret)
		goto err_clk;

	if (qca_one->enable) {
		gpiod_set_value_cansleep(qca_one->enable, 1);
		msleep(150);
	}

	if (qca_one->common->sw_ctrl) {
		bool sw_ctrl_state = gpiod_get_value_cansleep(qca_one->common->sw_ctrl);
		dev_dbg(&pwrseq->dev, "SW_CTRL is %d", sw_ctrl_state);
	}

	return 0;

err_clk:
	regulator_bulk_disable(qca_one->common->num_vregs, qca_one->common->vregs);
err_bulk:
	regulator_disable(qca_one->common->vddio);

	return ret;
}

static void pwrseq_qca_power_off(struct pwrseq *pwrseq)
{
	struct pwrseq_qca_one *qca_one = pwrseq_get_data(pwrseq);

	if (qca_one->enable) {
		gpiod_set_value_cansleep(qca_one->enable, 0);
		msleep(50);
	}

	clk_disable_unprepare(qca_one->common->susclk);

	regulator_bulk_disable(qca_one->common->num_vregs, qca_one->common->vregs);
	regulator_disable(qca_one->common->vddio);

	if (qca_one->common->sw_ctrl) {
		bool sw_ctrl_state = gpiod_get_value_cansleep(qca_one->common->sw_ctrl);
		dev_dbg(&pwrseq->dev, "SW_CTRL is %d", sw_ctrl_state);
	}
}

static const struct pwrseq_ops pwrseq_qca_ops = {
	.pre_power_on = pwrseq_qca_pre_power_on,
	.power_on = pwrseq_qca_power_on,
	.power_off = pwrseq_qca_power_off,
};

static int pwrseq_qca_common_init(struct device *dev, struct pwrseq_qca_common *qca_common,
		const struct qca_device_data *data)
{
	int ret, i;

	if (data->vddio.name) {
		qca_common->vddio = devm_regulator_get(dev, data->vddio.name);
		if (IS_ERR(qca_common->vddio))
			return PTR_ERR(qca_common->vddio);

		ret = regulator_set_load(qca_common->vddio, data->vddio.load_uA);
		if (ret)
			return ret;
	}

	qca_common->num_vregs = data->num_vregs;

	for (i = 0; i < qca_common->num_vregs; i++)
		qca_common->vregs[i].supply = data->vregs[i].name;

	ret = devm_regulator_bulk_get(dev, qca_common->num_vregs, qca_common->vregs);
	if (ret)
		return ret;

	for (i = 0; i < qca_common->num_vregs; i++) {
		if (!data->vregs[i].load_uA)
			continue;

		ret = regulator_set_load(qca_common->vregs[i].consumer, data->vregs[i].load_uA);
		if (ret)
			return ret;
	}

	qca_common->susclk = devm_clk_get_optional(dev, NULL);
	if (IS_ERR(qca_common->susclk)) {
		dev_err(dev, "failed to acquire clk\n");
		return PTR_ERR(qca_common->susclk);
	}

	qca_common->sw_ctrl = devm_gpiod_get_optional(dev, "swctrl", GPIOD_IN);
	if (IS_ERR(qca_common->sw_ctrl)) {
		return dev_err_probe(dev, PTR_ERR(qca_common->sw_ctrl),
				"failed to acquire SW_CTRL gpio\n");
	} else if (!qca_common->sw_ctrl)
		dev_info(dev, "No SW_CTRL gpio\n");

	return 0;
}

static void pwrseq_qca_unprepare_susclk(void *data)
{
	struct pwrseq_qca_common *qca_common = data;

	clk_disable_unprepare(qca_common->susclk);
}

static const struct qca_device_data qca_soc_data_default = {
	.num_vregs = 0,
	.has_enable_gpios = true,
};

static int pwrseq_qca_probe(struct platform_device *pdev)
{
	struct pwrseq_qca *pwrseq_qca;
	struct pwrseq *pwrseq;
	struct pwrseq_provider *provider;
	struct device *dev = &pdev->dev;
	struct pwrseq_onecell_data *onecell;
	const struct qca_device_data *data;
	int ret, i;

	data = device_get_match_data(dev);
	if (!data)
		data = &qca_soc_data_default;

	pwrseq_qca = devm_kzalloc(dev, struct_size(pwrseq_qca, common.vregs, data->num_vregs), GFP_KERNEL);
	if (!pwrseq_qca)
		return -ENOMEM;

	onecell = devm_kzalloc(dev, struct_size(onecell, pwrseqs, PWRSEQ_QCA_MAX), GFP_KERNEL);
	if (!onecell)
		return -ENOMEM;

	ret = pwrseq_qca_common_init(dev, &pwrseq_qca->common, data);
	if (ret)
		return ret;

	if (data->has_enable_gpios) {
		struct gpio_desc *gpiod;

		gpiod = devm_gpiod_get_optional(dev, "wifi-enable", GPIOD_OUT_LOW);
		if (IS_ERR(gpiod))
			return dev_err_probe(dev, PTR_ERR(gpiod), "failed to acquire WIFI enable GPIO\n");
		else if (!gpiod)
			dev_warn(dev, "No WiFi enable GPIO declared\n");

		pwrseq_qca->pwrseq_qcas[PWRSEQ_QCA_WIFI].enable = gpiod;

		gpiod = devm_gpiod_get_optional(dev, "bt-enable", GPIOD_OUT_LOW);
		if (IS_ERR(gpiod))
			return dev_err_probe(dev, PTR_ERR(gpiod), "failed to acquire BT enable GPIO\n");
		else if (!gpiod)
			dev_warn(dev, "No BT enable GPIO declared\n");

		pwrseq_qca->pwrseq_qcas[PWRSEQ_QCA_BT].enable = gpiod;
	}

	/* If we have no control over device's enablement, make sure that sleep clock is always running */
	if (!pwrseq_qca->common.vddio ||
	    !pwrseq_qca->common.num_vregs ||
	    !(pwrseq_qca->pwrseq_qcas[PWRSEQ_QCA_BT].enable &&
	      pwrseq_qca->pwrseq_qcas[PWRSEQ_QCA_WIFI].enable)) {
		ret = clk_set_rate(pwrseq_qca->common.susclk, SUSCLK_RATE_32KHZ);
		if (ret)
			return ret;

		ret = clk_prepare_enable(pwrseq_qca->common.susclk);
		if (ret)
			return ret;

		ret = devm_add_action_or_reset(dev, pwrseq_qca_unprepare_susclk, &pwrseq_qca->common);
		if (ret)
			return ret;
	}

	for (i = 0; i < PWRSEQ_QCA_MAX; i++) {
		pwrseq_qca->pwrseq_qcas[i].common = &pwrseq_qca->common;

		pwrseq = devm_pwrseq_create(dev, &pwrseq_qca_ops, &pwrseq_qca->pwrseq_qcas[i]);
		if (IS_ERR(pwrseq))
			return PTR_ERR(pwrseq);

		onecell->pwrseqs[i] = pwrseq;
	}

	onecell->num = PWRSEQ_QCA_MAX;

	provider = devm_of_pwrseq_provider_register(dev, of_pwrseq_xlate_onecell, onecell);

	return PTR_ERR_OR_ZERO(provider);
}

static const struct qca_device_data qca_soc_data_qca6390 = {
	.vddio = { "vddio", 20000 },
	.vregs = {
		/* 2.0 V */
		{ "vddpcie2", 15000 },
		{ "vddrfa3", 400000 },

		/* 0.95 V */
		{ "vddaon", 100000 },
		{ "vddpmu", 1250000 },
		{ "vddrfa1", 200000 },

		/* 1.35 V */
		{ "vddrfa2", 400000 },
		{ "vddpcie1", 35000 },
	},
	.num_vregs = 7,
	.has_enable_gpios = true,
};

/* Shared between wcn3990 and wcn3991 */
static const struct qca_device_data qca_soc_data_wcn3990 = {
	.vddio = { "vddio", 15000 },
	.vregs = {
		{ "vddxo", 80000  },
		{ "vddrf", 300000 },
		{ "vddch0", 450000 },
		{ "vddch1", 450000 },
	},
	.num_vregs = 4,
};

static const struct qca_device_data qca_soc_data_wcn3998 = {
	.vddio = { "vddio", 10000 },
	.vregs = {
		{ "vddxo", 80000  },
		{ "vddrf", 300000 },
		{ "vddch0", 450000 },
		{ "vddch1", 450000 },
	},
	.num_vregs = 4,
};

static const struct qca_device_data qca_soc_data_wcn6750 = {
	.vddio = { "vddio", 5000 },
	.vregs = {
		{ "vddaon", 26000 },
		{ "vddbtcxmx", 126000 },
		{ "vddrfacmn", 12500 },
		{ "vddrfa0p8", 102000 },
		{ "vddrfa1p7", 302000 },
		{ "vddrfa1p2", 257000 },
		{ "vddrfa2p2", 1700000 },
		{ "vddasd", 200 },
	},
	.num_vregs = 8,
	.has_enable_gpios = true,
};

static const struct of_device_id pwrseq_qca_of_match[] = {
	{ .compatible = "qcom,qca6174-pwrseq", },
	{ .compatible = "qcom,qca6390-pwrseq", .data = &qca_soc_data_qca6390 },
	{ .compatible = "qcom,qca9377-pwrseq" },
	{ .compatible = "qcom,wcn3990-pwrseq", .data = &qca_soc_data_wcn3990 },
	{ .compatible = "qcom,wcn3991-pwrseq", .data = &qca_soc_data_wcn3990 },
	{ .compatible = "qcom,wcn3998-pwrseq", .data = &qca_soc_data_wcn3998 },
	{ .compatible = "qcom,wcn6750-pwrseq", .data = &qca_soc_data_wcn6750 },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, pwrseq_qca_of_match);

static struct platform_driver pwrseq_qca_driver = {
	.probe = pwrseq_qca_probe,
	.driver = {
		.name = "pwrseq_qca",
		.of_match_table = pwrseq_qca_of_match,
	},
};

struct pwrseq_qca_fallback {
	struct pwrseq_qca_one qca_one;
	struct pwrseq_qca_common common;
};

static const struct of_device_id pwrseq_qca_bt_of_match[] = {
	{ .compatible = "qcom,qca6174-bt" },
	{ .compatible = "qcom,qca6390-bt", .data = &qca_soc_data_qca6390},
	{ .compatible = "qcom,qca9377-bt" },
	{ .compatible = "qcom,wcn3990-bt", .data = &qca_soc_data_wcn3990 },
	{ .compatible = "qcom,wcn3991-bt", .data = &qca_soc_data_wcn3990 },
	{ .compatible = "qcom,wcn3998-bt", .data = &qca_soc_data_wcn3998 },
	{ .compatible = "qcom,wcn6750-bt", .data = &qca_soc_data_wcn6750 },
	{ /* sentinel */ },
};

static const struct qca_device_data qca_soc_data_wifi = {
	.vregs = {
		{ "vdd-1.8-xo", 80000  },
		{ "vdd-1.3-rfa", 300000 },
		{ "vdd-3.3-ch0", 450000 },
		{ "vdd-3.3-ch1", 450000 },
	},
	.num_vregs = 4,
};

static const struct of_device_id pwrseq_qca_wifi_of_match[] = {
	{ .compatible = "qcom,wcn3990-wifi", .data = &qca_soc_data_wifi },
	{ /* sentinel */ }
};

static struct pwrseq * pwrseq_qca_fallback_get(struct device *dev)
{
	struct pwrseq_qca_fallback *fallback;
	const struct of_device_id *match;
	const struct qca_device_data *data;
	struct gpio_desc *gpiod;
	int ret;

	match = of_match_device(pwrseq_qca_bt_of_match, dev);
	if (!match)
		return ERR_PTR(-ENODEV);

	data = match->data;
	if (!data)
		data = &qca_soc_data_default;

	fallback = devm_kzalloc(dev, struct_size(fallback, common.vregs, data->num_vregs), GFP_KERNEL);
	if (!data)
		return ERR_PTR(-ENOMEM);

	fallback->qca_one.common = &fallback->common;

	ret = pwrseq_qca_common_init(dev, &fallback->common, data);
	if (ret)
		return ERR_PTR(ret);

	if (data->has_enable_gpios) {
		gpiod = devm_gpiod_get(dev, "enable", GPIOD_OUT_LOW);
		if (IS_ERR(gpiod))
			return ERR_PTR(dev_err_probe(dev, PTR_ERR(gpiod), "failed to acquire enable GPIO\n"));
		fallback->qca_one.enable = gpiod;
	}

	/* If we have no control over device's enablement, make sure that sleep clock is always running */
	if (!fallback->common.vddio ||
	    !fallback->common.num_vregs ||
	    !fallback->qca_one.enable) {
		ret = clk_set_rate(fallback->common.susclk, SUSCLK_RATE_32KHZ);
		if (ret)
			return ERR_PTR(ret);

		ret = clk_prepare_enable(fallback->common.susclk);
		if (ret)
			return ERR_PTR(ret);

		ret = devm_add_action_or_reset(dev, pwrseq_qca_unprepare_susclk, &fallback->common);
		if (ret)
			return ERR_PTR(ret);
	}

	return devm_pwrseq_create(dev, &pwrseq_qca_ops, &fallback->qca_one);
}

static struct pwrseq * pwrseq_qca_fallback_get_bt(struct device *dev, const char *id)
{
	if (strcmp(id, "bt"))
		return ERR_PTR(-ENODEV);

	return pwrseq_qca_fallback_get(dev);
}

static struct pwrseq * pwrseq_qca_fallback_get_wifi(struct device *dev, const char *id)
{
	if (strcmp(id, "wifi"))
		return ERR_PTR(-ENODEV);

	return pwrseq_qca_fallback_get(dev);
}

static struct pwrseq_fallback pwrseq_qca_fallback_bt = {
	.get = pwrseq_qca_fallback_get_bt,
	.of_match_table = pwrseq_qca_bt_of_match,
};

static struct pwrseq_fallback pwrseq_qca_fallback_wifi = {
	.get = pwrseq_qca_fallback_get_wifi,
	.of_match_table = pwrseq_qca_wifi_of_match,
};

static int __init pwrseq_qca_init(void)
{
	int ret;

	ret = platform_driver_register(&pwrseq_qca_driver);
	if (ret)
		return ret;

	ret = pwrseq_fallback_register(&pwrseq_qca_fallback_bt);
	if (ret)
		goto err_bt;

	ret = pwrseq_fallback_register(&pwrseq_qca_fallback_wifi);
	if (ret)
		goto err_wifi;

	return 0;

err_wifi:
	pwrseq_fallback_unregister(&pwrseq_qca_fallback_bt);
err_bt:
	platform_driver_unregister(&pwrseq_qca_driver);

	return ret;
}
module_init(pwrseq_qca_init);

static void __exit pwrseq_qca_exit(void)
{
	pwrseq_fallback_unregister(&pwrseq_qca_fallback_wifi);
	pwrseq_fallback_unregister(&pwrseq_qca_fallback_bt);
	platform_driver_unregister(&pwrseq_qca_driver);
}
module_exit(pwrseq_qca_exit);

MODULE_LICENSE("GPL v2");
