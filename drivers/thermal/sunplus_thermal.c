// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Sunplus Inc.
 * Author: Li-hao Kuo <lhjeff911@gmail.com>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/rtc.h>
#include <linux/thermal.h>

#define DISABLE_THREMAL		(BIT(31) | BIT(15))
#define ENABLE_THREMAL		BIT(31)
#define SP_THREMAL_MASK		GENMASK(10, 0)

#define TEMP_RATE		608
#define TEMP_BASE		3500
#define TEMP_OTP_BASE		1518

#define SP_THERMAL_CTL0_REG	0x0000
#define SP_THERMAL_STS0_REG	0x0030

/* common data structures */
struct sp_thermal_data {
	struct thermal_zone_device *pcb_tz;
	struct platform_device *pdev;
	enum thermal_device_mode mode;
	long sensor_temp;
	void __iomem *regs;
	int otp_temp0;
	int otp_temp1;
	u32 id;
};

char *sp7021_otp_coef_read(struct device *dev, ssize_t *len)
{
	char *ret = NULL;
	struct nvmem_cell *c = nvmem_cell_get(dev, "therm_calib");

	if (IS_ERR_OR_NULL(c)) {
		dev_err(dev, "OTP read failure:%ld", PTR_ERR(c));
		return NULL;
	}
	ret = nvmem_cell_read(c, len);
	nvmem_cell_put(c);
	dev_dbg(dev, "%d bytes read from OTP", *len);
	return ret;
}

static void sp7021_get_otp_temp_coef(struct sp_thermal_data *sp_data, struct device *_d)
{
	ssize_t otp_l = 0;
	char *otp_v;

	otp_v = sp7021_otp_coef_read(_d, &otp_l);
	if (otp_l < 3)
		return;
	if (IS_ERR_OR_NULL(otp_v))
		return;
	sp_data->otp_temp0 = otp_v[0] | (otp_v[1] << 8);
	sp_data->otp_temp0 = otp_v[0] | (otp_v[1] << 8);
	sp_data->otp_temp0 = FIELD_GET(SP_THREMAL_MASK, sp_data->otp_temp0);
	sp_data->otp_temp1 = (otp_v[1] >> 3) | (otp_v[2] << 5);
	sp_data->otp_temp1 = FIELD_GET(SP_THREMAL_MASK, sp_data->otp_temp1);
	if (sp_data->otp_temp0 == 0)
		sp_data->otp_temp0 = TEMP_OTP_BASE;
}

static int sp_thermal_get_sensor_temp(void *data, int *temp)
{
	struct sp_thermal_data *sp_data = data;
	int t_code;

	t_code = readl(sp_data->regs + SP_THERMAL_STS0_REG);
	t_code = FIELD_GET(SP_THREMAL_MASK, t_code);
	*temp = ((sp_data->otp_temp0 - t_code) * 10000 / TEMP_RATE) + TEMP_BASE;
	*temp *= 10;
	dev_dbg(&(sp_data->pdev->dev), "tc:%d t:%d", t_code, *temp);
	return 0;
}

static struct thermal_zone_of_device_ops sp_of_thermal_ops = {
	.get_temp = sp_thermal_get_sensor_temp,
};

static int sp_thermal_register_sensor(struct platform_device *pdev, struct sp_thermal_data *data,
				       int index)
{
	int ret;

	data->id = index;
	data->pcb_tz = devm_thermal_zone_of_sensor_register(&pdev->dev,
					data->id, data, &sp_of_thermal_ops);
	if (!IS_ERR_OR_NULL(data->pcb_tz))
		return 0;
	ret = PTR_ERR(data->pcb_tz);
	data->pcb_tz = NULL;
	dev_err(&pdev->dev, "sensor#%d reg fail: %d\n", index, ret);
	return ret;
}

static int sp7021_thermal_probe(struct platform_device *pdev)
{
	struct sp_thermal_data *sp_data;
	struct resource *res;
	int ret;

	sp_data = devm_kzalloc(&(pdev->dev), sizeof(*sp_data), GFP_KERNEL);
	if (!sp_data)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (IS_ERR(res))
		return dev_err_probe(&(pdev->dev), PTR_ERR(res), "resource get fail\n");

	sp_data->regs = devm_ioremap(&(pdev->dev), res->start, resource_size(res));
	if (IS_ERR(sp_data->regs))
		return dev_err_probe(&(pdev->dev), PTR_ERR(sp_data->regs), "mas_base get fail\n");

	writel(ENABLE_THREMAL, sp_data->regs + SP_THERMAL_CTL0_REG);

	platform_set_drvdata(pdev, sp_data);
	sp7021_get_otp_temp_coef(sp_data, &pdev->dev);
	ret = sp_thermal_register_sensor(pdev, sp_data, 0);

	return ret;
}

static int sp7021_thermal_remove(struct platform_device *_pd)
{
	return 0;
}

static const struct of_device_id of_sp7021_thermal_ids[] = {
	{ .compatible = "sunplus,sp7021-thermal" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_sp7021_thermal_ids);

static struct platform_driver sp7021_thermal_driver = {
	.probe	= sp7021_thermal_probe,
	.remove	= sp7021_thermal_remove,
	.driver	= {
		.name	= "sp7021-thermal",
		.of_match_table = of_match_ptr(of_sp7021_thermal_ids),
		},
};
module_platform_driver(sp7021_thermal_driver);

MODULE_AUTHOR("Li-hao Kuo <lhjeff911@gmail.com>");
MODULE_DESCRIPTION("Thermal driver for SP7021 SoC");
MODULE_LICENSE("GPL v2");
