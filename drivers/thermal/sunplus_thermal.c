// SPDX-License-Identifier: GPL-2.0-only
/*
 * SP7021 SoC thermal driver.
 * Copyright (C) Sunplus Tech/Tibbo Tech. 2019
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>

#include <linux/module.h>
#include <linux/err.h>
#include <linux/rtc.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/thermal.h>
#include <linux/nvmem-consumer.h>

#define DISABLE_THREMAL  (1<<31 | 1<<15)
#define ENABLE_THREMAL  (1<<31)

// thermal_sts_0 last 10 bits
#define TEMP_MASK 0x7FF

#define TEMP_RATE  608

/* common data structures */
struct sp_thermal_data {
	struct thermal_zone_device *pcb_tz;
	enum thermal_device_mode mode;
	struct platform_device *pdev;
	long sensor_temp;
	uint32_t id;
	void __iomem *regs;
};

struct sp_thermal_data sp_thermal;

#define MOO5_REG_NAME "thermal_reg"
#define MOO4_REG_NAME             "thermal_moon4"
#define OTP_CALIB_REG "therm_calib"

struct sp_thermal_reg {
	unsigned int mo5_thermal_ctl0;
	unsigned int mo5_thermal_ctl1;
	unsigned int mo5_thermal_ctl2;
	unsigned int mo5_thermal_ctl3;
	unsigned int mo5_tmds_l2sw_ctl;
	unsigned int mo5_l2sw_clksw_ctl;
	unsigned int mo5_i2c2bus_ctl;
	unsigned int mo5_pfcnt_ctl;
	unsigned int mo5_pfcntl_sensor_ctl0;
	unsigned int mo5_pfcntl_sensor_ctl1;
	unsigned int mo5_pfcnt_sts0;
	unsigned int mo5_pfcnt_sts1;
	unsigned int mo5_thermal_sts0;
	unsigned int mo5_thermal_sts1;
	unsigned int mo5_rsv13;
	unsigned int mo5_rsv14;
	unsigned int mo5_rsv15;
	unsigned int mo5_rsv16;
	unsigned int mo5_rsv17;
	unsigned int mo5_rsv18;
	unsigned int mo5_rsv19;
	unsigned int mo5_rsv20;
	unsigned int mo5_rsv21;
	unsigned int mo5_dc09_ctl0;
	unsigned int mo5_dc09_ctl1;
	unsigned int mo5_dc09_ctl2;
	unsigned int mo5_dc12_ctl0;
	unsigned int mo5_dc12_ctl1;
	unsigned int mo5_dc12_ctl2;
	unsigned int mo5_dc15_ctl0;
	unsigned int mo5_dc15_ctl1;
	unsigned int mo5_dc15_ctl2;
};

static struct sp_thermal_reg *thermal_reg_ptr;

struct sp_ctl_reg {
	unsigned int mo4_pllsp_ctl0;
	unsigned int mo4_pllsp_ctl1;
	unsigned int mo4_pllsp_ctl2;
	unsigned int mo4_pllsp_ctl3;
	unsigned int mo4_pllsp_ctl4;
	unsigned int mo4_pllsp_ctl5;
	unsigned int mo4_pllsp_ctl6;
	unsigned int mo5_pfcnt_ctl;
	unsigned int mo4_plla_ctl0;
	unsigned int mo4_plla_ctl1;
	unsigned int mo4_plla_ctl2;
	unsigned int mo4_plla_ctl3;
	unsigned int mo4_plla_ctl4;
	unsigned int mo4_plle_ctl;
	unsigned int mo4_pllf_ctl;
	unsigned int mo4_plltv_ctl0;
	unsigned int mo4_plltv_ctl1;
	unsigned int mo4_plltv_ctl2;
	unsigned int mo4_usbc_ctl;
	unsigned int mo4_uphy0_ctl0;
	unsigned int mo4_uphy0_ctl1;
	unsigned int mo4_uphy0_ctl2;
	unsigned int mo4_uphy1_ctl0;
	unsigned int mo4_uphy1_ctl1;
	unsigned int mo4_uphy1_ctl2;
	unsigned int mo4_uphy1_ctl3;
	unsigned int mo4_pllsys;
	unsigned int mo_clk_sel0;
	unsigned int mo_probe_sel;
	unsigned int mo4_misc_ctl0;
	unsigned int mo4_uphy0_sts;
	unsigned int otp_st;
};

static struct sp_ctl_reg *sp_ctl_reg_ptr;

int otp_thermal_t0;
int otp_thermal_t1;

char *sp7021_otp_coef_read(struct device *_d, ssize_t *_l)
{
	char *ret = NULL;
	struct nvmem_cell *c = nvmem_cell_get(_d, OTP_CALIB_REG);

	if (IS_ERR_OR_NULL(c)) {
		dev_err(_d, "OTP read failure:%ld", PTR_ERR(c));
		return NULL;
	}
	ret = nvmem_cell_read(c, _l);
	nvmem_cell_put(c);
	dev_dbg(_d, "%d bytes read from OTP", *_l);
	return ret;
}

static void sp7021_get_otp_temp_coef(struct device *_d)
{
	ssize_t otp_l = 0;
	char *otp_v;

	otp_v = sp7021_otp_coef_read(_d, &otp_l);
	if (otp_l < 3)
		return;
	if (IS_ERR_OR_NULL(otp_v))
		return;
	dev_dbg(_d, "OTP: %d %d %d", otp_v[0], otp_v[1], otp_v[2]);

	otp_thermal_t0 = otp_v[0] | (otp_v[1] << 8);
	otp_thermal_t0 = otp_thermal_t0 & TEMP_MASK;
	otp_thermal_t1 = (otp_v[1] >> 3) | (otp_v[2] << 5);
	otp_thermal_t1 = otp_thermal_t1 & TEMP_MASK;
	if (otp_thermal_t0 == 0)
		otp_thermal_t0 = 1518;
}

static int sp_thermal_get_sensor_temp(void *_data, int *temp)
{
	struct sp_thermal_data *data = _data;
	struct sp_thermal_reg *thermal_reg = data->regs;
	int t_code;

	t_code = (readl(&(thermal_reg->mo5_thermal_sts0)) & TEMP_MASK);
	*temp = ((otp_thermal_t0 - t_code)*10000/TEMP_RATE)+3500;
	*temp *= 10;   // milli means 10^-3!
	dev_dbg(&(data->pdev->dev), "tc:%d t:%d", t_code, *temp);
	return 0;
}

static struct thermal_zone_of_device_ops sp_of_thermal_ops = {
	.get_temp = sp_thermal_get_sensor_temp,
};

static int sp_thermal_register_sensor(struct platform_device *pdev,
					struct sp_thermal_data *data,
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
	return ret;  }

static int sp7021_thermal_probe(struct platform_device *_pd)
{
	struct sp_thermal_data *sp_data;
	int ret;
	struct resource *res;
	void __iomem *reg_base;
	void __iomem *ctl_base;
	int ctl_code;

	sp_data = devm_kzalloc(&(_pd->dev), sizeof(*sp_data), GFP_KERNEL);
	if (!sp_data)
		return -ENOMEM;

	memset(&sp_thermal, 0, sizeof(sp_thermal));

	res = platform_get_resource_byname(_pd, IORESOURCE_MEM, MOO5_REG_NAME);
	if (IS_ERR(res)) {
		dev_err(&(_pd->dev), "get_resource(%s) fail\n", MOO5_REG_NAME);
		ret = PTR_ERR(res);
		return ret;
	}
	reg_base = devm_ioremap(&(_pd->dev), res->start, resource_size(res));
	if (IS_ERR(reg_base)) {
		dev_err(&(_pd->dev), "ioremap_resource(%s) fail\n", MOO5_REG_NAME);
		ret = PTR_ERR(res);
		return ret;
	}
	sp_data->regs = reg_base;

	res = platform_get_resource_byname(_pd, IORESOURCE_MEM, MOO4_REG_NAME);
	if (IS_ERR(res)) {
		dev_err(&(_pd->dev), "get_resource(%s) fail\n", MOO4_REG_NAME);
		ret = PTR_ERR(res);
		return ret;
	}
	ctl_base = devm_ioremap(&(_pd->dev), res->start, resource_size(res));
	if (IS_ERR(reg_base)) {
		dev_err(&(_pd->dev), "ioremap_resource(%s) fail\n", MOO4_REG_NAME);
		ret = PTR_ERR(res);
		return ret;
	}

	dev_dbg(&(_pd->dev), "reg:%p ctl:%p\n", reg_base, ctl_base);

	thermal_reg_ptr = (struct sp_thermal_reg *)(reg_base);
	sp_ctl_reg_ptr = (struct sp_ctl_reg *)(ctl_base);

	// FIXME: enable thermal function - customize this part
	writel(ENABLE_THREMAL, &(thermal_reg_ptr->mo5_thermal_ctl0));
	// FIXME: it's just reg state. Clock enabled at different place
	ctl_code = 0xFFFF & readl(&(sp_ctl_reg_ptr->mo_clk_sel0));
	// writel(0x78F07810 , &sp_ctl_reg_ptr->mo_clk_sel0);  // enable thermal function
	dev_dbg(&(_pd->dev), "ctl_code %x ", ctl_code);

	platform_set_drvdata(_pd, sp_data);
	sp7021_get_otp_temp_coef(&(_pd->dev));
	ret = sp_thermal_register_sensor(_pd, sp_data, 0);
	if (ret == 0)
		dev_info(&(_pd->dev), "by Sunplus (C) 2020");
	return ret;
}

static int sp7021_thermal_remove(struct platform_device *_pd)
{
	// nothing to do case devm_*
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

MODULE_AUTHOR("lH Kuo <lh.kuo@sunplus.com>");
MODULE_DESCRIPTION("Thermal driver for SP7021 SoC");
MODULE_LICENSE("GPL v2");
