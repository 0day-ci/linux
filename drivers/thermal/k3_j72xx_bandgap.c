// SPDX-License-Identifier: GPL-2.0
/*
 * TI Bandgap temperature sensor driver for J72XX SoC Family
 *
 * Copyright (C) 2021 Texas Instruments Incorporated - http://www.ti.com/
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/pm_runtime.h>
#include <linux/err.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/of_platform.h>
#include <linux/io.h>
#include <linux/thermal.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/math.h>
#include <linux/slab.h>

#define K3_VTM_DEVINFO_PWR0_OFFSET		0x4
#define K3_VTM_DEVINFO_PWR0_CVD_CT_MASK		0xf
#define K3_VTM_DEVINFO_PWR0_TEMPSENS_CT_MASK	0xf0
#define K3_VTM_TMPSENS0_CTRL_OFFSET		0x300
#define K3_VTM_MISC_CTRL_OFFSET			0xc
#define K3_VTM_TMPSENS_STAT_OFFSET		0x8
#define K3_VTM_ANYMAXT_OUTRG_ALERT_EN		0x1
#define K3_VTM_MISC_CTRL2_OFFSET		0x10
#define K3_VTM_REGS_PER_TS			0x10
#define K3_VTM_TS_STAT_DTEMP_MASK		0x3ff
#define K3_VTM_MAX_NUM_TS			8
#define K3_VTM_TMPSENS_CTRL_CBIASSEL		BIT(0)
#define K3_VTM_TMPSENS_CTRL_SOC			BIT(5)
#define K3_VTM_TMPSENS_CTRL_CLRZ		BIT(6)
#define K3_VTM_TMPSENS_CTRL_CLKON_REQ		BIT(7)
#define K3_VTM_TMPSENS_CTRL_MAXT_OUTRG_EN	BIT(11)

#define K3_VTM_CORRECTION_TEMP_CNT		3

#define K3_VTM_ADC_BEGIN_VAL			6
#define K3_VTM_ADC_END_VAL			944

#define MINUS40CREF				5
#define PLUS30CREF				253
#define PLUS125CREF				730
#define PLUS150CREF				940

#define MAX_TEMP				123000
#define COOL_DOWN_TEMP				105000

//#define DEBUG_VTM

static int *ref_table;
static int *derived_table;

s64 golden_factors[] = {
	-490019999999999936,
	3251200000000000,
	-1705800000000,
	603730000,
	-92627,
};

s64 pvt_wa_factors[] = {
	-415230000000000000,
	3126600000000000,
	-1157800000000,
};

static s64 int_power(unsigned long long base, unsigned int exp)
{
	s64 result = 1;

	while (exp) {
		if (exp & 1)
			result *= base;
		exp >>= 1;
		base *= base;
	}

	return result;
}

static int compute_value(int index, s64 *factors, int nr_factors, int reduction)
{
	s64 value = 0;
	int i;

	for (i = 0; i < nr_factors; i++)
		value += factors[i] * int_power(index, i);

	return (int)(value / int_power(10, reduction));
}

void init_table(bool wa, int *mytable)
{
	s64 *factors;
	const int size = 1024;
	int factors_size;
	int i;

	if (wa) {
		factors_size = 3;
		factors = pvt_wa_factors;
	} else {
		factors_size = 5;
		factors = golden_factors;
	}

	for (i = 0; i < size; i++)
		mytable[i] = compute_value(i, factors, factors_size, 13);
}

static void fill_table(int err1, int err2, int ref1, int ref2, int idx1,
		       int idx2)
{
	int m, c, num, den, i, err;

	/**
	 * Calculate the slope with adc values read from the register
	 * as the y-axis param and err in adc value as x-axis param
	 */
	if (err1 != err2) {
		num = ref2 - ref1;
		den = err2 - err1;
		m = num / den;
		c = ref2 - m * err2;
		for (i = idx1; i <= idx2; i++) {
			err = (i - c) / m;
			if (((i + err1) < 0) || ((i + err1) > 1023))
				continue;
			derived_table[i] = ref_table[i + err];
		}
	} else { /* Constant error take care of divide by zero */
		for (i = idx1; i < idx2; i++) {
			if (((i + err1) < 0) || ((i + err1) > 1023))
				continue;
			derived_table[i] = ref_table[i + err1];
		}
	}
}

static int prep_lookup_table(int minus40cerr, int plus30cerr, int plus125cerr,
			     int plus150cerr)
{
	int start_temp, inc, i;

	/* Fill up the lookup table region -40C to +30C */
	fill_table(minus40cerr, plus30cerr, MINUS40CREF, PLUS30CREF, 0,
		   PLUS30CREF);
	/* Fill up the lookup table region +30C to +125C */
	fill_table(plus30cerr, plus125cerr, PLUS30CREF, PLUS125CREF, PLUS30CREF,
		   PLUS125CREF);
	/* Fill up the lookup table region +125C to +150C */
	fill_table(plus125cerr, plus150cerr, PLUS125CREF, PLUS150CREF, PLUS125CREF,
		   PLUS150CREF);

	/* Get to the first valid temperature */
	i = 0;
	while (!derived_table[i])
		i++;

	start_temp = i;
	/*
	 * Get to the last zero index and back fill the temperature for
	 * sake of continuity
	 */
	if (i) {
		/* 300 milli celsius steps */
		while (i--)
			derived_table[i] = derived_table[i + 1] - 300;
		/* case 0 */
		derived_table[i] = derived_table[i + 1] - 300;
	}

	/*
	 * Fill the last trailing 0s which are unfilled with increments of
	 * 100 milli celsius till 1023 code
	 */
	i = 1023;
	while (!derived_table[i])
		i--;

	i++;
	inc = 1;
	while (i < 1024) {
		derived_table[i] = derived_table[i - 1] + inc * 100;
		i++;
	}

	return 0;
}

struct k3_thermal_data;

struct k3_j72xx_bandgap {
	struct device *dev;
	void __iomem *base;
	void __iomem *cfg2_base;
	void __iomem *fuse_base;
	const struct k3_j72xx_bandgap_data	*conf;
	spinlock_t lock; /* shields this struct */
	int ts_cnt;
	struct k3_thermal_data *ts_data[K3_VTM_MAX_NUM_TS];
};

/* common data structures */
struct k3_thermal_data {
	struct thermal_zone_device *ti_thermal;
	struct thermal_cooling_device *cool_dev;
	struct k3_j72xx_bandgap *bgp;
	enum thermal_device_mode mode;
	int sensor_id;
	u32 ctrl_offset;
	u32 stat_offset;
	int prev_temp;
	int ct_offsets[K3_VTM_CORRECTION_TEMP_CNT];
	int ct_bm[K3_VTM_CORRECTION_TEMP_CNT];
	int ref_adc_val[3];
	int act_adc_val[3];
	int slope[2];
	int coeff[2];
};

int two_cmp(int tmp, int mask)
{
	tmp = ~(tmp);
	tmp &= mask;
	tmp += 1;

	/* Return negative value */
	return (0 - tmp);
}

static unsigned int vtm_get_best_value(unsigned int s0, unsigned int s1,
				       unsigned int s2)
{
	int d01 = abs(s0 - s1);
	int d02 = abs(s0 - s2);
	int d12 = abs(s1 - s2);

	if (d01 <= d02 && d01 <= d12)
		return (s0 + s1) / 2;

	if (d02 <= d01 && d02 <= d12)
		return (s0 + s2) / 2;

	return (s1 + s2) / 2;
}

static inline int k3_bgp_read_temp(struct k3_thermal_data *devdata,
				   int *temp)
{
	struct k3_j72xx_bandgap *bgp;
	unsigned int dtemp, s0, s1, s2;

	bgp = devdata->bgp;
	/*
	 * Errata is applicable for am654 pg 1.0 silicon/J7ES. There
	 * is a variation of the order for certain degree centigrade on AM654.
	 * Work around that by getting the average of two closest
	 * readings out of three readings everytime we want to
	 * report temperatures.
	 *
	 * Errata workaround.
	 */
	s0 = readl(bgp->base + devdata->stat_offset) &
		K3_VTM_TS_STAT_DTEMP_MASK;
	s1 = readl(bgp->base + devdata->stat_offset) &
		K3_VTM_TS_STAT_DTEMP_MASK;
	s2 = readl(bgp->base + devdata->stat_offset) &
		K3_VTM_TS_STAT_DTEMP_MASK;
	dtemp = vtm_get_best_value(s0, s1, s2);

	if (dtemp < 0 || dtemp > 1023)
		return -EINVAL;

	*temp = derived_table[dtemp];

	return 0;
}

/* Get temperature callback function for thermal zone */
static int k3_thermal_get_temp(void *devdata, int *temp)
{
	struct k3_thermal_data *data = devdata;
	int ret = 0;

	ret = k3_bgp_read_temp(data, temp);
	if (ret)
		return ret;

	data->prev_temp = *temp;

	return ret;
}

static const struct thermal_zone_of_device_ops k3_of_thermal_ops = {
	.get_temp = k3_thermal_get_temp,
};

static int k3_j72xx_bandgap_temp_to_adc_code(int temp)
{
	int low = 0, high = 1023, mid;

	if (temp > 160000 || temp < -50000)
		return -EINVAL;

	/* Binary search to find the adc code */
	while (low < (high - 1)) {
		mid = (low + high) / 2;
		if (temp <= derived_table[mid])
			high = mid;
		else
			low = mid;
	}

	return mid;
}

static void get_efuse_values(int id, struct k3_thermal_data *data, int *err,
			     struct k3_j72xx_bandgap *bgp)
{
	int i, tmp, pow;

	/* Populate efuse reg offsets & Bit masks for -40C, 30C, 125C */
	switch (id) {
	case 0:
		data->ct_offsets[0] = 0x0;
		data->ct_offsets[1] = 0x8;
		data->ct_offsets[2] = 0x4;
		data->ct_bm[0] = 0x3f;
		data->ct_bm[1] = 0x1fe000;
		data->ct_bm[2] = 0x1ff;
		break;

	case 1:
		data->ct_offsets[0] = 0x0;
		data->ct_offsets[1] = 0x8;
		data->ct_offsets[2] = 0x4;
		data->ct_bm[0] = 0xfc0;
		data->ct_bm[1] = 0x1fe00000;
		data->ct_bm[2] = 0x3fe00;
		break;

	case 2:
		data->ct_offsets[0] = 0x0;
		data->ct_offsets[1] = -1;
		data->ct_offsets[2] = 0x4;
		data->ct_bm[0] = 0x3f000;
		data->ct_bm[1] = 0x7f800000;
		data->ct_bm[2] = 0x7fc0000;
		break;

	case 3:
		data->ct_offsets[0] = 0x0;
		data->ct_offsets[1] = 0xC;
		data->ct_offsets[2] = -1; /* Spread across 2 registers */
		data->ct_bm[0] = 0xfc0000;
		data->ct_bm[1] = 0x1fe0;
		data->ct_bm[2] = 0x1f800000;
		break;

	case 4:
		data->ct_offsets[0] = 0x0;
		data->ct_offsets[1] = 0xc;
		data->ct_offsets[2] = 0x8;
		data->ct_bm[0] = 0x3f000000;
		data->ct_bm[1] = 0x1fe000;
		data->ct_bm[2] = 0x1ff0;
		break;
	}

	for (i = 0; i < 3; i++) {
		/* Extract the offset value using bit-mask */
		if (data->ct_offsets[i] == -1 && i == 1) {
			/* 25C offset Case of Sensor 2 split between 2 regs */
			tmp = (readl(bgp->fuse_base + 0x8) & 0xE0000000) >> (29);
			tmp |= ((readl(bgp->fuse_base + 0xC) & 0x1F) << 3);
			pow = tmp & 0x80;
		} else if (data->ct_offsets[i] == -1 && i == 2) {
			/* 125C Case of Sensor 3 split between 2 regs */
			tmp = (readl(bgp->fuse_base + 0x4) & 0xF8000000) >> (27);
			tmp |= ((readl(bgp->fuse_base + 0x8) & 0xF) << 5);
			pow = tmp & 0x100;
		} else {
			tmp = readl(bgp->fuse_base + data->ct_offsets[i]);
			tmp &= data->ct_bm[i];
			tmp = tmp >> __ffs(data->ct_bm[i]);

			/* Obtain the sign bit pow*/
			pow = data->ct_bm[i] >> __ffs(data->ct_bm[i]);
			pow += 1;
			pow /= 2;
		}

		/* Check for negative value */
		if (tmp & pow) {
			/* 2's complement value */
			tmp = two_cmp(tmp, data->ct_bm[i] >> __ffs(data->ct_bm[i]));
		}
		err[i] = tmp;
	}
}

#ifdef DEBUG_VTM
static void print_look_up_table(struct device *dev)
{
	int i;

	dev_info(dev, "The contents of derived array\n");
	dev_info(dev, "Code   Temperaturei\n");
	for (i = 0; i <= 1023; i++)
		dev_info(dev, "%d       %d\n", i, derived_table[i]);
}
#endif

static const struct of_device_id of_k3_j72xx_bandgap_match[];

struct k3_j72xx_bandgap_j72xx_data {
	unsigned int	workaround;
};

static int k3_j72xx_bandgap_probe(struct platform_device *pdev)
{
	int ret = 0, cnt, val, id, reg_cnt = 0, j, table_size = 1024;
	int err[3], high_max, low_max;
	struct resource *res;
	struct device *dev = &pdev->dev;
	struct k3_j72xx_bandgap *bgp;
	struct k3_thermal_data *data;
	int workaround_needed = 0;
	const struct of_device_id *of_id;
	const struct k3_j72xx_bandgap_j72xx_data *driver_data;

	bgp = devm_kzalloc(&pdev->dev, sizeof(*bgp), GFP_KERNEL);
	if (!bgp)
		return -ENOMEM;

	bgp->dev = dev;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	bgp->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(bgp->base))
		return PTR_ERR(bgp->base);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	bgp->cfg2_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(bgp->cfg2_base))
		return PTR_ERR(bgp->cfg2_base);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	bgp->fuse_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(bgp->fuse_base))
		return PTR_ERR(bgp->fuse_base);

	of_id = of_match_device(of_k3_j72xx_bandgap_match, &pdev->dev);
	if (of_id) {
		driver_data = of_id->data;
		workaround_needed = driver_data->workaround;
	}

	pm_runtime_enable(dev);
	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		pm_runtime_put_noidle(dev);
		pm_runtime_disable(dev);
		return ret;
	}

	/* Get the sensor count in the VTM */
	val = readl(bgp->base + K3_VTM_DEVINFO_PWR0_OFFSET);
	cnt = val & K3_VTM_DEVINFO_PWR0_TEMPSENS_CT_MASK;
	cnt >>= __ffs(K3_VTM_DEVINFO_PWR0_TEMPSENS_CT_MASK);
	bgp->ts_cnt = cnt;

	data = devm_kcalloc(bgp->dev, cnt, sizeof(*data), GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto err_alloc;
	}

	ref_table = devm_kzalloc(bgp->dev, sizeof(*ref_table) * table_size,
				 GFP_KERNEL);
	if (!ref_table) {
		ret = -ENOMEM;
		goto err_alloc;
	}

	derived_table = devm_kzalloc(bgp->dev, sizeof(*derived_table) * table_size,
				     GFP_KERNEL);
	if (!derived_table) {
		ret = -ENOMEM;
		goto err_alloc;
	}

	/* Workaround not needed if bit30/bit31 is set even for J721e */
	if (workaround_needed && (readl(bgp->fuse_base + 0x0) & 0xc0000000) == 0xc0000000)
		workaround_needed = false;

	if (!workaround_needed) {
		dev_info(bgp->dev, "work around NOT needed!!\n");
		init_table(false, ref_table);
	} else {
		dev_info(bgp->dev, "work around needed!!\n");
		init_table(true, ref_table);
	}

	/* Register the thermal sensors */
	for (id = 0; id < cnt; id++) {
		data[id].sensor_id = id;
		data[id].bgp = bgp;
		data[id].ctrl_offset = K3_VTM_TMPSENS0_CTRL_OFFSET + id * 0x20;
		data[id].stat_offset = data[id].ctrl_offset +
					K3_VTM_TMPSENS_STAT_OFFSET;

		if (!workaround_needed)
			goto prepare_lookup;
		/* ref adc values for -40C, 30C & 125C respectively */
		data[id].ref_adc_val[0] = MINUS40CREF;
		data[id].ref_adc_val[1] = PLUS30CREF;
		data[id].ref_adc_val[2] = PLUS125CREF;
		get_efuse_values(id, &data[id], err, bgp);

prepare_lookup:
		if (id == 0 && workaround_needed) {
			prep_lookup_table(err[0], err[1], err[2], 0);
		} else if (id == 0 && !workaround_needed) {
			for (j = 0; j < table_size; j++)
				derived_table[j] = ref_table[j];
		}

		val = readl(data[id].bgp->cfg2_base + data[id].ctrl_offset);
		val |= (K3_VTM_TMPSENS_CTRL_MAXT_OUTRG_EN |
			K3_VTM_TMPSENS_CTRL_SOC |
			K3_VTM_TMPSENS_CTRL_CLRZ | BIT(4));
		writel(val, data[id].bgp->cfg2_base + data[id].ctrl_offset);

		bgp->ts_data[id] = &data[id];
		data[id].ti_thermal =
		devm_thermal_zone_of_sensor_register(bgp->dev, id,
						     &data[id],
						     &k3_of_thermal_ops);
		if (IS_ERR(data[id].ti_thermal)) {
			dev_err(bgp->dev, "thermal zone device is NULL\n");
			ret = PTR_ERR(data[id].ti_thermal);
			goto err_alloc;
		}

		reg_cnt++;

		/* Initialize Previous temp */
		k3_thermal_get_temp(&data[id], &data[id].prev_temp);
	}

	/*
	 * Program TSHUT thresholds
	 * Step 1: set the thresholds to ~123C and 105C WKUP_VTM_MISC_CTRL2
	 * Step 2: WKUP_VTM_TMPSENS_CTRL_j set the MAXT_OUTRG_EN  bit
	 *         This is already taken care as per of init
	 * Step 3: WKUP_VTM_MISC_CTRL set the ANYMAXT_OUTRG_ALERT_EN  bit
	 */
	high_max = k3_j72xx_bandgap_temp_to_adc_code(MAX_TEMP);
	low_max = k3_j72xx_bandgap_temp_to_adc_code(COOL_DOWN_TEMP);

	writel((low_max << 16) | high_max, data[0].bgp->cfg2_base +
	       K3_VTM_MISC_CTRL2_OFFSET);
	mdelay(100);
	writel(K3_VTM_ANYMAXT_OUTRG_ALERT_EN, data[0].bgp->cfg2_base +
	       K3_VTM_MISC_CTRL_OFFSET);

	platform_set_drvdata(pdev, bgp);

#ifdef DEBUG_VTM
	print_look_up_table(dev);
#endif
	/*
	 * Now that the derived_table has the appropriate look up values
	 * Free up the ref_table
	 */
	kfree(ref_table);

	return 0;

err_alloc:
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	return ret;
}

static int k3_j72xx_bandgap_remove(struct platform_device *pdev)
{
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

const struct k3_j72xx_bandgap_j72xx_data k3_j72xx_bandgap_j721e_data = {
	.workaround = 1,
};

const struct k3_j72xx_bandgap_j72xx_data k3_j72xx_bandgap_j7200_data = {
	.workaround = 0,
};

static const struct of_device_id of_k3_j72xx_bandgap_match[] = {
	{
		.compatible = "ti,j721e-vtm",
		.data = (void *)&k3_j72xx_bandgap_j721e_data,
	},
	{
		.compatible = "ti,j7200-vtm",
		.data = (void *)&k3_j72xx_bandgap_j7200_data,
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, of_k3_j72xx_bandgap_match);

static struct platform_driver k3_j72xx_bandgap_sensor_driver = {
	.probe = k3_j72xx_bandgap_probe,
	.remove = k3_j72xx_bandgap_remove,
	.driver = {
		.name = "k3-j72xx-soc-thermal",
		.of_match_table	= of_k3_j72xx_bandgap_match,
	},
};

module_platform_driver(k3_j72xx_bandgap_sensor_driver);

MODULE_DESCRIPTION("K3 bandgap temperature sensor driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("J Keerthy <j-keerthy@ti.com>");
