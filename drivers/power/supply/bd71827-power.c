// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * bd71827-power.c
 * @file ROHM BD71815, BD71827, BD71828 and BD71878 Charger driver
 *
 * Copyright 2021.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/linear_range.h>
#include <linux/mfd/rohm-bd71815.h>
#include <linux/mfd/rohm-bd71827.h>
#include <linux/mfd/rohm-bd71828.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/power/simple_gauge.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define MAX(X, Y) ((X) >= (Y) ? (X) : (Y))
#define uAMP_TO_mAMP(ma) ((ma) / 1000)

#define LINEAR_INTERPOLATE(y_hi, y_lo, x_hi, x_lo, x) \
	((y_lo) + ((x) - (x_lo)) * ((y_hi) - (y_lo)) / ((x_hi) - (x_lo)))

#define CAP2DSOC(cap, full_cap) ((cap) * 1000 / (full_cap))

/* common defines */
#define BD7182x_MASK_VBAT_U			0x1f
#define BD7182x_MASK_VDCIN_U			0x0f
#define BD7182x_MASK_IBAT_U			0x3f
#define BD7182x_MASK_CURDIR_DISCHG		0x80
#define BD7182x_MASK_CC_CCNTD_HI		0x0FFF
#define BD7182x_MASK_CC_CCNTD			0x0FFFFFFF
#define BD7182x_MASK_CHG_STATE			0x7f
#define BD7182x_MASK_CC_FULL_CLR		0x10
#define BD7182x_MASK_BAT_TEMP			0x07
#define BD7182x_MASK_DCIN_DET			BIT(0)
#define BD7182x_MASK_CONF_PON			BIT(0)
#define BD71815_MASK_CONF_XSTB			BIT(1)
#define BD7182x_MASK_BAT_STAT			0x3f
#define BD7182x_MASK_DCIN_STAT			0x07

#define BD7182x_MASK_CCNTRST			0x80
#define BD7182x_MASK_CCNTENB			0x40
#define BD7182x_MASK_CCCALIB			0x20
#define BD7182x_MASK_WDT_AUTO			0x40
#define BD7182x_MASK_VBAT_ALM_LIMIT_U		0x01
#define BD7182x_MASK_CHG_EN			0x01

#define BD7182x_DCIN_COLLAPSE_DEFAULT		0x36

static const struct linear_range dcin_collapse = {
	.min = 0,
	.min_sel = 0,
	.max_sel = 0xff,
	/* Anti-collapse voltage threshold 0.0V to 20.4V range, 80 mV steps */
	.step = 80000,
};

/* Measured min and max value clear bits */
#define BD718XX_MASK_VSYS_MIN_AVG_CLR		0x10

#define JITTER_DEFAULT				3000
#define MAX_CURRENT_DEFAULT			890000		/* uA */
#define AC_NAME					"bd71827_ac"
#define BAT_NAME				"bd71827_bat"

/*
 * VBAT Low voltage detection Threshold
 * 0x00D4*16mV = 212*0.016 = 3.392v
 */
#define VBAT_LOW_TH			0x00D4

#define THR_RELAX_CURRENT_DEFAULT		5		/* mA */
#define THR_RELAX_TIME_DEFAULT			(60 * 60)	/* sec. */

#define DGRD_CYC_CAP_DEFAULT			88	/* 1 micro Ah */

#define DGRD_TEMP_H_DEFAULT			450	/* 0.1 degrees C */
#define DGRD_TEMP_M_DEFAULT			250	/* 0.1 degrees C */
#define DGRD_TEMP_L_DEFAULT			50	/* 0.1 degrees C */
#define DGRD_TEMP_VL_DEFAULT			0	/* 0.1 degrees C */

#define SOC_EST_MAX_NUM_DEFAULT			5
#define PWRCTRL_NORMAL				0x22
#define PWRCTRL_RESET				0x23

/*
 * Originally we relied upon a fixed size table of OCV and VDR params.
 * However the exising linux power-supply batinfo interface for getting the OCV
 * values from DT does not have fixed amount of OCV values. Thus we use fixed
 * parameter amount only for values provided as module params - and use this
 * only as maximum number of parameters when values come from DT.
 */
#define NUM_BAT_PARAMS				23
#define MAX_NUM_VDR_VALUES NUM_BAT_PARAMS

struct pwr_regs {
	int used_init_regs;
	u8 vbat_init;
	u8 vbat_init2;
	u8 vbat_init3;
	u8 vbat_avg;
	u8 ibat;
	u8 ibat_avg;
	u8 meas_clear;
	u8 vsys_min_avg;
	u8 btemp_vth;
	u8 chg_state;
	u8 coulomb3;
	u8 coulomb2;
	u8 coulomb1;
	u8 coulomb0;
	u8 coulomb_ctrl;
	u8 vbat_rex_avg;
	u8 coulomb_full3;
	u8 cc_full_clr;
	u8 coulomb_chg3;
	u8 bat_temp;
	u8 dcin_stat;
	u8 dcin_collapse_limit;
	u8 chg_set1;
	u8 chg_en;
	u8 vbat_alm_limit_u;
	u8 batcap_mon_limit_u;
	u8 conf;
	u8 vdcin;
#ifdef PWRCTRL_HACK
	u8 pwrctrl;
#endif
};

/* Regions for High, Medium, Low, Very Low temperature */
enum {
	VDR_TEMP_HIGH,
	VDR_TEMP_NORMAL,
	VDR_TEMP_LOW,
	VDR_TEMP_VERY_LOW,
	NUM_VDR_TEMPS
};

/*
 * This works as long as we have only one instance of this driver (which is
 * likely to be the case even with DT originated battery info). Anyways,
 * consider moving these in allocated data just to pretend to know what I am
 * doing XD
 */
static int vdr_temps[NUM_VDR_TEMPS] = { -EINVAL, -EINVAL, -EINVAL, -EINVAL};
static int g_num_vdr_params;

static struct pwr_regs pwr_regs_bd71827 = {
	.vbat_init = BD71827_REG_VM_OCV_PRE_U,
	.vbat_init2 = BD71827_REG_VM_OCV_PST_U,
	.vbat_init3 = BD71827_REG_VM_OCV_PWRON_U,
	.used_init_regs = 3,
	.vbat_avg = BD71827_REG_VM_SA_VBAT_U,
	.ibat = BD71827_REG_CC_CURCD_U,
	.ibat_avg = BD71827_REG_CC_SA_CURCD_U,
	.meas_clear = BD71827_REG_VM_SA_MINMAX_CLR,
	.vsys_min_avg = BD71827_REG_VM_SA_VSYS_MIN_U,
	.btemp_vth = BD71827_REG_VM_BTMP,
	.chg_state = BD71827_REG_CHG_STATE,
	.coulomb3 = BD71827_REG_CC_CCNTD_3,
	.coulomb2 = BD71827_REG_CC_CCNTD_2,
	.coulomb1 = BD71827_REG_CC_CCNTD_1,
	.coulomb0 = BD71827_REG_CC_CCNTD_0,
	.coulomb_ctrl = BD71827_REG_CC_CTRL,
	.coulomb_full3 = BD71827_REG_FULL_CCNTD_3,
	.cc_full_clr = BD71827_REG_FULL_CTRL,
	.coulomb_chg3 = BD71827_REG_CCNTD_CHG_3,
	.bat_temp = BD71827_REG_BAT_TEMP,
	.dcin_stat = BD71827_REG_DCIN_STAT,
	.dcin_collapse_limit = BD71827_REG_DCIN_CLPS,
	.chg_set1 = BD71827_REG_CHG_SET1,
	.chg_en = BD71827_REG_CHG_SET1,
	.vbat_alm_limit_u = BD71827_REG_ALM_VBAT_TH_U,
	.batcap_mon_limit_u = BD71827_REG_CC_BATCAP1_TH_U,
	.conf = BD71827_REG_CONF,
	.vdcin = BD71827_REG_VM_DCIN_U,
#ifdef PWRCTRL_HACK
	.pwrctrl = BD71827_REG_PWRCTRL,
	.hibernate_mask = 0x1,
#endif
};

static struct pwr_regs pwr_regs_bd71828 = {
	.vbat_init = BD71828_REG_VBAT_INITIAL1_U,
	.vbat_init2 = BD71828_REG_VBAT_INITIAL2_U,
	.vbat_init3 = BD71828_REG_OCV_PWRON_U,
	.used_init_regs = 3,
	.vbat_avg = BD71828_REG_VBAT_U,
	.ibat = BD71828_REG_IBAT_U,
	.ibat_avg = BD71828_REG_IBAT_AVG_U,
	.meas_clear = BD71828_REG_MEAS_CLEAR,
	.vsys_min_avg = BD71828_REG_VSYS_MIN_AVG_U,
	.btemp_vth = BD71828_REG_VM_BTMP_U,
	.chg_state = BD71828_REG_CHG_STATE,
	.coulomb3 = BD71828_REG_CC_CNT3,
	.coulomb2 = BD71828_REG_CC_CNT2,
	.coulomb1 = BD71828_REG_CC_CNT1,
	.coulomb0 = BD71828_REG_CC_CNT0,
	.coulomb_ctrl = BD71828_REG_COULOMB_CTRL,
	.vbat_rex_avg = BD71828_REG_VBAT_REX_AVG_U,
	.coulomb_full3 = BD71828_REG_CC_CNT_FULL3,
	.cc_full_clr = BD71828_REG_COULOMB_CTRL2,
	.coulomb_chg3 = BD71828_REG_CC_CNT_CHG3,
	.bat_temp = BD71828_REG_BAT_TEMP,
	.dcin_stat = BD71828_REG_DCIN_STAT,
	.dcin_collapse_limit = BD71828_REG_DCIN_CLPS,
	.chg_set1 = BD71828_REG_CHG_SET1,
	.chg_en   = BD71828_REG_CHG_EN,
	.vbat_alm_limit_u = BD71828_REG_ALM_VBAT_LIMIT_U,
	.batcap_mon_limit_u = BD71828_REG_BATCAP_MON_LIMIT_U,
	.conf = BD71828_REG_CONF,
	.vdcin = BD71828_REG_VDCIN_U,
#ifdef PWRCTRL_HACK
	.pwrctrl = BD71828_REG_PS_CTRL_1,
	.hibernate_mask = 0x2,
#endif
};

static struct pwr_regs pwr_regs_bd71815 = {
	.vbat_init = BD71815_REG_VM_OCV_PRE_U,
	.vbat_init2 = BD71815_REG_VM_OCV_PST_U,
	.used_init_regs = 2,
	.vbat_avg = BD71815_REG_VM_SA_VBAT_U,
	/* BD71815 does not have separate current and current avg */
	.ibat = BD71815_REG_CC_CURCD_U,
	.ibat_avg = BD71815_REG_CC_CURCD_U,

	.meas_clear = BD71815_REG_VM_SA_MINMAX_CLR,
	.vsys_min_avg = BD71815_REG_VM_SA_VSYS_MIN_U,
	.btemp_vth = BD71815_REG_VM_BTMP,
	.chg_state = BD71815_REG_CHG_STATE,
	.coulomb3 = BD71815_REG_CC_CCNTD_3,
	.coulomb2 = BD71815_REG_CC_CCNTD_2,
	.coulomb1 = BD71815_REG_CC_CCNTD_1,
	.coulomb0 = BD71815_REG_CC_CCNTD_0,
	.coulomb_ctrl = BD71815_REG_CC_CTRL,
	.vbat_rex_avg = BD71815_REG_REX_SA_VBAT_U,
	.coulomb_full3 = BD71815_REG_FULL_CCNTD_3,
	.cc_full_clr = BD71815_REG_FULL_CTRL,
	.coulomb_chg3 = BD71815_REG_CCNTD_CHG_3,
	.bat_temp = BD71815_REG_BAT_TEMP,
	.dcin_stat = BD71815_REG_DCIN_STAT,
	.dcin_collapse_limit = BD71815_REG_DCIN_CLPS,
	.chg_set1 = BD71815_REG_CHG_SET1,
	.chg_en   = BD71815_REG_CHG_SET1,
	.vbat_alm_limit_u = BD71815_REG_ALM_VBAT_TH_U,
	.batcap_mon_limit_u = BD71815_REG_CC_BATCAP1_TH_U,
	.conf = BD71815_REG_CONF,

	.vdcin = BD71815_REG_VM_DCIN_U,
#ifdef PWRCTRL_HACK
	#error "Not implemented for BD71815"
#endif
};

/*
 * unit 0.1%
 *
 * These are the SOCs we use at zero-correction. If OCV is given via DT
 * we interpolate the OCV tables to get the OCV corresponding these SOCs.
 *
 * If VDR tables are given we will ovrride these SOCs by SOCs corresponding
 * the VDR values.
 */
static int soc_table_default[NUM_BAT_PARAMS] = {
	1000,
	1000,
	950,
	900,
	850,
	800,
	750,
	700,
	650,
	600,
	550,
	500,
	450,
	400,
	350,
	300,
	250,
	200,
	150,
	100,
	50,
	0,
	-50
};

/* Module parameters */
static int use_load_bat_params;
static int param_thr_voltage;
static int param_max_voltage;
static int param_min_voltage;

static int battery_cap_mah;

static int dgrd_cyc_cap = DGRD_CYC_CAP_DEFAULT;

static int soc_est_max_num;
static int ocv_table[NUM_BAT_PARAMS];
static int soc_table[NUM_BAT_PARAMS];
static int vdr_table_h[NUM_BAT_PARAMS];
static int vdr_table_m[NUM_BAT_PARAMS];
static int vdr_table_l[NUM_BAT_PARAMS];
static int vdr_table_vl[NUM_BAT_PARAMS];

struct bd71827_power {
	struct simple_gauge *sw;
	struct simple_gauge_desc gdesc;
	struct simple_gauge_ops ops;
	struct regmap *regmap;
	enum rohm_chip_type chip_type;
	struct device *dev;
	struct power_supply *ac;	/**< alternating current power */
	int gauge_delay;		/**< Schedule to call gauge algorithm */
	int	relax_time;		/**< Relax Time */

	struct pwr_regs *regs;
	/* Reg val to uA */
	int curr_factor;
	int rsens;
	int min_voltage;
	int max_voltage;
	int low_thr_voltage;
	int (*get_temp)(struct bd71827_power *pwr, int *temp);
	int (*bat_inserted)(struct bd71827_power *pwr);
	int battery_cap;
	struct power_supply_battery_info batinfo;
};

#define __CC_to_UAH(pwr, cc)				\
({							\
	u64 __tmp = ((u64)(cc)) * 1000000000LLU;	\
							\
	do_div(__tmp, (pwr)->rsens * 36 / 1000);	\
	__tmp;						\
})

#define CC16_to_UAH(pwe, cc) ((int)__CC_to_UAH((pwr), (cc)))
#define CC32_to_UAH(pwe, cc) ((int)(__CC_to_UAH((pwr), (cc)) >> 16))

/*
 * rsens is typically tens of Mohms so dividing by 1000 should be ok. (usual
 * values are 10 and 30 Mohms so division is likely to go even). We do this
 * to avoid doing two do_divs which would be unnecessary performance hit
 * even if this should not be time critical.
 */
#define UAH_to_CC(pwr, uah) ({			\
	u64 __tmp = (uah);			\
	u32 __rs = (pwr)->rsens / 1000;		\
	__tmp *= ((u64)__rs) * 36LLU;		\
						\
	do_div(__tmp, 1000000000);		\
	(int)__tmp;				\
})

#define CALIB_NORM			0
#define CALIB_START			1
#define CALIB_GO			2

enum {
	STAT_POWER_ON,
	STAT_INITIALIZED,
};

static int bd7182x_write16(struct bd71827_power *pwr, int reg, uint16_t val)
{
	__be16 tmp;

	tmp = cpu_to_be16(val);

	return regmap_bulk_write(pwr->regmap, reg, &tmp, sizeof(tmp));
}

static int bd7182x_read16_himask(struct bd71827_power *pwr, int reg, int himask,
				 uint16_t *val)
{
	struct regmap *regmap = pwr->regmap;
	int ret;
	__be16 rvals;
	u8 *tmp = (u8 *) &rvals;

	ret = regmap_bulk_read(regmap, reg, &rvals, sizeof(*val));
	if (!ret) {
		*tmp &= himask;
		*val = be16_to_cpu(rvals);
	}
	return ret;
}

#define MAX_INITIAL_OCV_REGS 3
/* get initial battery voltage and current */
static int bd71827_get_init_voltage(struct bd71827_power *pwr,
				     int *ocv)
{
	int ret;
	int i;
	u8 regs[MAX_INITIAL_OCV_REGS] = {
		pwr->regs->vbat_init,
		pwr->regs->vbat_init2,
		pwr->regs->vbat_init3
	 };
	uint16_t vals[MAX_INITIAL_OCV_REGS];

	*ocv = 0;

	for (i = 0; i < pwr->regs->used_init_regs; i++) {

		ret = bd7182x_read16_himask(pwr, regs[i], BD7182x_MASK_VBAT_U,
					    &vals[i]);
		if (ret) {
			dev_err(pwr->dev,
				"Failed to read initial battery voltage\n");
			return ret;
		}
		*ocv = MAX(vals[i], *ocv);

		dev_dbg(pwr->dev, "VM_OCV_%d = %d\n", i, ((int)vals[i]) * 1000);
	}

	*ocv *= 1000;

	return ret;
}

static int bd71827_get_vbat(struct bd71827_power *pwr, int *vcell)
{
	uint16_t tmp_vcell;
	int ret;

	ret = bd7182x_read16_himask(pwr, pwr->regs->vbat_avg,
				    BD7182x_MASK_VBAT_U, &tmp_vcell);
	if (ret)
		dev_err(pwr->dev, "Failed to read battery average voltage\n");
	else
		*vcell = ((int)tmp_vcell) * 1000;

	return ret;
}

static int bd71827_get_current_ds_adc(struct bd71827_power *pwr, int *curr, int *curr_avg)
{
	__be16 tmp_curr;
	char *tmp = (char *)&tmp_curr;
	int dir = 1;
	int regs[] = { pwr->regs->ibat, pwr->regs->ibat_avg };
	int *vals[] = { curr, curr_avg };
	int ret, i;

	for (dir = 1, i = 0; i < ARRAY_SIZE(regs); i++) {
		ret = regmap_bulk_read(pwr->regmap, regs[i], &tmp_curr,
				       sizeof(tmp_curr));
		if (ret)
			break;

		if (*tmp & BD7182x_MASK_CURDIR_DISCHG)
			dir = -1;

		*tmp &= BD7182x_MASK_IBAT_U;
		tmp_curr = be16_to_cpu(tmp_curr);

		*vals[i] = dir * ((int)tmp_curr) * pwr->curr_factor;
	}

	return ret;
}

static int bd71827_voltage_to_capacity(struct simple_gauge *sw, int ocv,
				       int temp __always_unused,
				       int *dsoc);

static int bd71827_voltage_to_capacity(struct simple_gauge *sw, int ocv, int temp,
				       int *dsoc)
{
	int i = 0;
	struct bd71827_power *pwr;

	/* If ocv_table is not given try luck with batinfo */
	if (!use_load_bat_params || !ocv_table[0]) {
		if (!sw)
			return -EINVAL;

		pwr = simple_gauge_get_drvdata(sw);
		*dsoc = power_supply_batinfo_ocv2dcap(&pwr->batinfo, ocv, 0);
		if (*dsoc < 0)
			return *dsoc;

		return 0;
	}

	/* Default or module param OCV table. We have NUM_BAT_PARAMS */
	if (ocv > ocv_table[0]) {
		*dsoc = soc_table[0];
	} else {
		for (i = 0; i < NUM_BAT_PARAMS; i++) {
			if ((ocv <= ocv_table[i]) && (ocv > ocv_table[i+1])) {
				*dsoc = (soc_table[i] - soc_table[i+1]) *
				      (ocv - ocv_table[i+1]) /
				      (ocv_table[i] - ocv_table[i+1]);
				*dsoc += soc_table[i+1];
				break;
			}
		}
		if (i == NUM_BAT_PARAMS)
			*dsoc = soc_table[i - 1];
	}

	return 0;
}

/* Unit is tenths of degree C */
static int bd71827_get_temp(struct simple_gauge *sw, int *temp)
{
	struct bd71827_power *pwr = simple_gauge_get_drvdata(sw);
	struct regmap *regmap = pwr->regmap;
	int ret;
	int t;

	ret = regmap_read(regmap, pwr->regs->btemp_vth, &t);
	t = 200 - t;

	if (ret || t > 200) {
		dev_err(pwr->dev, "Failed to read battery temperature\n");
		*temp = 2000;
	} else {
		*temp = t * 10;
	}

	return ret;
}

/* Unit is tenths of degree C */
static int bd71828_get_temp(struct simple_gauge *sw, int *temp)
{
	struct bd71827_power *pwr = simple_gauge_get_drvdata(sw);
	uint16_t t;
	int ret;
	int tmp = 200 * 10000;

	ret = bd7182x_read16_himask(pwr, pwr->regs->btemp_vth,
				    BD71828_MASK_VM_BTMP_U, &t);
	if (ret || t > 3200)
		dev_err(pwr->dev,
			"Failed to read system min average voltage\n");

	tmp -= 625ULL * (unsigned int)t;
	*temp = tmp / 1000;

	return ret;
}

static int bd71827_charge_status(struct bd71827_power *pwr,
				 int *s, int *h)
{
	unsigned int state;
	int status, health;
	int ret = 1;

	ret = regmap_read(pwr->regmap, pwr->regs->chg_state, &state);
	if (ret)
		dev_err(pwr->dev, "charger status reading failed (%d)\n", ret);

	state &= BD7182x_MASK_CHG_STATE;

	dev_dbg(pwr->dev, "CHG_STATE %d\n", state);

	switch (state) {
	case 0x00:
		ret = 0;
		status = POWER_SUPPLY_STATUS_DISCHARGING;
		health = POWER_SUPPLY_HEALTH_GOOD;
		break;
	case 0x01:
	case 0x02:
	case 0x03:
	case 0x0E:
		status = POWER_SUPPLY_STATUS_CHARGING;
		health = POWER_SUPPLY_HEALTH_GOOD;
		break;
	case 0x0F:
		ret = 0;
		status = POWER_SUPPLY_STATUS_FULL;
		health = POWER_SUPPLY_HEALTH_GOOD;
		break;
	case 0x10:
	case 0x11:
	case 0x12:
	case 0x13:
	case 0x14:
	case 0x20:
	case 0x21:
	case 0x22:
	case 0x23:
	case 0x24:
		ret = 0;
		status = POWER_SUPPLY_STATUS_NOT_CHARGING;
		health = POWER_SUPPLY_HEALTH_OVERHEAT;
		break;
	case 0x30:
	case 0x31:
	case 0x32:
	case 0x40:
		ret = 0;
		status = POWER_SUPPLY_STATUS_DISCHARGING;
		health = POWER_SUPPLY_HEALTH_GOOD;
		break;
	case 0x7f:
	default:
		ret = 0;
		status = POWER_SUPPLY_STATUS_NOT_CHARGING;
		health = POWER_SUPPLY_HEALTH_DEAD;
		break;
	}

	if (s)
		*s = status;
	if (h)
		*h = health;

	return ret;
}

static int __write_cc(struct bd71827_power *pwr, uint16_t bcap,
		      unsigned int reg, uint32_t *new)
{
	int ret;
	__be32 tmp;
	__be16 *swap_hi = (__be16 *)&tmp;
	uint16_t *swap_lo = swap_hi + 1;

	*swap_hi = cpu_to_be16(bcap & BD7182x_MASK_CC_CCNTD_HI);
	*swap_lo = 0;

	ret = regmap_bulk_write(pwr->regmap, reg, &tmp, sizeof(tmp));
	if (ret) {
		dev_err(pwr->dev, "Failed to write coulomb counter\n");
		return ret;
	}
	if (new)
		*new = be32_to_cpu(tmp);

	return ret;
}

static int write_cc(struct bd71827_power *pwr, uint16_t bcap)
{
	int ret;
	uint32_t new;

	ret = __write_cc(pwr, bcap, pwr->regs->coulomb3, &new);
	if (!ret)
		dev_dbg(pwr->dev, "CC set to 0x%x\n", (int)new);

	return ret;
}

static int stop_cc(struct bd71827_power *pwr)
{
	struct regmap *r = pwr->regmap;

	return regmap_update_bits(r, pwr->regs->coulomb_ctrl,
				  BD7182x_MASK_CCNTENB, 0);
}

static int start_cc(struct bd71827_power *pwr)
{
	struct regmap *r = pwr->regmap;

	return regmap_update_bits(r, pwr->regs->coulomb_ctrl,
				  BD7182x_MASK_CCNTENB, BD7182x_MASK_CCNTENB);
}

static int update_cc(struct bd71827_power *pwr, uint16_t bcap)
{
	int ret;

	ret = stop_cc(pwr);
	if (ret)
		goto err_out;

	ret = write_cc(pwr, bcap);
	if (ret)
		goto enable_out;

	ret = start_cc(pwr);
	if (ret)
		goto enable_out;

	return 0;

enable_out:
	start_cc(pwr);
err_out:
	dev_err(pwr->dev, "Coulomb counter write failed  (%d)\n", ret);
	return ret;
}

static int bd71828_set_uah(struct simple_gauge *sw, int bcap)
{
	struct bd71827_power *pwr = simple_gauge_get_drvdata(sw);
	u16 cc_val = UAH_to_CC(pwr, bcap);

	return update_cc(pwr, cc_val);
}

static int __read_cc(struct bd71827_power *pwr, u32 *cc, unsigned int reg)
{
	int ret;
	__be32 tmp_cc;

	ret = regmap_bulk_read(pwr->regmap, reg, &tmp_cc, sizeof(tmp_cc));
	if (ret) {
		dev_err(pwr->dev, "Failed to read coulomb counter\n");
		return ret;
	}
	*cc = be32_to_cpu(tmp_cc) & BD7182x_MASK_CC_CCNTD;

	return 0;
}

static int read_cc_full(struct bd71827_power *pwr, u32 *cc)
{
	return __read_cc(pwr, cc, pwr->regs->coulomb_full3);
}

static int read_cc(struct bd71827_power *pwr, u32 *cc)
{
	return __read_cc(pwr, cc, pwr->regs->coulomb3);
}

/** @brief set initial coulomb counter value from battery voltage
 * @param pwr power device
 * @return 0
 */
static int calibration_coulomb_counter(struct bd71827_power *pwr)
{
	struct regmap *regmap = pwr->regmap;
	u32 bcap;
	int soc, ocv, ret = 0, tmpret = 0;

	/* Get initial OCV */
	bd71827_get_init_voltage(pwr, &ocv);
	dev_dbg(pwr->dev, "ocv %d\n", ocv);

	if (pwr->ops.get_soc_by_ocv) {
		ret = pwr->ops.get_soc_by_ocv(NULL, ocv, 0, &soc);
	} else {
		soc = power_supply_batinfo_ocv2dcap(&pwr->batinfo, ocv, 0);
		if (soc < 0)
			return soc;
	}
	/* Get init soc from ocv/soc table */

	dev_dbg(pwr->dev, "soc %d[0.1%%]\n", soc);
	if (soc < 0)
		soc = 0;

	bcap = UAH_to_CC(pwr, pwr->battery_cap) * soc / 1000;
	write_cc(pwr, bcap + UAH_to_CC(pwr, pwr->battery_cap) / 200);

	msleep(5000);

	tmpret = write_cc(pwr, bcap);
	if (tmpret)
		goto enable_cc_out;

enable_cc_out:
	/* Start canceling offset of the DS ADC. This needs 1 second at least */
	ret = regmap_update_bits(regmap, pwr->regs->coulomb_ctrl,
				 BD7182x_MASK_CCCALIB, BD7182x_MASK_CCCALIB);

	return (tmpret) ? tmpret : ret;
}

/* This should be used to get VSYS for low limit calculations */
static int bd71827_get_vsys_min(struct simple_gauge *sw, int *uv)
{
	struct bd71827_power *pwr = simple_gauge_get_drvdata(sw);
	uint16_t tmp_vcell;
	int ret;

	ret = bd7182x_read16_himask(pwr, pwr->regs->vsys_min_avg,
				    BD7182x_MASK_VBAT_U, &tmp_vcell);
	if (ret) {
		dev_err(pwr->dev,
			"Failed to read system min average voltage\n");
		return ret;
	}
	ret = regmap_update_bits(pwr->regmap, pwr->regs->meas_clear,
				 BD718XX_MASK_VSYS_MIN_AVG_CLR,
				 BD718XX_MASK_VSYS_MIN_AVG_CLR);
	if (ret)
		dev_warn(pwr->dev, "failed to clear cached Vsys\n");

	*uv = ((int)tmp_vcell) * 1000;

	return 0;
}

/* This should be used for relax Vbat with BD71827 */
static int bd71827_get_voltage(struct simple_gauge *sg, int *vbat)
{
	int voltage, ret;
	struct bd71827_power *pwr = simple_gauge_get_drvdata(sg);

	ret = bd71827_get_vbat(pwr, &voltage);
	if (ret)
		return ret;

	*vbat = voltage;

	return 0;
}

static int bd71828_get_uah_from_full(struct simple_gauge *sw, int *from_full_uah)
{
	int ret;
	struct bd71827_power *pwr;
	struct regmap *regmap;
	u32 full_charged_coulomb_cnt;
	u32 cc;
	int diff_coulomb_cnt;

	pwr = simple_gauge_get_drvdata(sw);
	regmap = pwr->regmap;

	/*
	 * Read and clear the stored CC value from moment when battery was
	 * last charged to full.
	 */
	ret = read_cc_full(pwr, &full_charged_coulomb_cnt);
	if (ret) {
		dev_err(pwr->dev, "failed to read full coulomb counter\n");
		return ret;
	}

	ret = regmap_update_bits(regmap, pwr->regs->cc_full_clr,
					 BD7182x_MASK_CC_FULL_CLR,
					 BD7182x_MASK_CC_FULL_CLR);
	/* Get current CC value to estimate change of charge since full */
	ret = read_cc(pwr, &cc);
	if (ret)
		return ret;

	diff_coulomb_cnt = full_charged_coulomb_cnt - cc;

	diff_coulomb_cnt >>= 16;

	/*
	 * Ignore possible increase in CC which can be caused by ADC offset or
	 * temperature change
	 */
	if (diff_coulomb_cnt > 0)
		diff_coulomb_cnt = 0;

	*from_full_uah = CC16_to_UAH(pwr, diff_coulomb_cnt);

	return 0;
}

static int bd71828_get_uah(struct simple_gauge *sw, int *uah)
{
	struct bd71827_power *pwr = simple_gauge_get_drvdata(sw);
	u32 cc;
	int ret;

	ret = read_cc(pwr, &cc);
	if (!ret)
		*uah = CC32_to_UAH(pwr, cc);

	return ret;
}

/*
 * Standard batinfo supports only accuracy of 1% for SOC - which
 * may not be sufficient for us. SWGAUGE provides soc in unts of 0.1% here
 * to allow more accurate computation.
 */
static int bd71827_get_ocv(struct simple_gauge *sw, int dsoc, int temp, int *ocv)
{
	int i = 0;
	struct bd71827_power *pwr;

	/* If soc_table is not given try luck with batinfo */
	if (!use_load_bat_params || !ocv_table[0]) {
		if (!sw)
			return -EINVAL;

		pwr = simple_gauge_get_drvdata(sw);
		*ocv = power_supply_batinfo_dcap2ocv(&pwr->batinfo, dsoc, temp);
		if (*ocv < 0)
			return *ocv;

		return 0;
	}

	/* Default or module param OCV table. We have NUM_BAT_PARAMS */

	if (dsoc > soc_table[0]) {
		*ocv = pwr->max_voltage;
		return 0;
	}
	if (dsoc == 0) {
		*ocv = ocv_table[NUM_BAT_PARAMS - 2];
		return 0;
	}

	i = 0;
	while (i < NUM_BAT_PARAMS - 1) {
		if ((dsoc <= soc_table[i]) && (dsoc > soc_table[i+1])) {
			*ocv = (ocv_table[i] - ocv_table[i+1]) *
			       (dsoc - soc_table[i+1]) / (soc_table[i] -
			       soc_table[i+1]) + ocv_table[i+1];
			return 0;
		}
		i++;
	}

	*ocv = ocv_table[NUM_BAT_PARAMS - 1];

	return 0;
}

static void calc_vdr(int *res, int *vdr, int temp, int dgrd_temp,
		     int *vdr_hi, int dgrd_temp_hi, int items)
{
	int i;

	/* Get VDR weighed by temperature */
	for (i = 0; i < items; i++)
		res[i] = LINEAR_INTERPOLATE(vdr_hi[i], vdr[i], dgrd_temp_hi,
					    dgrd_temp, temp);
}

/* get VDR(Voltage Drop Rate) value by SOC */
static int bd71827_get_vdr(struct bd71827_power *pwr, int dsoc, int temp)
{
	int i = 0;
	int vdr = 100;
	int vdr_table[NUM_BAT_PARAMS];

	/* Calculate VDR by temperature */
	if (temp >= vdr_temps[VDR_TEMP_HIGH])
		for (i = 0; i < g_num_vdr_params; i++)
			vdr_table[i] = vdr_table_h[i];
	else if (temp >= vdr_temps[VDR_TEMP_NORMAL])
		calc_vdr(vdr_table, vdr_table_m, temp, vdr_temps[VDR_TEMP_NORMAL],
			 vdr_table_h, vdr_temps[VDR_TEMP_HIGH],
			 g_num_vdr_params);
	else if (temp >= vdr_temps[VDR_TEMP_LOW])
		calc_vdr(vdr_table, vdr_table_l, temp, vdr_temps[VDR_TEMP_LOW],
			 vdr_table_m, vdr_temps[VDR_TEMP_NORMAL],
			 g_num_vdr_params);
	else if (temp >= vdr_temps[VDR_TEMP_VERY_LOW])
		calc_vdr(vdr_table, vdr_table_vl, temp,
			 vdr_temps[VDR_TEMP_VERY_LOW], vdr_table_l,
			 vdr_temps[VDR_TEMP_LOW], g_num_vdr_params);
	else
		for (i = 0; i < g_num_vdr_params; i++)
			vdr_table[i] = vdr_table_vl[i];

	if (dsoc > soc_table[0]) {
		vdr = 100;
	} else if (dsoc == 0) {
		vdr = vdr_table[g_num_vdr_params - 1];
	} else {
		for (i = 0; i < g_num_vdr_params - 1; i++)
			if ((dsoc <= soc_table[i]) && (dsoc > soc_table[i+1])) {
				vdr = LINEAR_INTERPOLATE(vdr_table[i],
							 vdr_table[i+1],
							 soc_table[i],
							 soc_table[i+1], dsoc);

				break;
			}
		if (i == g_num_vdr_params - 1)
			vdr = vdr_table[i];
	}
	dev_dbg(pwr->dev, "vdr = %d\n", vdr);
	return vdr;
}

static int bd71828_zero_correct(struct simple_gauge *sw, int *effective_cap,
				int cc_uah, int vbat, int temp)
{
	int ocv_table_load[NUM_BAT_PARAMS];
	int i, ret;
	/* Assume fixed-size module param table */
	static int params = NUM_BAT_PARAMS;
	int ocv;
	int dsoc;
	struct bd71827_power *pwr = simple_gauge_get_drvdata(sw);

	/*
	 * Calculate SOC from CC and effective battery cap.
	 * Use unit of 0.1% for dsoc to improve accuracy
	 */
	dsoc = CAP2DSOC(cc_uah, *effective_cap);
	dev_dbg(pwr->dev, "dsoc = %d\n", dsoc);

	ret = bd71827_get_ocv(sw, dsoc, 0, &ocv);
	if (ret)
		return ret;

	if (!ocv_table[0]) {
		for (i = 0; i < g_num_vdr_params; i++)
			ocv_table[i] = power_supply_batinfo_dcap2ocv(&pwr->batinfo,
								     soc_table[i], temp);
		/*
		 * Update amount of OCV values id we didn't have the fixed size
		 * module param table
		 */
		params = g_num_vdr_params;
	}
	for (i = 1; i < params; i++) {
		ocv_table_load[i] = ocv_table[i] - (ocv - vbat);
		if (ocv_table_load[i] <= pwr->min_voltage) {
			dev_dbg(pwr->dev, "ocv_table_load[%d] = %d\n", i,
				ocv_table_load[i]);
			break;
		}
	}

	/*
	 * For improved accuracy ROHM helps customers to measure some
	 * battery voltage drop curves to do further SOC estimation improvement.
	 * If VDR tables are available we perform these corrections.
	 */
	if (i < params) {
		int zero_soc_pos;
		int j, k, m;
		int dv;
		int lost_cap, new_lost_cap;
		int dsoc0;
		int vdr, vdr0;
		int soc_range;

		/*
		 * The original ROHM algorithm had fixed amount of OCV and VDR
		 * values. The quiet expectation of the algorithm was that the
		 * second last value in these tables correspond zero SOC. In
		 * order to relax this assumption when values come from DT we
		 * try to scan the SOC table for zero SOC.
		 */
		for (zero_soc_pos = params - 1; zero_soc_pos >= 0;
		     zero_soc_pos--)
			if (soc_table[zero_soc_pos] >= 0)
				break;

		if (soc_table[zero_soc_pos])
			dev_warn_once(pwr->dev,
				      "VDR/OCV: zero SOC not found\n");

		/*
		 * We want to know the zero soc position from the last entry
		 * in SOC table so that we know where the fully depleted cap
		 * is met.
		 */
		zero_soc_pos = params - zero_soc_pos;

		soc_range = (soc_table[i - 1] - soc_table[i]) / 10;
		if (soc_range < 1) {
			dev_err_once(pwr->dev, "Bad SOC table values %u, %u\n",
				soc_table[i - 1], soc_table[i]);
			return -EINVAL;
		}
		dv = (ocv_table_load[i - 1] - ocv_table_load[i]) / soc_range; /* was hard coded 5 */
		for (j = 1; j < soc_range/* was 5 */; j++) {
			if ((ocv_table_load[i] + dv * j) >
			    pwr->min_voltage) {
				break;
			}
		}

		lost_cap = ((params - zero_soc_pos - i) * soc_range /* was 5 */ +
			   (j - 1)) * *effective_cap / 100;
		dev_dbg(pwr->dev, "lost_cap-1 = %d\n", lost_cap);

		for (m = 0; m < soc_est_max_num; m++) {
			new_lost_cap = lost_cap;
			dsoc0 = CAP2DSOC(lost_cap, *effective_cap);
			if ((dsoc >= 0 && dsoc0 > dsoc) ||
			    (dsoc < 0 && dsoc0 < dsoc))
				dsoc0 = dsoc;

			dev_dbg(pwr->dev, "dsoc0(%d) = %d\n", m, dsoc0);

			vdr = bd71827_get_vdr(pwr, dsoc, temp);
			vdr0 = bd71827_get_vdr(pwr, dsoc0, temp);

			for (k = 1; k < params; k++) {
				if (!vdr) {
					dev_err(pwr->dev,
						"VDR zero-correction failed\n");
					break;
				}
				ocv_table_load[k] = ocv_table[k] -
						    (ocv - vbat) * vdr0 / vdr;
				if (ocv_table_load[k] <= pwr->min_voltage) {
					dev_dbg(pwr->dev,
						"ocv_table_load[%d] = %d\n",  k,
						ocv_table_load[k]);
					break;
				}
			}
			if (k < params) {
				dv = (ocv_table_load[k-1] -
				     ocv_table_load[k]) / 5;
				for (j = 1; j < 5; j++)
					if ((ocv_table_load[k] + dv * j) >
					     pwr->min_voltage)
						break;

				new_lost_cap = ((params - zero_soc_pos - k) *
						 5 + (j - 1)) *
						*effective_cap / 100;
				if (soc_est_max_num == 1)
					lost_cap = new_lost_cap;
				else
					lost_cap += (new_lost_cap - lost_cap) /
						    (2 * (soc_est_max_num - m));
				dev_dbg(pwr->dev, "lost_cap-2(%d) = %d\n", m,
					lost_cap);
			}
			if (new_lost_cap == lost_cap)
				break;
		}

		*effective_cap -= lost_cap;
	}

	return 0;
}

static int get_chg_online(struct bd71827_power *pwr, int *chg_online)
{
	int r, ret;

	ret = regmap_read(pwr->regmap, pwr->regs->dcin_stat, &r);
	if (ret) {
		dev_err(pwr->dev, "Failed to read DCIN status\n");
		return ret;
	}
	*chg_online = ((r & BD7182x_MASK_DCIN_DET) != 0);

	return 0;
}

static int get_bat_online(struct bd71827_power *pwr, int *bat_online)
{
	int r, ret;

#define BAT_OPEN	0x7
	ret = regmap_read(pwr->regmap, pwr->regs->bat_temp, &r);
	if (ret) {
		dev_err(pwr->dev, "Failed to read battery temperature\n");
		return ret;
	}
	*bat_online = ((r & BD7182x_MASK_BAT_TEMP) != BAT_OPEN);

	return 0;
}

static int bd71828_bat_inserted(struct bd71827_power *pwr)
{
	int ret, val;

	ret = regmap_read(pwr->regmap, pwr->regs->conf, &val);
	if (ret) {
		dev_err(pwr->dev, "Failed to read CONF register\n");
		return 0;
	}
	ret = val & BD7182x_MASK_CONF_PON;

	if (ret)
		regmap_update_bits(pwr->regmap, pwr->regs->conf,
				   BD7182x_MASK_CONF_PON, 0);

	return ret;
}

static int bd71815_bat_inserted(struct bd71827_power *pwr)
{
	int ret, val;

	ret = regmap_read(pwr->regmap, pwr->regs->conf, &val);
	if (ret) {
		dev_err(pwr->dev, "Failed to read CONF register\n");
		return ret;
	}

	ret = !(val & BD71815_MASK_CONF_XSTB);
	if (ret)
		regmap_write(pwr->regmap, pwr->regs->conf,  val |
			     BD71815_MASK_CONF_XSTB);

	return ret;
}

static int bd71827_init_hardware(struct bd71827_power *pwr)
{
	int ret;

	/* TODO: Collapse limit should come from device-tree ? */
	ret = regmap_write(pwr->regmap, pwr->regs->dcin_collapse_limit,
			   BD7182x_DCIN_COLLAPSE_DEFAULT);
	if (ret) {
		dev_err(pwr->dev, "Failed to write DCIN collapse limit\n");
		return ret;
	}

	ret = pwr->bat_inserted(pwr);
	if (ret < 0)
		return ret;

	if (ret) {
		int cc_val;

		/* Ensure Coulomb Counter is stopped */
		ret = stop_cc(pwr);
		if (ret)
			return ret;

		/* Set Coulomb Counter Reset bit*/
		ret = regmap_update_bits(pwr->regmap, pwr->regs->coulomb_ctrl,
					 BD7182x_MASK_CCNTRST,
					 BD7182x_MASK_CCNTRST);
		if (ret)
			return ret;

		/* Clear Coulomb Counter Reset bit*/
		ret = regmap_update_bits(pwr->regmap, pwr->regs->coulomb_ctrl,
					 BD7182x_MASK_CCNTRST, 0);
		if (ret)
			return ret;

		/* Set initial Coulomb Counter by HW OCV */
		calibration_coulomb_counter(pwr);

		/* WDT_FST auto set */
		ret = regmap_update_bits(pwr->regmap, pwr->regs->chg_set1,
					 BD7182x_MASK_WDT_AUTO,
					 BD7182x_MASK_WDT_AUTO);
		if (ret)
			return ret;

		ret = bd7182x_write16(pwr, pwr->regs->vbat_alm_limit_u,
				      VBAT_LOW_TH);
		if (ret)
			return ret;

		/* Set monitor threshold to 9/10 of battery uAh capacity */
		cc_val = UAH_to_CC(pwr, pwr->battery_cap);

		/*
		 * On BD71815 "we mask the power-state" from relax detection.
		 * I am unsure what the impact of the power-state would be if
		 * we didn't - but this is what the vendor driver did - and
		 * that driver has been used in few projects so I just assume
		 * this is needed.
		 */
		if (pwr->chip_type == ROHM_CHIP_TYPE_BD71815) {
			ret = regmap_set_bits(pwr->regmap,
					      BD71815_REG_REX_CTRL_1,
					      REX_PMU_STATE_MASK);
			if (ret)
				return ret;
		}

		ret = bd7182x_write16(pwr, pwr->regs->batcap_mon_limit_u,
				      cc_val * 9 / 10);
		if (ret)
			return ret;

		dev_dbg(pwr->dev, "REG_CC_BATCAP1_TH = %d\n",
			(cc_val * 9 / 10));
	}

	return start_cc(pwr);
}

#define MK_2_100MCELSIUS(m_kevl_in) (((int)(m_kevl_in) - 273150) / 100)
static int get_vdr_from_dt(struct bd71827_power *pwr,
			   struct fwnode_handle *node, int temp_values)
{
	int i, ret, num_values, *tmp_table;
	u32 vdr_kelvin[NUM_VDR_TEMPS];

	if (temp_values != NUM_VDR_TEMPS) {
		dev_err(pwr->dev, "Bad VDR temperature table size (%d). Expected %d",
			temp_values, NUM_VDR_TEMPS);
		return -EINVAL;
	}
	ret = fwnode_property_read_u32_array(node,
					    "rohm,volt-drop-temp-millikelvin",
					    &vdr_kelvin[0], NUM_VDR_TEMPS);
	if (ret) {
		dev_err(pwr->dev, "Invalid VDR temperatures in device-tree");
		return ret;
	}
	/* Convert millikelvin to .1 celsius as is expected by VDR algo */
	for (i = 0; i < NUM_VDR_TEMPS; i++)
		vdr_temps[i] = MK_2_100MCELSIUS(vdr_kelvin[i]);

	num_values = fwnode_property_count_u32(node, "rohm,volt-drop-soc");
	if (num_values <= 0 || num_values > MAX_NUM_VDR_VALUES) {
		dev_err(pwr->dev, "malformed voltage drop parameters\n");
		return -EINVAL;
	}
	g_num_vdr_params = num_values;

	tmp_table = kcalloc(num_values, sizeof(int), GFP_KERNEL);
	if (!tmp_table)
		return -ENOMEM;
	/*
	 * We collect NUM_VDR_TEMPS + 1 tables, all temperature tables +
	 * the SOC table
	 */
	for (i = 0; i < NUM_VDR_TEMPS + 1; i++) {
		int index;
		static const char * const prop[] = {
			/*
			 * SOC in units of 0.1 percent. TODO: Check if we have
			 * standard DT unit for percentage with higher accuracy
			 */
			"rohm,volt-drop-soc",
			"rohm,volt-drop-high-temp-microvolt",
			"rohm,volt-drop-normal-temp-microvolt",
			"rohm,volt-drop-low-temp-microvolt",
			"rohm,volt-drop-very-low-temp-microvolt",
		};
		int *tables[5] = {
			&soc_table[0], &vdr_table_h[0], &vdr_table_m[0],
			&vdr_table_l[0], &vdr_table_vl[0]
		};

		if (num_values != fwnode_property_count_u32(node, prop[i])) {
			dev_err(pwr->dev,
				"%s: Bad size. Expected %d parameters",
				prop[i], num_values);
			ret = -EINVAL;
			goto out;
		}
		ret = fwnode_property_read_u32_array(node, prop[i], tmp_table,
						     num_values);
		if (ret) {
			dev_err(pwr->dev,
				"Invalid VDR temperatures in device-tree");
			goto out;
		}
		for (index = 0; index < num_values; index++)
			tables[i][index] = tmp_table[index];
	}
out:
	kfree(tmp_table);

	return ret;
}

 /* Set default parameters if no module parameters were given */
static int bd71827_set_battery_parameters(struct bd71827_power *pwr)
{
	int i;

	/*
	 * We support getting battery parameters from static battery node
	 * or as module parameters.
	 */
	if (!use_load_bat_params) {
		int ret, num_vdr;
		struct fwnode_handle *node;

		/*
		 * power_supply_dev_get_battery_info uses devm internally
		 * so we do not need explicit remove()
		 */
		ret = power_supply_dev_get_battery_info(pwr->dev->parent, NULL,
							&pwr->batinfo);
		if (ret) {
			dev_err(pwr->dev, "No battery information (%d)\n", ret);
			return ret;
		}

		if (!pwr->batinfo.ocv_table[0]) {
			dev_err(pwr->dev, "No Open Circuit Voltages for battery\n");
			return -EINVAL;
		}

		if (pwr->batinfo.charge_full_design_uah == -EINVAL) {
			dev_err(pwr->dev, "Unknown battery capacity\n");
			return -EINVAL;
		}

		if (pwr->batinfo.voltage_max_design_uv == -EINVAL) {
			/*
			 * We could try digging this from OCV table but
			 * lets just bail-out for now
			 */
			dev_err(pwr->dev, "Unknown max voltage\n");
			return -EINVAL;
		}
		pwr->max_voltage = pwr->batinfo.voltage_max_design_uv;

		if (pwr->batinfo.voltage_min_design_uv == -EINVAL) {
			/* We could try digging this from OCV table....? */
			dev_err(pwr->dev, "Unknown max voltage\n");
			return -EINVAL;
		}
		pwr->min_voltage = pwr->batinfo.voltage_min_design_uv;
		/*
		 * Let's default the zero-correction limit to 10%
		 * voltage limit. TODO: See what would be the correct value
		 */
		pwr->battery_cap = pwr->batinfo.charge_full_design_uah;
		pwr->gdesc.degrade_cycle_uah = pwr->batinfo.degrade_cycle_uah;

		soc_est_max_num = SOC_EST_MAX_NUM_DEFAULT;

		node = dev_fwnode(pwr->dev->parent);
		if (!node) {
			dev_err(pwr->dev, "no charger node\n");
			return -ENODEV;
		}
		node = fwnode_find_reference(node, "monitored-battery", 0);
		if (IS_ERR(node)) {
			dev_err(pwr->dev, "No battery node found\n");
			return PTR_ERR(node);
		}

		num_vdr = fwnode_property_count_u32(node, "rohm,volt-drop-temp-millikelvin");
		if (num_vdr > 0) {
			ret = get_vdr_from_dt(pwr, node, num_vdr);
			if (ret)
				return ret;
		} else {
			vdr_temps[VDR_TEMP_HIGH] = DGRD_TEMP_H_DEFAULT;
			vdr_temps[VDR_TEMP_NORMAL] = DGRD_TEMP_M_DEFAULT;
			vdr_temps[VDR_TEMP_LOW] = DGRD_TEMP_L_DEFAULT;
			vdr_temps[VDR_TEMP_VERY_LOW] = DGRD_TEMP_VL_DEFAULT;
		}
	} else {
		if (vdr_temps[VDR_TEMP_HIGH] == -EINVAL ||
		    vdr_temps[VDR_TEMP_NORMAL] == -EINVAL ||
		    vdr_temps[VDR_TEMP_LOW] == -EINVAL ||
		    vdr_temps[VDR_TEMP_VERY_LOW] == -EINVAL) {
			vdr_temps[VDR_TEMP_HIGH] = DGRD_TEMP_H_DEFAULT;
			vdr_temps[VDR_TEMP_NORMAL] = DGRD_TEMP_M_DEFAULT;
			vdr_temps[VDR_TEMP_LOW] = DGRD_TEMP_L_DEFAULT;
			vdr_temps[VDR_TEMP_VERY_LOW] = DGRD_TEMP_VL_DEFAULT;
		}

		pwr->min_voltage = param_max_voltage;
		pwr->max_voltage = param_min_voltage;
		pwr->low_thr_voltage = param_thr_voltage;
		pwr->battery_cap = battery_cap_mah * 1000;
		pwr->gdesc.degrade_cycle_uah = dgrd_cyc_cap;
	}
	/* SOC table is expected to be in descending order. */
	if (!soc_table[0])
		for (i = 0; i < NUM_BAT_PARAMS; i++)
			soc_table[i] = soc_table_default[i];

	if (!pwr->min_voltage || !pwr->max_voltage || !pwr->battery_cap) {
		dev_err(pwr->dev, "Battery parameters missing\n");

		return -EINVAL;
	}
	if (!pwr->low_thr_voltage)
		pwr->low_thr_voltage = pwr->min_voltage +
			(pwr->max_voltage - pwr->min_voltage) / 10;
	return 0;
}

static int bd71827_charger_get_property(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	struct bd71827_power *pwr = dev_get_drvdata(psy->dev.parent);
	u32 vot;
	uint16_t tmp;
	int online;
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		ret = get_chg_online(pwr, &online);
		if (!ret)
			val->intval = online;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = bd7182x_read16_himask(pwr, pwr->regs->vdcin,
					    BD7182x_MASK_VDCIN_U, &tmp);
		if (ret)
			return ret;

		vot = tmp;
		/* 5 milli volt steps */
		val->intval = 5000 * vot;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int bd71827_battery_get_property(struct simple_gauge *gauge,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	struct bd71827_power *pwr = simple_gauge_get_drvdata(gauge);
	int ret = 0;
	int status, health, tmp, curr, curr_avg;

	if (psp == POWER_SUPPLY_PROP_STATUS || psp == POWER_SUPPLY_PROP_HEALTH
	    || psp == POWER_SUPPLY_PROP_CHARGE_TYPE)
		ret = bd71827_charge_status(pwr, &status, &health);
	else if (psp == POWER_SUPPLY_PROP_CURRENT_AVG ||
		 psp == POWER_SUPPLY_PROP_CURRENT_NOW)
		ret = bd71827_get_current_ds_adc(pwr, &curr, &curr_avg);
	if (ret)
		return ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = status;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = health;
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		if (status == POWER_SUPPLY_STATUS_CHARGING)
			val->intval = POWER_SUPPLY_CHARGE_TYPE_FAST;
		else
			val->intval = POWER_SUPPLY_CHARGE_TYPE_NONE;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
	case POWER_SUPPLY_PROP_PRESENT:
		ret = get_bat_online(pwr, &tmp);
		if (!ret)
			val->intval = tmp;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = bd71827_get_vbat(pwr, &tmp);
		val->intval = tmp;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		val->intval = curr_avg;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = curr;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		val->intval = pwr->max_voltage;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
		val->intval = pwr->min_voltage;
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val->intval = MAX_CURRENT_DEFAULT;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

/** @brief ac properties */
static enum power_supply_property bd71827_charger_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
};

static enum power_supply_property bd71827_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_MIN,
	POWER_SUPPLY_PROP_CURRENT_MAX,
};

static ssize_t charging_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf,
					   size_t count)
{
	struct bd71827_power *pwr = dev_get_drvdata(dev->parent);
	ssize_t ret = 0;
	unsigned long val;

	ret = kstrtoul(buf, 0, &val);
	if (ret)
		return ret;

	if (val != 0 && val != 1) {
		dev_warn(dev, "use 0/1 to disable/enable charging\n");
		return -EINVAL;
	}

	if (val)
		ret = regmap_update_bits(pwr->regmap, pwr->regs->chg_en,
					 BD7182x_MASK_CHG_EN,
					 BD7182x_MASK_CHG_EN);
	else
		ret = regmap_update_bits(pwr->regmap, pwr->regs->chg_en,
					 BD7182x_MASK_CHG_EN,
					 0);
	if (ret)
		return ret;

	return count;
}

static ssize_t charging_show(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct bd71827_power *pwr = dev_get_drvdata(dev->parent);
	int chg_en, chg_online, ret;

	ret = regmap_read(pwr->regmap, pwr->regs->chg_en, &chg_en);
	if (ret)
		return ret;

	ret = get_chg_online(pwr, &chg_online);
	if (ret)
		return ret;

	chg_en &= BD7182x_MASK_CHG_EN;
	return sprintf(buf, "%x\n", chg_online && chg_en);
}

static DEVICE_ATTR_RW(charging);

static struct attribute *bd71827_sysfs_attributes[] = {
	&dev_attr_charging.attr, NULL,
};

static const struct attribute_group bd71827_sysfs_attr_group = {
	.attrs = bd71827_sysfs_attributes,
};

static const struct attribute_group *bd71827_sysfs_attr_groups[] = {
	&bd71827_sysfs_attr_group, NULL,
};

/** @brief powers supplied by bd71827_ac */
static char *bd71827_ac_supplied_to[] = {
	BAT_NAME,
};

static const struct power_supply_desc bd71827_ac_desc = {
	.name		= AC_NAME,
	.type		= POWER_SUPPLY_TYPE_MAINS,
	.properties	= bd71827_charger_props,
	.num_properties	= ARRAY_SIZE(bd71827_charger_props),
	.get_property	= bd71827_charger_get_property,
};

static const struct simple_gauge_psy gauge_psy_config = {
	.psy_name = BAT_NAME,
	.additional_props = bd71827_battery_props,
	.num_additional_props = ARRAY_SIZE(bd71827_battery_props),
	.get_custom_property = bd71827_battery_get_property,
};
#ifdef PWRCTRL_HACK
/*
 * This is not-so-pretty hack for allowing external code to call
 * bd71827_chip_hibernate() without this power device-data
 */
static struct bd71827_power *hack;
static DEFINE_SPINLOCK(pwrlock);

static struct bd71827_power *get_power(void)
{
	mutex_lock(&pwrlock);
	if (!hack) {
		mutex_unlock(&pwrlock);
		return -ENOENT;
	}
	return hack;
}

static void put_power(void)
{
	mutex_unlock(&pwrlock);
}

static int set_power(struct bd71827_power *pwr)
{
	mutex_lock(&pwrlock);
	hack = pwr;
	mutex_unlock(&pwrlock);
}

static void free_power(void)
{
	mutex_lock(&pwrlock);
	hack = NULL;
	mutex_unlock(&pwrlock);
}

/*
 * TODO: Find the corret way to do this
 */
void bd71827_chip_hibernate(void)
{
	struct bd71827_power *pwr = get_power();

	if (IS_ERR(pwr)) {
		pr_err("%s called before probe finished\n", __func__);
		return PTR_ERR(pwr);
	}

	/* programming sequence in EANAB-151 */
	regmap_update_bits(pwr->regmap, pwr->regs->pwrctrl,
			   pwr->regs->hibernate_mask, 0);
	regmap_update_bits(pwr->regmap, pwr->regs->pwrctrl,
			   pwr->regs->hibernate_mask,
			   pwr->regs->hibernate_mask);
	put_power();
}
#endif

#define RSENS_CURR 10000000000LLU

#define BD_ISR_NAME(name) \
bd7181x_##name##_isr

#define BD_ISR_BAT(name, print, run_gauge)				\
static irqreturn_t BD_ISR_NAME(name)(int irq, void *data)		\
{									\
	struct bd71827_power *pwr = (struct bd71827_power *)data;	\
									\
	if (run_gauge)							\
		simple_gauge_run(pwr->sw);				\
	dev_dbg(pwr->dev, "%s\n", print);				\
	power_supply_changed(pwr->sw->psy);				\
									\
	return IRQ_HANDLED;						\
}

#define BD_ISR_AC(name, print, run_gauge)				\
static irqreturn_t BD_ISR_NAME(name)(int irq, void *data)		\
{									\
	struct bd71827_power *pwr = (struct bd71827_power *)data;	\
									\
	if (run_gauge)							\
		simple_gauge_run(pwr->sw);				\
	power_supply_changed(pwr->ac);					\
	dev_dbg(pwr->dev, "%s\n", print);				\
	power_supply_changed(pwr->sw->psy);				\
									\
	return IRQ_HANDLED;						\
}

#define BD_ISR_DUMMY(name, print)					\
static irqreturn_t BD_ISR_NAME(name)(int irq, void *data)		\
{									\
	struct bd71827_power *pwr = (struct bd71827_power *)data;	\
									\
	dev_dbg(pwr->dev, "%s\n", print);				\
									\
	return IRQ_HANDLED;						\
}

BD_ISR_BAT(chg_state_changed, "CHG state changed", true)
/* DCIN voltage changes */
BD_ISR_AC(dcin_removed, "DCIN removed", true)
BD_ISR_AC(clps_out, "DCIN voltage back to normal", true)
BD_ISR_AC(clps_in, "DCIN voltage collapsed", false)
BD_ISR_AC(dcin_ovp_res, "DCIN voltage normal", true)
BD_ISR_AC(dcin_ovp_det, "DCIN OVER VOLTAGE", true)

BD_ISR_DUMMY(dcin_mon_det, "DCIN voltage below threshold")
BD_ISR_DUMMY(dcin_mon_res, "DCIN voltage above threshold")

BD_ISR_DUMMY(vsys_uv_res, "VSYS under-voltage cleared")
BD_ISR_DUMMY(vsys_uv_det, "VSYS under-voltage")
BD_ISR_DUMMY(vsys_low_res, "'VSYS low' cleared")
BD_ISR_DUMMY(vsys_low_det, "VSYS low")
BD_ISR_DUMMY(vsys_mon_res, "VSYS mon - resumed")
BD_ISR_DUMMY(vsys_mon_det, "VSYS mon - detected")
BD_ISR_BAT(chg_wdg_temp, "charger temperature watchdog triggered", true)
BD_ISR_BAT(chg_wdg, "charging watchdog triggered", true)
BD_ISR_BAT(bat_removed, "Battery removed", true)
BD_ISR_BAT(bat_det, "Battery detected", true)
/* TODO: Verify the meaning of these interrupts */
BD_ISR_BAT(rechg_det, "Recharging", true)
BD_ISR_BAT(rechg_res, "Recharge ending", true)
BD_ISR_DUMMY(temp_transit, "Temperature transition")
BD_ISR_BAT(therm_rmv, "bd71815-therm-rmv", false)
BD_ISR_BAT(therm_det, "bd71815-therm-det", true)
BD_ISR_BAT(bat_dead, "bd71815-bat-dead", false)
BD_ISR_BAT(bat_short_res, "bd71815-bat-short-res", true)
BD_ISR_BAT(bat_short, "bd71815-bat-short-det", false)
BD_ISR_BAT(bat_low_res, "bd71815-bat-low-res", true)
BD_ISR_BAT(bat_low, "bd71815-bat-low-det", true)
BD_ISR_BAT(bat_ov_res, "bd71815-bat-over-res", true)
/* What should we do here? */
BD_ISR_BAT(bat_ov, "bd71815-bat-over-det", false)
BD_ISR_BAT(bat_mon_res, "bd71815-bat-mon-res", true)
BD_ISR_BAT(bat_mon, "bd71815-bat-mon-det", true)
BD_ISR_BAT(bat_cc_mon, "bd71815-bat-cc-mon2", false)
BD_ISR_BAT(bat_oc1_res, "bd71815-bat-oc1-res", true)
BD_ISR_BAT(bat_oc1, "bd71815-bat-oc1-det", false)
BD_ISR_BAT(bat_oc2_res, "bd71815-bat-oc2-res", true)
BD_ISR_BAT(bat_oc2, "bd71815-bat-oc2-det", false)
BD_ISR_BAT(bat_oc3_res, "bd71815-bat-oc3-res", true)
BD_ISR_BAT(bat_oc3, "bd71815-bat-oc3-det", false)
BD_ISR_BAT(temp_bat_low_res, "bd71815-temp-bat-low-res", true)
BD_ISR_BAT(temp_bat_low, "bd71815-temp-bat-low-det", true)
BD_ISR_BAT(temp_bat_hi_res, "bd71815-temp-bat-hi-res", true)
BD_ISR_BAT(temp_bat_hi, "bd71815-temp-bat-hi-det", true)

static irqreturn_t bd7182x_dcin_removed(int irq, void *data)
{
	struct bd71827_power *pwr = (struct bd71827_power *)data;

	simple_gauge_run(pwr->sw);
	power_supply_changed(pwr->ac);
	dev_dbg(pwr->dev, "DCIN removed\n");

	return IRQ_HANDLED;
}

static irqreturn_t bd718x7_chg_done(int irq, void *data)
{
	struct bd71827_power *pwr = (struct bd71827_power *)data;

	/*
	 * Battery is likely to be FULL => run simple_gauge to initiate
	 * CC setting
	 */
	simple_gauge_run(pwr->sw);

	return IRQ_HANDLED;
}

static irqreturn_t bd7182x_dcin_detected(int irq, void *data)
{
	struct bd71827_power *pwr = (struct bd71827_power *)data;

	dev_dbg(pwr->dev, "DCIN inserted\n");
	power_supply_changed(pwr->ac);

	return IRQ_HANDLED;
}

static irqreturn_t bd71827_vbat_low_res(int irq, void *data)
{
	struct bd71827_power *pwr = (struct bd71827_power *)data;

	dev_dbg(pwr->dev, "VBAT LOW Resumed\n");
	return IRQ_HANDLED;
}

static irqreturn_t bd71827_vbat_low_det(int irq, void *data)
{
	struct bd71827_power *pwr = (struct bd71827_power *)data;

	dev_dbg(pwr->dev, "VBAT LOW Detected\n");

	return IRQ_HANDLED;
}

static irqreturn_t bd71827_temp_bat_hi_det(int irq, void *data)
{
	struct bd71827_power *pwr = (struct bd71827_power *)data;

	dev_warn(pwr->dev, "Overtemp Detected\n");
	power_supply_changed(pwr->sw->psy);

	return IRQ_HANDLED;
}

static irqreturn_t bd71827_temp_bat_hi_res(int irq, void *data)
{
	struct bd71827_power *pwr = (struct bd71827_power *)data;

	dev_dbg(pwr->dev, "Overtemp Resumed\n");
	power_supply_changed(pwr->sw->psy);

	return IRQ_HANDLED;
}

static irqreturn_t bd71827_temp_bat_low_det(int irq, void *data)
{
	struct bd71827_power *pwr = (struct bd71827_power *)data;

	dev_dbg(pwr->dev, "Lowtemp Detected\n");
	power_supply_changed(pwr->sw->psy);

	return IRQ_HANDLED;
}

static irqreturn_t bd71827_temp_bat_low_res(int irq, void *data)
{
	struct bd71827_power *pwr = (struct bd71827_power *)data;

	dev_dbg(pwr->dev, "Lowtemp Resumed\n");
	power_supply_changed(pwr->sw->psy);

	return IRQ_HANDLED;
}

static irqreturn_t bd71827_temp_vf_det(int irq, void *data)
{
	struct bd71827_power *pwr = (struct bd71827_power *)data;

	dev_dbg(pwr->dev, "VF Detected\n");
	power_supply_changed(pwr->sw->psy);

	return IRQ_HANDLED;
}

static irqreturn_t bd71827_temp_vf_res(int irq, void *data)
{
	struct bd71827_power *pwr = (struct bd71827_power *)data;

	dev_dbg(pwr->dev, "VF Resumed\n");
	power_supply_changed(pwr->sw->psy);

	return IRQ_HANDLED;
}

static irqreturn_t bd71827_temp_vf125_det(int irq, void *data)
{
	struct bd71827_power *pwr = (struct bd71827_power *)data;

	dev_dbg(pwr->dev, "VF125 Detected\n");
	power_supply_changed(pwr->sw->psy);

	return IRQ_HANDLED;
}

static irqreturn_t bd71827_temp_vf125_res(int irq, void *data)
{
	struct bd71827_power *pwr = (struct bd71827_power *)data;

	dev_dbg(pwr->dev, "VF125 Resumed\n");
	power_supply_changed(pwr->sw->psy);

	return IRQ_HANDLED;
}

struct bd7182x_irq_res {
	const char *name;
	irq_handler_t handler;
};

#define BDIRQ(na, hn) { .name = (na), .handler = (hn) }

static int bd7182x_get_irqs(struct platform_device *pdev,
			    struct bd71827_power *pwr)
{
	int i, irq, ret;
	static const struct bd7182x_irq_res bd71815_irqs[] = {
		BDIRQ("bd71815-dcin-rmv", BD_ISR_NAME(dcin_removed)),
		BDIRQ("bd71815-dcin-clps-out", BD_ISR_NAME(clps_out)),
		BDIRQ("bd71815-dcin-clps-in", BD_ISR_NAME(clps_in)),
		BDIRQ("bd71815-dcin-ovp-res", BD_ISR_NAME(dcin_ovp_res)),
		BDIRQ("bd71815-dcin-ovp-det", BD_ISR_NAME(dcin_ovp_det)),
		BDIRQ("bd71815-dcin-mon-res", BD_ISR_NAME(dcin_mon_res)),
		BDIRQ("bd71815-dcin-mon-det", BD_ISR_NAME(dcin_mon_det)),

		BDIRQ("bd71815-vsys-uv-res", BD_ISR_NAME(vsys_uv_res)),
		BDIRQ("bd71815-vsys-uv-det", BD_ISR_NAME(vsys_uv_det)),
		BDIRQ("bd71815-vsys-low-res", BD_ISR_NAME(vsys_low_res)),
		BDIRQ("bd71815-vsys-low-det",  BD_ISR_NAME(vsys_low_det)),
		BDIRQ("bd71815-vsys-mon-res",  BD_ISR_NAME(vsys_mon_res)),
		BDIRQ("bd71815-vsys-mon-det",  BD_ISR_NAME(vsys_mon_det)),
		BDIRQ("bd71815-chg-wdg-temp", BD_ISR_NAME(chg_wdg_temp)),
		BDIRQ("bd71815-chg-wdg",  BD_ISR_NAME(chg_wdg)),
		BDIRQ("bd71815-rechg-det", BD_ISR_NAME(rechg_det)),
		BDIRQ("bd71815-rechg-res", BD_ISR_NAME(rechg_res)),
		BDIRQ("bd71815-ranged-temp-transit", BD_ISR_NAME(temp_transit)),
		BDIRQ("bd71815-chg-state-change", BD_ISR_NAME(chg_state_changed)),
		BDIRQ("bd71815-bat-temp-normal", bd71827_temp_bat_hi_res),
		BDIRQ("bd71815-bat-temp-erange", bd71827_temp_bat_hi_det),
		BDIRQ("bd71815-bat-rmv", BD_ISR_NAME(bat_removed)),
		BDIRQ("bd71815-bat-det", BD_ISR_NAME(bat_det)),

		/* Add ISRs for these */
		BDIRQ("bd71815-therm-rmv", BD_ISR_NAME(therm_rmv)),
		BDIRQ("bd71815-therm-det", BD_ISR_NAME(therm_det)),
		BDIRQ("bd71815-bat-dead", BD_ISR_NAME(bat_dead)),
		BDIRQ("bd71815-bat-short-res", BD_ISR_NAME(bat_short_res)),
		BDIRQ("bd71815-bat-short-det", BD_ISR_NAME(bat_short)),
		BDIRQ("bd71815-bat-low-res", BD_ISR_NAME(bat_low_res)),
		BDIRQ("bd71815-bat-low-det", BD_ISR_NAME(bat_low)),
		BDIRQ("bd71815-bat-over-res", BD_ISR_NAME(bat_ov_res)),
		BDIRQ("bd71815-bat-over-det", BD_ISR_NAME(bat_ov)),
		BDIRQ("bd71815-bat-mon-res", BD_ISR_NAME(bat_mon_res)),
		BDIRQ("bd71815-bat-mon-det", BD_ISR_NAME(bat_mon)),
		/* cc-mon 1 & 3 ? */
		BDIRQ("bd71815-bat-cc-mon2", BD_ISR_NAME(bat_cc_mon)),
		BDIRQ("bd71815-bat-oc1-res", BD_ISR_NAME(bat_oc1_res)),
		BDIRQ("bd71815-bat-oc1-det", BD_ISR_NAME(bat_oc1)),
		BDIRQ("bd71815-bat-oc2-res", BD_ISR_NAME(bat_oc2_res)),
		BDIRQ("bd71815-bat-oc2-det", BD_ISR_NAME(bat_oc2)),
		BDIRQ("bd71815-bat-oc3-res", BD_ISR_NAME(bat_oc3_res)),
		BDIRQ("bd71815-bat-oc3-det", BD_ISR_NAME(bat_oc3)),
		BDIRQ("bd71815-temp-bat-low-res", BD_ISR_NAME(temp_bat_low_res)),
		BDIRQ("bd71815-temp-bat-low-det", BD_ISR_NAME(temp_bat_low)),
		BDIRQ("bd71815-temp-bat-hi-res", BD_ISR_NAME(temp_bat_hi_res)),
		BDIRQ("bd71815-temp-bat-hi-det", BD_ISR_NAME(temp_bat_hi)),
		/*
		 * TODO: add rest of the IRQs and re-check the handling.
		 * Check the bd71815-bat-cc-mon1, bd71815-bat-cc-mon3,
		 * bd71815-bat-low-res, bd71815-bat-low-det,
		 * bd71815-bat-hi-res, bd71815-bat-hi-det.
		 */
	};
	static const struct bd7182x_irq_res bd71828_irqs[] = {
		BDIRQ("bd71828-chg-done", bd718x7_chg_done),
		BDIRQ("bd71828-pwr-dcin-in", bd7182x_dcin_detected),
		BDIRQ("bd71828-pwr-dcin-out", bd7182x_dcin_removed),
		BDIRQ("bd71828-vbat-normal", bd71827_vbat_low_res),
		BDIRQ("bd71828-vbat-low", bd71827_vbat_low_det),
		BDIRQ("bd71828-btemp-hi", bd71827_temp_bat_hi_det),
		BDIRQ("bd71828-btemp-cool", bd71827_temp_bat_hi_res),
		BDIRQ("bd71828-btemp-lo", bd71827_temp_bat_low_det),
		BDIRQ("bd71828-btemp-warm", bd71827_temp_bat_low_res),
		BDIRQ("bd71828-temp-hi", bd71827_temp_vf_det),
		BDIRQ("bd71828-temp-norm", bd71827_temp_vf_res),
		BDIRQ("bd71828-temp-125-over", bd71827_temp_vf125_det),
		BDIRQ("bd71828-temp-125-under", bd71827_temp_vf125_res),
	};
	int num_irqs;
	const struct bd7182x_irq_res *irqs;


	switch (pwr->chip_type) {
	case ROHM_CHIP_TYPE_BD71827:
	case ROHM_CHIP_TYPE_BD71828:
		irqs = &bd71828_irqs[0];
		num_irqs = ARRAY_SIZE(bd71828_irqs);
		break;
	case ROHM_CHIP_TYPE_BD71815:
		irqs = &bd71815_irqs[0];
		num_irqs = ARRAY_SIZE(bd71815_irqs);
		break;
	default:
		return -EINVAL;
	}

	for (i = 0; i < num_irqs; i++) {
		irq = platform_get_irq_byname(pdev, irqs[i].name);

		ret = devm_request_threaded_irq(&pdev->dev, irq, NULL,
						irqs[i].handler, 0,
						irqs[i].name, pwr);
		if (ret)
			break;
	}

	return ret;
}

#define RSENS_DEFAULT_30MOHM 30000000

static int bd7182x_get_rsens(struct bd71827_power *pwr)
{
	u64 tmp = RSENS_CURR;
	int rsens_ohm = RSENS_DEFAULT_30MOHM;
	struct fwnode_handle *node = NULL;

	if (pwr->dev->parent)
		node = dev_fwnode(pwr->dev->parent);

	if (node) {
		int ret;
		uint32_t rs;

		ret = fwnode_property_read_u32(node,
					       "rohm,charger-sense-resistor-ohms",
					       &rs);
		if (ret) {
			if (ret == -EINVAL) {
				rs = RSENS_DEFAULT_30MOHM;
			} else {
				dev_err(pwr->dev, "Bad RSENS dt property\n");
				return ret;
			}
		}
		if (!rs) {
			dev_err(pwr->dev, "Bad RSENS value\n");
			return -EINVAL;
		}

		rsens_ohm = (int)rs;
	}

	/* Reg val to uA */
	do_div(tmp, rsens_ohm);

	pwr->curr_factor = tmp;
	pwr->rsens = rsens_ohm;
	dev_dbg(pwr->dev, "Setting rsens to %u ohm\n", pwr->rsens);
	dev_dbg(pwr->dev, "Setting curr-factor to %u\n", pwr->curr_factor);
	return 0;
}

/*
 * BD71827 has no proper support for detecting relaxed battery.
 * Driver has implemented current polling and logic has been that:
 * if for the specified time current consumption has always been below
 * threshold value when polled - then battery is assumed to be relaxed. This
 * for sure leads to a problem when current cunsumption has had short
 * 'spikes' - but this is what the logic has been - and it has probably been
 * working as the driver is left as is? So let's just keep this logic here.
 */
static bool bd71827_is_relaxed(struct simple_gauge *sw, int *rex_volt)
{
	struct bd71827_power *pwr = simple_gauge_get_drvdata(sw);
	int tmp_curr_mA, ret, curr, curr_avg;

	ret = bd71827_get_current_ds_adc(pwr, &curr, &curr_avg);
	if (ret) {
		dev_err(pwr->dev, "Failed to get current\n");
		return false;
	}

	tmp_curr_mA = uAMP_TO_mAMP(curr);
	if ((tmp_curr_mA * tmp_curr_mA) <=
	    (THR_RELAX_CURRENT_DEFAULT * THR_RELAX_CURRENT_DEFAULT))
		 /* No load */
		pwr->relax_time = pwr->relax_time + (JITTER_DEFAULT / 1000);
	else
		pwr->relax_time = 0;
	if (!(pwr->relax_time >= THR_RELAX_TIME_DEFAULT))
		return false;

	ret = bd71827_get_voltage(sw, rex_volt);
	if (ret) {
		dev_err(pwr->dev, "Failed to get Vbat\n");
		return false;
	}

	return true;
}

static bool bd71828_is_relaxed(struct simple_gauge *sw, int *rex_volt)
{
	int ret;
	u16 tmp;
	struct bd71827_power *pwr = simple_gauge_get_drvdata(sw);

	ret = bd7182x_read16_himask(pwr, pwr->regs->vbat_rex_avg,
				    BD7182x_MASK_VBAT_U, &tmp);
	if (ret) {
		dev_err(pwr->dev,
			"Failed to read battery relax voltage\n");
		return 0;
	}
	*rex_volt = tmp * 1000;

	return !!tmp;
}

static int bd71828_get_cycle(struct simple_gauge *sw, int *cycle)
{
	int tmpret, ret, update = 0;
	uint16_t charged_coulomb_cnt;
	int cc_designed_cap;
	struct bd71827_power *pwr = simple_gauge_get_drvdata(sw);

	ret = bd7182x_read16_himask(pwr, pwr->regs->coulomb_chg3, 0xff,
				    &charged_coulomb_cnt);
	if (ret) {
		dev_err(pwr->dev, "Failed to read charging CC (%d)\n", ret);
		return ret;
	}
	dev_dbg(pwr->dev, "charged_coulomb_cnt = 0x%x\n",
		(int)charged_coulomb_cnt);

	cc_designed_cap = UAH_to_CC(pwr, sw->designed_cap);

	while (charged_coulomb_cnt >= cc_designed_cap) {
		update = 1;
		/*
		 * sw-gauge caches old cycle value so we do not need to care
		 * about it. We just add new cycles
		 */
		*cycle = *cycle + 1;
		dev_dbg(pwr->dev,  "Update cycle = %d\n", *cycle);
		charged_coulomb_cnt -= cc_designed_cap;
	}
	if (update) {
		ret = stop_cc(pwr);
		if (ret)
			return ret;

		ret = bd7182x_write16(pwr, pwr->regs->coulomb_chg3,
				      charged_coulomb_cnt);
		if (ret) {
			dev_err(pwr->dev, "Failed to update charging CC (%d)\n",
				ret);
		}

		tmpret = start_cc(pwr);
		if (tmpret)
			return tmpret;
	}
	return ret;
}

static void fgauge_initial_values(struct bd71827_power *pwr)
{
	struct simple_gauge_desc *d = &pwr->gdesc;
	struct simple_gauge_ops *o = &pwr->ops;
	bool use_vdr = false;

	/* TODO: See if these could be get from DT? */
	d->poll_interval = JITTER_DEFAULT; /* 3 seconds */
	d->allow_set_cycle = true;
	d->cap_adjust_volt_threshold = pwr->low_thr_voltage;
	d->designed_cap = pwr->battery_cap;
	d->clamp_soc = true;

	o->get_uah_from_full = bd71828_get_uah_from_full;	/* Ok */
	o->get_uah = bd71828_get_uah;				/* Ok */
	o->update_cc_uah = bd71828_set_uah;			/* Ok */
	o->get_cycle = bd71828_get_cycle;			/* Ok */
	o->get_vsys = bd71827_get_vsys_min;

	/*
	 * We have custom OCV table => provide our own volt_to_cap and
	 * ocv_by_soc which utilize the custom tables.
	 */
	if (ocv_table[0]) {
		dev_dbg(pwr->dev, "OCV values given as parameters\n");
		o->get_soc_by_ocv = &bd71827_voltage_to_capacity;
		o->get_ocv_by_soc = &bd71827_get_ocv;
	}

	if (vdr_table_h[0] && vdr_table_m[0] && vdr_table_l[0] &&
		   vdr_table_vl[0])
		use_vdr = true;

	/* TODO:
	 *	o->suspend_calibrate = bd71828_suspend_calibrate;
	 */
	switch (pwr->chip_type) {
	case ROHM_CHIP_TYPE_BD71828:
		o->get_temp = bd71828_get_temp;
		o->is_relaxed = bd71828_is_relaxed;
		if (use_vdr)
			o->zero_cap_adjust = bd71828_zero_correct;
		break;
	case ROHM_CHIP_TYPE_BD71827:
		o->get_temp = bd71827_get_temp;
		o->is_relaxed = bd71827_is_relaxed;
		if (use_vdr)
			o->zero_cap_adjust = bd71828_zero_correct;
		break;
	case ROHM_CHIP_TYPE_BD71815:
		o->get_temp = bd71827_get_temp;
		o->is_relaxed = bd71828_is_relaxed;
		/*
		 * TODO: BD71815 has not been used with VDR. This is untested
		 * but I don't see why it wouldn't work by setting thresholds
		 * and by populating correct SOC-OCV tables.
		 */
		if (use_vdr)
			o->zero_cap_adjust = bd71828_zero_correct;
		break;
	/*
	 * No need to handle default here as this is done already in probe.
	 * But this keeps gcc shut-up.
	 */
	default:
		break;
	}
}

static int bd71827_power_probe(struct platform_device *pdev)
{
	struct bd71827_power *pwr;
	struct power_supply_config ac_cfg = {};
	struct simple_gauge_psy psycfg;
	int ret;
	struct regmap *regmap;

	psycfg = gauge_psy_config;

	regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!regmap) {
		dev_err(&pdev->dev, "No parent regmap\n");
		return -EINVAL;
	}

	pwr = devm_kzalloc(&pdev->dev, sizeof(*pwr), GFP_KERNEL);
	if (!pwr)
		return -ENOMEM;

	pwr->regmap = regmap;
	pwr->dev = &pdev->dev;
	pwr->chip_type = platform_get_device_id(pdev)->driver_data;

	switch (pwr->chip_type) {
	case ROHM_CHIP_TYPE_BD71828:
		pwr->bat_inserted = bd71828_bat_inserted;
		pwr->regs = &pwr_regs_bd71828;
		dev_dbg(pwr->dev, "Found ROHM BD71828\n");
		psycfg.psy_name	= "bd71828-charger";
		break;
	case ROHM_CHIP_TYPE_BD71827:
		pwr->bat_inserted = bd71828_bat_inserted;
		pwr->regs = &pwr_regs_bd71827;
		dev_dbg(pwr->dev, "Found ROHM BD71817\n");
		psycfg.psy_name	= "bd71827-charger";
		break;
	case ROHM_CHIP_TYPE_BD71815:
		pwr->bat_inserted = bd71815_bat_inserted;
		pwr->regs = &pwr_regs_bd71815;
		psycfg.psy_name	= "bd71815-charger";
		dev_dbg(pwr->dev, "Found ROHM BD71815\n");
	break;
	default:
		dev_err(pwr->dev, "Unknown PMIC\n");
		return -EINVAL;
	}

	/* We need to set batcap etc before we do set fgauge initial values */
	ret = bd71827_set_battery_parameters(pwr);
	if (ret) {
		dev_err(pwr->dev, "Missing battery parameters\n");

		return ret;
	}
	fgauge_initial_values(pwr);

	pwr->gdesc.drv_data = pwr;

	ret = bd7182x_get_rsens(pwr);
	if (ret) {
		dev_err(&pdev->dev, "sense resistor missing\n");
		return ret;
	}

	dev_set_drvdata(&pdev->dev, pwr);
	bd71827_init_hardware(pwr);

	psycfg.attr_grp		= &bd71827_sysfs_attr_groups[0];
	psycfg.of_node		= pdev->dev.parent->of_node;

	ac_cfg.supplied_to	= bd71827_ac_supplied_to;
	ac_cfg.num_supplicants	= ARRAY_SIZE(bd71827_ac_supplied_to);
	ac_cfg.drv_data		= pwr;

	pwr->ac = devm_power_supply_register(&pdev->dev, &bd71827_ac_desc,
					     &ac_cfg);
	if (IS_ERR(pwr->ac)) {
		ret = PTR_ERR(pwr->ac);
		dev_err(&pdev->dev, "failed to register ac: %d\n", ret);
		return ret;
	}

	pwr->sw = devm_psy_register_simple_gauge(pwr->dev, &psycfg, &pwr->ops,
					&pwr->gdesc);
	if (IS_ERR(pwr->sw)) {
		dev_err(&pdev->dev, "SW-gauge registration failed\n");
		return PTR_ERR(pwr->sw);
	}

	ret = bd7182x_get_irqs(pdev, pwr);
	if (ret) {
		dev_err(&pdev->dev, "failed to request IRQs: %d\n", ret);
		return ret;
	};

	/* Configure wakeup capable */
	device_set_wakeup_capable(pwr->dev, 1);
	device_set_wakeup_enable(pwr->dev, 1);

	return 0;
}

static const struct platform_device_id bd71827_charger_id[] = {
	{ "bd71815-power", ROHM_CHIP_TYPE_BD71815 },
	{ "bd71827-power", ROHM_CHIP_TYPE_BD71827 },
	{ "bd71828-power", ROHM_CHIP_TYPE_BD71828 },
	{ },
};
MODULE_DEVICE_TABLE(platform, bd71827_charger_id);

static struct platform_driver bd71827_power_driver = {
	.driver = {
		.name = "bd718xx-power",
	},
	.probe = bd71827_power_probe,
	.id_table = bd71827_charger_id,
};

module_platform_driver(bd71827_power_driver);
MODULE_ALIAS("platform:bd718xx-power");

module_param(use_load_bat_params, int, 0444);
MODULE_PARM_DESC(use_load_bat_params, "use_load_bat_params:Use loading battery parameters");

module_param(param_max_voltage, int, 0444);
MODULE_PARM_DESC(param_max_voltage,
		 "Maximum voltage of fully charged battery, uV");

module_param(param_min_voltage, int, 0444);
MODULE_PARM_DESC(param_min_voltage,
		 "Minimum voltage of fully drained battery, uV");

module_param(param_thr_voltage, int, 0444);
MODULE_PARM_DESC(param_thr_voltage,
		 "Threshold voltage for applying zero correction, uV");

module_param(battery_cap_mah, int, 0444);
MODULE_PARM_DESC(battery_cap_mah, "battery_cap_mah:Battery capacity (mAh)");

module_param(dgrd_cyc_cap, int, 0444);
MODULE_PARM_DESC(dgrd_cyc_cap, "dgrd_cyc_cap:Degraded capacity per cycle (uAh)");

module_param(soc_est_max_num, int, 0444);
MODULE_PARM_DESC(soc_est_max_num, "soc_est_max_num:SOC estimation max repeat number");

module_param_array(ocv_table, int, NULL, 0444);
MODULE_PARM_DESC(ocv_table, "ocv_table:Open Circuit Voltage table (uV)");

module_param_array(vdr_temps, int, NULL, 0444);
MODULE_PARM_DESC(vdr_temps, "vdr_temps:temperatures for VDR tables. (0.1C)");

module_param_array(vdr_table_h, int, NULL, 0444);
MODULE_PARM_DESC(vdr_table_h, "vdr_table_h:Voltage Drop Ratio temperature high area table");

module_param_array(vdr_table_m, int, NULL, 0444);
MODULE_PARM_DESC(vdr_table_m, "vdr_table_m:Voltage Drop Ratio temperature middle area table");

module_param_array(vdr_table_l, int, NULL, 0444);
MODULE_PARM_DESC(vdr_table_l, "vdr_table_l:Voltage Drop Ratio temperature low area table");

module_param_array(vdr_table_vl, int, NULL, 0444);
MODULE_PARM_DESC(vdr_table_vl, "vdr_table_vl:Voltage Drop Ratio temperature very low area table");

MODULE_AUTHOR("Cong Pham <cpham2403@gmail.com>");
MODULE_DESCRIPTION("ROHM BD718(15/17/27/28/78) PMIC Battery Charger driver");
MODULE_LICENSE("GPL");
