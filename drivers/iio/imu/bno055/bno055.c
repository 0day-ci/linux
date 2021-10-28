// SPDX-License-Identifier: GPL-2.0
/*
 * IIO driver for Bosh BNO055 IMU
 *
 * Copyright (C) 2021 Istituto Italiano di Tecnologia
 * Electronic Design Laboratory
 * Written by Andrea Merello <andrea.merello@iit.it>
 *
 * Portions of this driver are taken from the BNO055 driver patch
 * from Vlad Dogaru which is Copyright (c) 2016, Intel Corporation.
 *
 * This driver is also based on BMI160 driver, which is:
 *	Copyright (c) 2016, Intel Corporation.
 *	Copyright (c) 2019, Martin Kelly.
 */

#include <linux/bitmap.h>
#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/util_macros.h>

#include <linux/iio/iio.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/buffer.h>
#include <linux/iio/sysfs.h>

#include "bno055.h"

#define BNO055_FW_NAME "bno055-caldata"
#define BNO055_FW_EXT ".dat"
#define BNO055_FW_UID_NAME BNO055_FW_NAME "-%*phN" BNO055_FW_EXT
#define BNO055_FW_GENERIC_NAME (BNO055_FW_NAME BNO055_FW_EXT)

/* common registers */
#define BNO055_PAGESEL_REG		0x7

/* page 0 registers */
#define BNO055_CHIP_ID_REG		0x0
#define BNO055_CHIP_ID_MAGIC 0xA0
#define BNO055_SW_REV_LSB_REG		0x4
#define BNO055_SW_REV_MSB_REG		0x5
#define BNO055_ACC_DATA_X_LSB_REG	0x8
#define BNO055_ACC_DATA_Y_LSB_REG	0xA
#define BNO055_ACC_DATA_Z_LSB_REG	0xC
#define BNO055_MAG_DATA_X_LSB_REG	0xE
#define BNO055_MAG_DATA_Y_LSB_REG	0x10
#define BNO055_MAG_DATA_Z_LSB_REG	0x12
#define BNO055_GYR_DATA_X_LSB_REG	0x14
#define BNO055_GYR_DATA_Y_LSB_REG	0x16
#define BNO055_GYR_DATA_Z_LSB_REG	0x18
#define BNO055_EUL_DATA_X_LSB_REG	0x1A
#define BNO055_EUL_DATA_Y_LSB_REG	0x1C
#define BNO055_EUL_DATA_Z_LSB_REG	0x1E
#define BNO055_QUAT_DATA_W_LSB_REG	0x20
#define BNO055_LIA_DATA_X_LSB_REG	0x28
#define BNO055_LIA_DATA_Y_LSB_REG	0x2A
#define BNO055_LIA_DATA_Z_LSB_REG	0x2C
#define BNO055_GRAVITY_DATA_X_LSB_REG	0x2E
#define BNO055_GRAVITY_DATA_Y_LSB_REG	0x30
#define BNO055_GRAVITY_DATA_Z_LSB_REG	0x32
#define BNO055_SCAN_CH_COUNT ((BNO055_GRAVITY_DATA_Z_LSB_REG - BNO055_ACC_DATA_X_LSB_REG) / 2)
#define BNO055_TEMP_REG			0x34
#define BNO055_CALIB_STAT_REG		0x35
#define BNO055_CALIB_STAT_MASK GENMASK(1, 0)
#define BNO055_CALIB_STAT_MAGN_SHIFT 0
#define BNO055_CALIB_STAT_ACCEL_SHIFT 2
#define BNO055_CALIB_STAT_GYRO_SHIFT 4
#define BNO055_CALIB_STAT_SYS_SHIFT 6
#define BNO055_SYS_ERR_REG		0x3A
#define BNO055_SYS_TRIGGER_REG		0x3F
#define BNO055_SYS_TRIGGER_RST_INT BIT(6)
#define BNO055_SYS_TRIGGER_CLK_SEL BIT(7)
#define BNO055_OPR_MODE_REG		0x3D
#define BNO055_OPR_MODE_CONFIG 0x0
#define BNO055_OPR_MODE_AMG 0x7
#define BNO055_OPR_MODE_FUSION_FMC_OFF 0xB
#define BNO055_OPR_MODE_FUSION 0xC
#define BNO055_UNIT_SEL_REG		0x3B
/* Android orientation mode means: pitch value decreases turning clockwise */
#define BNO055_UNIT_SEL_ANDROID BIT(7)
#define BNO055_CALDATA_START		0x55
#define BNO055_CALDATA_END		0x6A
#define BNO055_CALDATA_LEN 22

/*
 * The difference in address between the register that contains the
 * value and the register that contains the offset.  This applies for
 * accel, gyro and magn channels.
 */
#define BNO055_REG_OFFSET_ADDR		0x4D

/* page 1 registers */
#define PG1(x) ((x) | 0x80)
#define BNO055_ACC_CONFIG_REG		PG1(0x8)
#define BNO055_ACC_CONFIG_LPF_MASK GENMASK(4, 2)
#define BNO055_ACC_CONFIG_RANGE_MASK GENMASK(1, 0)
#define BNO055_MAG_CONFIG_REG		PG1(0x9)
#define BNO055_MAG_CONFIG_HIGHACCURACY 0x18
#define BNO055_MAG_CONFIG_ODR_MASK GENMASK(2, 0)
#define BNO055_GYR_CONFIG_REG		PG1(0xA)
#define BNO055_GYR_CONFIG_RANGE_MASK GENMASK(2, 0)
#define BNO055_GYR_CONFIG_LPF_MASK GENMASK(5, 3)
#define BNO055_GYR_AM_SET_REG		PG1(0x1F)
#define BNO055_UID_LOWER_REG		PG1(0x50)
#define BNO055_UID_HIGHER_REG		PG1(0x5F)
#define BNO055_UID_LEN 16

static const int bno055_mag_odr_vals[] = {2, 6, 8, 10, 15, 20, 25, 30};
/* the following one is INT_PLUS_MICRO */
static const int bno055_acc_lpf_vals[] = {7, 810000, 15, 630000,
					  31, 250000, 62, 500000, 125, 0,
					  250, 0, 500, 0, 1000, 0};
static const int bno055_acc_ranges[] = {2, 4, 8, 16};
static const int bno055_gyr_lpf_vals[] = {523, 230, 116, 47, 23, 12, 64, 32};
static const int bno055_gyr_ranges[] = {2000, 1000, 500, 250, 125};

struct bno055_priv {
	struct regmap *regmap;
	struct device *dev;
	struct clk *clk;
	int operation_mode;
	int xfer_burst_break_thr;
	struct mutex lock;
	u8 uid[BNO055_UID_LEN];
	struct {
		__le16 chans[BNO055_SCAN_CH_COUNT];
		s64 timestamp __aligned(8);
	} buf;
};

static bool bno055_regmap_volatile(struct device *dev, unsigned int reg)
{
	/* data and status registers */
	if (reg >= BNO055_ACC_DATA_X_LSB_REG && reg <= BNO055_SYS_ERR_REG)
		return true;

	/* when in fusion mode, config is updated by chip */
	if (reg == BNO055_MAG_CONFIG_REG ||
	    reg == BNO055_ACC_CONFIG_REG ||
	    reg == BNO055_GYR_CONFIG_REG)
		return true;

	/* calibration data may be updated by the IMU */
	if (reg >= BNO055_CALDATA_START && reg <= BNO055_CALDATA_END)
		return true;
	return false;
}

static bool bno055_regmap_readable(struct device *dev, unsigned int reg)
{
	/* unnamed PG0 reserved areas */
	if ((reg < PG1(0) && reg > BNO055_CALDATA_END) ||
	    reg == 0x3C)
		return false;

	/* unnamed PG1 reserved areas */
	if (reg > PG1(BNO055_UID_HIGHER_REG) ||
	    (reg < PG1(BNO055_UID_LOWER_REG) && reg > PG1(BNO055_GYR_AM_SET_REG)) ||
	    reg == PG1(0xE) ||
	    (reg < PG1(BNO055_PAGESEL_REG) && reg >= PG1(0x0)))
		return false;
	return true;
}

static bool bno055_regmap_writeable(struct device *dev, unsigned int reg)
{
	/*
	 * Unreadable registers are indeed reserved; there are no WO regs
	 * (except for a single bit in SYS_TRIGGER register)
	 */
	if (!bno055_regmap_readable(dev, reg))
		return false;

	/* data and status registers */
	if (reg >= BNO055_ACC_DATA_X_LSB_REG && reg <= BNO055_SYS_ERR_REG)
		return false;

	/* IDs areas */
	if (reg < BNO055_PAGESEL_REG ||
	    (reg <= BNO055_UID_HIGHER_REG && reg >= BNO055_UID_LOWER_REG))
		return false;

	return true;
}

static const struct regmap_range_cfg bno055_regmap_ranges[] = {
	{
		.range_min = 0,
		.range_max = 0x7f * 2,
		.selector_reg = BNO055_PAGESEL_REG,
		.selector_mask = GENMASK(7, 0),
		.selector_shift = 0,
		.window_start = 0,
		.window_len = 0x80
	},
};

const struct regmap_config bno055_regmap_config = {
	.name = "bno055",
	.reg_bits = 8,
	.val_bits = 8,
	.ranges = bno055_regmap_ranges,
	.num_ranges = 1,
	.volatile_reg = bno055_regmap_volatile,
	.max_register = 0x80 * 2,
	.writeable_reg = bno055_regmap_writeable,
	.readable_reg = bno055_regmap_readable,
	.cache_type = REGCACHE_RBTREE,
};
EXPORT_SYMBOL_GPL(bno055_regmap_config);

static int bno055_reg_update_bits(struct bno055_priv *priv, unsigned int reg,
				  unsigned int mask, unsigned int val)
{
	int ret;

	ret = regmap_update_bits(priv->regmap, reg, mask, val);
	if (ret && ret != -ERESTARTSYS) {
		dev_err(priv->dev, "Regmap update_bits  error. adr: 0x%x, ret: %d",
			reg,  ret);
	}

	return ret;
}

/* must be called in configuration mode */
int bno055_calibration_load(struct bno055_priv *priv, const struct firmware *fw)
{
	if (fw->size != BNO055_CALDATA_LEN) {
		dev_dbg(priv->dev, "Invalid calibration file size %d (expected %d)",
			fw->size, BNO055_CALDATA_LEN);
		return -EINVAL;
	}

	dev_dbg(priv->dev, "loading cal data: %*ph", BNO055_CALDATA_LEN, fw->data);
	return regmap_bulk_write(priv->regmap, BNO055_CALDATA_START,
				fw->data, BNO055_CALDATA_LEN);
}

static int bno055_init(struct bno055_priv *priv, const struct firmware *caldata)
{
	int ret;

	ret = regmap_write(priv->regmap, BNO055_SYS_TRIGGER_REG,
			   (priv->clk ? BNO055_SYS_TRIGGER_CLK_SEL : 0) |
			   BNO055_SYS_TRIGGER_RST_INT);
	if (ret)
		return ret;

	msleep(100);
	ret = regmap_write(priv->regmap, BNO055_OPR_MODE_REG,
			   BNO055_OPR_MODE_CONFIG);
	if (ret)
		return ret;

	/* use standard SI units */
	ret = regmap_write(priv->regmap, BNO055_UNIT_SEL_REG,
			   BNO055_UNIT_SEL_ANDROID);
	if (ret)
		return ret;

	if (caldata) {
		ret = bno055_calibration_load(priv, caldata);
		if (ret)
			dev_warn(priv->dev, "failed to load calibration data with error %d",
				 ret);
	}

	priv->operation_mode = BNO055_OPR_MODE_FUSION;
	return regmap_write(priv->regmap, BNO055_OPR_MODE_REG,
			    priv->operation_mode);
}

static void bno055_uninit(void *arg)
{
	struct bno055_priv *priv = arg;

	/* stop the IMU */
	regmap_write(priv->regmap, BNO055_OPR_MODE_REG, BNO055_OPR_MODE_CONFIG);
}

static void bno055_clk_disable(void *arg)
{
	struct bno055_priv *priv = arg;

	clk_disable_unprepare(priv->clk);
}

#define BNO055_CHANNEL(_type, _axis, _index, _address, _sep, _sh, _avail) {	\
	.address = _address,							\
	.type = _type,								\
	.modified = 1,								\
	.channel2 = IIO_MOD_##_axis,						\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | (_sep),			\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) | (_sh),		\
	.info_mask_shared_by_type_available = _avail,				\
	.scan_index = _index,							\
	.scan_type = {								\
		.sign = 's',							\
		.realbits = 16,							\
		.storagebits = 16,						\
		.endianness = IIO_LE,						\
		.repeat = IIO_MOD_##_axis == IIO_MOD_QUATERNION ? 4 : 0		\
	},									\
}

/* scan indexes follow DATA register order */
enum bmi160_scan_axis {
	BNO055_SCAN_ACCEL_X,
	BNO055_SCAN_ACCEL_Y,
	BNO055_SCAN_ACCEL_Z,
	BNO055_SCAN_MAGN_X,
	BNO055_SCAN_MAGN_Y,
	BNO055_SCAN_MAGN_Z,
	BNO055_SCAN_GYRO_X,
	BNO055_SCAN_GYRO_Y,
	BNO055_SCAN_GYRO_Z,
	BNO055_SCAN_YAW,
	BNO055_SCAN_ROLL,
	BNO055_SCAN_PITCH,
	BNO055_SCAN_QUATERNION,
	BNO055_SCAN_LIA_X,
	BNO055_SCAN_LIA_Y,
	BNO055_SCAN_LIA_Z,
	BNO055_SCAN_GRAVITY_X,
	BNO055_SCAN_GRAVITY_Y,
	BNO055_SCAN_GRAVITY_Z,
	BNO055_SCAN_TIMESTAMP,
};

static const struct iio_chan_spec bno055_channels[] = {
	/* accelerometer */
	BNO055_CHANNEL(IIO_ACCEL, X, BNO055_SCAN_ACCEL_X,
		       BNO055_ACC_DATA_X_LSB_REG, BIT(IIO_CHAN_INFO_OFFSET),
		       BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY),
		       BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY)),
	BNO055_CHANNEL(IIO_ACCEL, Y, BNO055_SCAN_ACCEL_Y,
		       BNO055_ACC_DATA_Y_LSB_REG, BIT(IIO_CHAN_INFO_OFFSET),
		       BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY),
		       BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY)),
	BNO055_CHANNEL(IIO_ACCEL, Z, BNO055_SCAN_ACCEL_Z,
		       BNO055_ACC_DATA_Z_LSB_REG, BIT(IIO_CHAN_INFO_OFFSET),
		       BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY),
		       BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY)),
	/* gyroscope */
	BNO055_CHANNEL(IIO_ANGL_VEL, X, BNO055_SCAN_GYRO_X,
		       BNO055_GYR_DATA_X_LSB_REG, BIT(IIO_CHAN_INFO_OFFSET),
		       BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY),
		       BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY)),
	BNO055_CHANNEL(IIO_ANGL_VEL, Y, BNO055_SCAN_GYRO_Y,
		       BNO055_GYR_DATA_Y_LSB_REG, BIT(IIO_CHAN_INFO_OFFSET),
		       BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY),
		       BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY)),
	BNO055_CHANNEL(IIO_ANGL_VEL, Z, BNO055_SCAN_GYRO_Z,
		       BNO055_GYR_DATA_Z_LSB_REG, BIT(IIO_CHAN_INFO_OFFSET),
		       BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY),
		       BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY)),
	/* magnetometer */
	BNO055_CHANNEL(IIO_MAGN, X, BNO055_SCAN_MAGN_X,
		       BNO055_MAG_DATA_X_LSB_REG, BIT(IIO_CHAN_INFO_OFFSET),
		       BIT(IIO_CHAN_INFO_SAMP_FREQ), BIT(IIO_CHAN_INFO_SAMP_FREQ)),
	BNO055_CHANNEL(IIO_MAGN, Y, BNO055_SCAN_MAGN_Y,
		       BNO055_MAG_DATA_Y_LSB_REG, BIT(IIO_CHAN_INFO_OFFSET),
		       BIT(IIO_CHAN_INFO_SAMP_FREQ), BIT(IIO_CHAN_INFO_SAMP_FREQ)),
	BNO055_CHANNEL(IIO_MAGN, Z, BNO055_SCAN_MAGN_Z,
		       BNO055_MAG_DATA_Z_LSB_REG, BIT(IIO_CHAN_INFO_OFFSET),
		       BIT(IIO_CHAN_INFO_SAMP_FREQ), BIT(IIO_CHAN_INFO_SAMP_FREQ)),
	/* euler angle */
	BNO055_CHANNEL(IIO_ROT, YAW, BNO055_SCAN_YAW,
		       BNO055_EUL_DATA_X_LSB_REG, 0, 0, 0),
	BNO055_CHANNEL(IIO_ROT, ROLL, BNO055_SCAN_ROLL,
		       BNO055_EUL_DATA_Y_LSB_REG, 0, 0, 0),
	BNO055_CHANNEL(IIO_ROT, PITCH, BNO055_SCAN_PITCH,
		       BNO055_EUL_DATA_Z_LSB_REG, 0, 0, 0),
	/* quaternion */
	BNO055_CHANNEL(IIO_ROT, QUATERNION, BNO055_SCAN_QUATERNION,
		       BNO055_QUAT_DATA_W_LSB_REG, 0, 0, 0),

	/* linear acceleration */
	BNO055_CHANNEL(IIO_ACCEL, ACCEL_LINEAR_X, BNO055_SCAN_LIA_X,
		       BNO055_LIA_DATA_X_LSB_REG, 0, 0, 0),
	BNO055_CHANNEL(IIO_ACCEL, ACCEL_LINEAR_Y, BNO055_SCAN_LIA_Y,
		       BNO055_LIA_DATA_Y_LSB_REG, 0, 0, 0),
	BNO055_CHANNEL(IIO_ACCEL, ACCEL_LINEAR_Z, BNO055_SCAN_LIA_Z,
		       BNO055_LIA_DATA_Z_LSB_REG, 0, 0, 0),

	/* gravity vector */
	BNO055_CHANNEL(IIO_GRAVITY, X, BNO055_SCAN_GRAVITY_X,
		       BNO055_GRAVITY_DATA_X_LSB_REG, 0, 0, 0),
	BNO055_CHANNEL(IIO_GRAVITY, Y, BNO055_SCAN_GRAVITY_Y,
		       BNO055_GRAVITY_DATA_Y_LSB_REG, 0, 0, 0),
	BNO055_CHANNEL(IIO_GRAVITY, Z, BNO055_SCAN_GRAVITY_Z,
		       BNO055_GRAVITY_DATA_Z_LSB_REG, 0, 0, 0),

	{
		.type = IIO_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
		.scan_index = -1
	},
	IIO_CHAN_SOFT_TIMESTAMP(BNO055_SCAN_TIMESTAMP),
};

static int bno055_get_acc_lpf(struct bno055_priv *priv, int *val, int *val2)
{
	const int shift = __ffs(BNO055_ACC_CONFIG_LPF_MASK);
	int hwval, idx;
	int ret;

	ret = regmap_read(priv->regmap, BNO055_ACC_CONFIG_REG, &hwval);
	if (ret)
		return ret;

	idx = (hwval & BNO055_ACC_CONFIG_LPF_MASK) >> shift;
	*val = bno055_acc_lpf_vals[idx * 2];
	*val2 = bno055_acc_lpf_vals[idx * 2 + 1];

	return IIO_VAL_INT_PLUS_MICRO;
}

static int bno055_set_acc_lpf(struct bno055_priv *priv, int val, int val2)
{
	const int shift = __ffs(BNO055_ACC_CONFIG_LPF_MASK);
	int req_val = val * 1000 + val2 / 1000;
	bool first = true;
	int best_delta;
	int best_idx;
	int tbl_val;
	int delta;
	int ret;
	int i;

	for (i = 0; i < ARRAY_SIZE(bno055_acc_lpf_vals) / 2; i++) {
		tbl_val = bno055_acc_lpf_vals[i * 2] * 1000 +
			bno055_acc_lpf_vals[i * 2 + 1] / 1000;
		delta = abs(tbl_val - req_val);
		if (first || delta < best_delta) {
			best_delta = delta;
			best_idx = i;
			first = false;
		}
	}

	/*
	 * The closest value the HW supports is only one in fusion mode,
	 * and it is autoselected, so don't do anything, just return OK,
	 * as the closest possible value has been (virtually) selected
	 */
	if (priv->operation_mode != BNO055_OPR_MODE_AMG)
		return 0;

	ret = regmap_write(priv->regmap, BNO055_OPR_MODE_REG,
			   BNO055_OPR_MODE_CONFIG);
	if (ret)
		return ret;

	ret = bno055_reg_update_bits(priv, BNO055_ACC_CONFIG_REG,
				     BNO055_ACC_CONFIG_LPF_MASK,
				     best_idx << shift);

	if (ret)
		return ret;

	return regmap_write(priv->regmap, BNO055_OPR_MODE_REG,
			    BNO055_OPR_MODE_AMG);
}

static int bno055_get_regmask(struct bno055_priv *priv, int *val, int reg,
			      int mask,  const int tbl[])
{
	const int shift = __ffs(mask);
	int hwval, idx;
	int ret;

	ret = regmap_read(priv->regmap, reg, &hwval);
	if (ret)
		return ret;

	idx = (hwval & mask) >> shift;
	*val = tbl[idx];

	return IIO_VAL_INT;
}

static int bno055_set_regmask(struct bno055_priv *priv, int val, int reg,
			      int mask, const int table[], int table_len)

{
	int hwval = find_closest_unsorted(val, table, table_len);
	const int shift = __ffs(mask);
	int ret;
	/*
	 * The closest value the HW supports is only one in fusion mode,
	 * and it is autoselected, so don't do anything, just return OK,
	 * as the closest possible value has been (virtually) selected
	 */
	if (priv->operation_mode != BNO055_OPR_MODE_AMG)
		return 0;

	ret = regmap_write(priv->regmap, BNO055_OPR_MODE_REG,
			   BNO055_OPR_MODE_CONFIG);
	if (ret)
		return ret;

	ret = bno055_reg_update_bits(priv, reg, mask, hwval << shift);

	if (ret)
		return ret;

	return regmap_write(priv->regmap, BNO055_OPR_MODE_REG,
			    BNO055_OPR_MODE_AMG);
}

#define bno055_get_mag_odr(p, v) \
	bno055_get_regmask(p, v, \
			   BNO055_MAG_CONFIG_REG, BNO055_MAG_CONFIG_ODR_MASK, \
			   bno055_mag_odr_vals)

#define bno055_set_mag_odr(p, v) \
	bno055_set_regmask(p, v, \
			   BNO055_MAG_CONFIG_REG, BNO055_MAG_CONFIG_ODR_MASK, \
			   bno055_mag_odr_vals, \
			   ARRAY_SIZE(bno055_mag_odr_vals))

#define bno055_get_acc_range(p, v) \
	bno055_get_regmask(priv, v, \
			   BNO055_ACC_CONFIG_REG, BNO055_ACC_CONFIG_RANGE_MASK, \
			   bno055_acc_ranges)

#define bno055_set_acc_range(p, v) \
	bno055_set_regmask(p, v, \
			   BNO055_ACC_CONFIG_REG, \
			   BNO055_ACC_CONFIG_RANGE_MASK,  \
			   bno055_acc_ranges, ARRAY_SIZE(bno055_acc_ranges))

#define bno055_get_gyr_lpf(p, v) \
	bno055_get_regmask(p, v, \
			   BNO055_GYR_CONFIG_REG, BNO055_GYR_CONFIG_LPF_MASK, \
			   bno055_gyr_lpf_vals)

#define bno055_set_gyr_lpf(p, v) \
	bno055_set_regmask(p, v, \
			   BNO055_GYR_CONFIG_REG, BNO055_GYR_CONFIG_LPF_MASK, \
			   bno055_gyr_lpf_vals, \
			   ARRAY_SIZE(bno055_gyr_lpf_vals))

#define bno055_get_gyr_range(p, v) \
	bno055_get_regmask(p, v, \
			   BNO055_GYR_CONFIG_REG, BNO055_GYR_CONFIG_RANGE_MASK, \
			   bno055_gyr_ranges)

#define bno055_set_gyr_range(p, v) \
	bno055_set_regmask(p, v, \
			   BNO055_GYR_CONFIG_REG, \
			   BNO055_GYR_CONFIG_RANGE_MASK, \
			   bno055_gyr_ranges, ARRAY_SIZE(bno055_gyr_ranges))

static int bno055_read_simple_chan(struct iio_dev *indio_dev,
				   struct iio_chan_spec const *chan,
				   int *val, int *val2, long mask)
{
	struct bno055_priv *priv = iio_priv(indio_dev);
	__le16 raw_val;
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = regmap_bulk_read(priv->regmap, chan->address,
				       &raw_val, sizeof(raw_val));
		if (ret < 0)
			return ret;
		*val = (s16)le16_to_cpu(raw_val);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_OFFSET:
		if (priv->operation_mode != BNO055_OPR_MODE_AMG) {
			*val = 0;
		} else {
			ret = regmap_bulk_read(priv->regmap,
					       chan->address +
					       BNO055_REG_OFFSET_ADDR,
					       &raw_val, sizeof(raw_val));
			if (ret < 0)
				return ret;
			/*
			 * IMU reports sensor offests; IIO wants correction
			 * offset, thus we need the 'minus' here.
			 */
			*val = -(s16)le16_to_cpu(raw_val);
		}
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = 1;
		switch (chan->type) {
		case IIO_GRAVITY:
			/* Table 3-35: 1 m/s^2 = 100 LSB */
		case IIO_ACCEL:
			/* Table 3-17: 1 m/s^2 = 100 LSB */
			*val2 = 100;
			break;
		case IIO_MAGN:
			/*
			 * Table 3-19: 1 uT = 16 LSB.  But we need
			 * Gauss: 1G = 0.1 uT.
			 */
			*val2 = 160;
			break;
		case IIO_ANGL_VEL:
			/* Table 3-22: 1 Rps = 900 LSB */
			*val2 = 900;
			break;
		case IIO_ROT:
			/* Table 3-28: 1 degree = 16 LSB */
			*val2 = 16;
			break;
		default:
			return -EINVAL;
		}
		return IIO_VAL_FRACTIONAL;
	default:
		return -EINVAL;

	case IIO_CHAN_INFO_SAMP_FREQ:
		if (chan->type != IIO_MAGN)
			return -EINVAL;
		else
			return bno055_get_mag_odr(priv, val);

	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		switch (chan->type) {
		case IIO_ANGL_VEL:
			return bno055_get_gyr_lpf(priv, val);
		case IIO_ACCEL:
			return bno055_get_acc_lpf(priv, val, val2);
		default:
			return -EINVAL;
		}
	}
}

static int bno055_read_avail(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     const int **vals, int *type, int *length,
			     long mask)
{
	struct bno055_priv *priv = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		switch (chan->type) {
		case IIO_ANGL_VEL:
			if (priv->operation_mode != BNO055_OPR_MODE_AMG) {
				/* locked on 32 */
				*vals = bno055_gyr_lpf_vals + 7;
				*length = 1;
			} else {
				*vals = bno055_gyr_lpf_vals;
				*length = ARRAY_SIZE(bno055_gyr_lpf_vals);
			}
			*type =  IIO_VAL_INT;
			return IIO_AVAIL_LIST;
		default:
			return -EINVAL;
		case IIO_ACCEL:
			if (priv->operation_mode != BNO055_OPR_MODE_AMG) {
				/* locked on 62.5Hz  */
				*vals = bno055_acc_lpf_vals + 6;
				*length = 2;
			} else {
				*vals = bno055_acc_lpf_vals;
				*length = ARRAY_SIZE(bno055_acc_lpf_vals);
			}
			*type = IIO_VAL_INT_PLUS_MICRO;
			return IIO_AVAIL_LIST;
		}
		break;
	case IIO_CHAN_INFO_SAMP_FREQ:
		switch (chan->type) {
		case IIO_MAGN:
			if (priv->operation_mode != BNO055_OPR_MODE_AMG) {
				/* locked on 20Hz */
				*vals = bno055_mag_odr_vals + 5;
				*length = 1;
			} else {
				*vals = bno055_mag_odr_vals;
				*length = ARRAY_SIZE(bno055_mag_odr_vals);
			}
			*type =  IIO_VAL_INT;
			return IIO_AVAIL_LIST;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static int bno055_read_temp_chan(struct iio_dev *indio_dev, int *val)
{
	struct bno055_priv *priv = iio_priv(indio_dev);
	unsigned int raw_val;
	int ret;

	ret = regmap_read(priv->regmap, BNO055_TEMP_REG, &raw_val);
	if (ret < 0)
		return ret;

	/*
	 * Tables 3-36 and 3-37: one byte of priv, signed, 1 LSB = 1C.
	 * ABI wants milliC.
	 */
	*val = raw_val * 1000;

	return IIO_VAL_INT;
}

static int bno055_read_quaternion(struct iio_dev *indio_dev,
				  struct iio_chan_spec const *chan,
				  int size, int *vals, int *val_len,
				  long mask)
{
	struct bno055_priv *priv = iio_priv(indio_dev);
	__le16 raw_vals[4];
	int i, ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (size < 4)
			return -EINVAL;
		ret = regmap_bulk_read(priv->regmap,
				       BNO055_QUAT_DATA_W_LSB_REG,
				       raw_vals, sizeof(raw_vals));
		if (ret < 0)
			return ret;
		for (i = 0; i < 4; i++)
			vals[i] = (s16)le16_to_cpu(raw_vals[i]);
		*val_len = 4;
		return IIO_VAL_INT_MULTIPLE;
	case IIO_CHAN_INFO_SCALE:
		/* Table 3-31: 1 quaternion = 2^14 LSB */
		if (size < 2)
			return -EINVAL;
		vals[0] = 1;
		vals[1] = 1 << 14;
		return IIO_VAL_FRACTIONAL;
	default:
		return -EINVAL;
	}
}

static int _bno055_read_raw_multi(struct iio_dev *indio_dev,
				  struct iio_chan_spec const *chan,
				  int size, int *vals, int *val_len,
				  long mask)
{
	switch (chan->type) {
	case IIO_MAGN:
	case IIO_ACCEL:
	case IIO_ANGL_VEL:
	case IIO_GRAVITY:
		if (size < 2)
			return -EINVAL;
		*val_len = 2;
		return bno055_read_simple_chan(indio_dev, chan,
					       &vals[0], &vals[1],
					       mask);
	case IIO_TEMP:
		*val_len = 1;
		return bno055_read_temp_chan(indio_dev, &vals[0]);
	case IIO_ROT:
		/*
		 * Rotation is exposed as either a quaternion or three
		 * Euler angles.
		 */
		if (chan->channel2 == IIO_MOD_QUATERNION)
			return bno055_read_quaternion(indio_dev, chan,
						      size, vals,
						      val_len, mask);
		if (size < 2)
			return -EINVAL;
		*val_len = 2;
		return bno055_read_simple_chan(indio_dev, chan,
					       &vals[0], &vals[1],
					       mask);
	default:
		return -EINVAL;
	}
}

static int bno055_read_raw_multi(struct iio_dev *indio_dev,
				 struct iio_chan_spec const *chan,
				 int size, int *vals, int *val_len,
				 long mask)
{
	struct bno055_priv *priv = iio_priv(indio_dev);
	int ret;

	mutex_lock(&priv->lock);
	ret = _bno055_read_raw_multi(indio_dev, chan, size,
				     vals, val_len, mask);
	mutex_unlock(&priv->lock);
	return ret;
}

static int _bno055_write_raw(struct iio_dev *iio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long mask)
{
	struct bno055_priv *priv = iio_priv(iio_dev);

	switch (chan->type) {
	case IIO_MAGN:
		switch (mask) {
		case IIO_CHAN_INFO_SAMP_FREQ:
			return bno055_set_mag_odr(priv, val);

		default:
			return -EINVAL;
		}
	case IIO_ACCEL:
		switch (mask) {
		case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
			return bno055_set_acc_lpf(priv, val, val2);

		default:
			return -EINVAL;
		}
	case IIO_ANGL_VEL:
		switch (mask) {
		case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
			return bno055_set_gyr_lpf(priv, val);

		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static int bno055_write_raw(struct iio_dev *iio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long mask)
{
	struct bno055_priv *priv = iio_priv(iio_dev);
	int ret;

	mutex_lock(&priv->lock);
	ret = _bno055_write_raw(iio_dev, chan, val, val2, mask);
	mutex_unlock(&priv->lock);

	return ret;
}

static ssize_t in_accel_range_available_show(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	struct bno055_priv *priv = iio_priv(dev_to_iio_dev(dev));

	return sysfs_emit(buf, "%s\n",
			  priv->operation_mode != BNO055_OPR_MODE_AMG ? "4" :
			 "2 4 8 16");
}

static ssize_t in_anglvel_range_available_show(struct device *dev,
					       struct device_attribute *attr,
					       char *buf)
{
	struct bno055_priv *priv = iio_priv(dev_to_iio_dev(dev));

	return sysfs_emit(buf, "%s\n",
			 (priv->operation_mode != BNO055_OPR_MODE_AMG) ? "2000" :
			 "125 250 500 1000 2000");
}

static ssize_t bno055_operation_mode_set(struct bno055_priv *priv,
					 int operation_mode)
{
	int ret;

	mutex_lock(&priv->lock);
	priv->operation_mode = operation_mode;
	ret = regmap_write(priv->regmap, BNO055_OPR_MODE_REG,
			   BNO055_OPR_MODE_CONFIG);
	if (ret) {
		mutex_unlock(&priv->lock);
		return ret;
	}

	ret = regmap_write(priv->regmap, BNO055_OPR_MODE_REG, priv->operation_mode);
	mutex_unlock(&priv->lock);

	return ret;
}

static ssize_t bno055_fusion_enable_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct bno055_priv *priv = iio_priv(dev_to_iio_dev(dev));

	return sysfs_emit(buf, "%d\n",
			  priv->operation_mode != BNO055_OPR_MODE_AMG);
}

static ssize_t bno055_fusion_enable_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t len)
{
	struct bno055_priv *priv = iio_priv(dev_to_iio_dev(dev));
	int ret = 0;

	if (sysfs_streq(buf, "0")) {
		ret = bno055_operation_mode_set(priv, BNO055_OPR_MODE_AMG);
	} else {
		/*
		 * Coming from AMG means the FMC was off, just switch to fusion
		 * but don't change anything that doesn't belong to us (i.e let.
		 * FMC stay off.
		 * Coming from any other fusion mode means we don't need to do
		 * anything.
		 */
		if (priv->operation_mode == BNO055_OPR_MODE_AMG)
			ret = bno055_operation_mode_set(priv, BNO055_OPR_MODE_FUSION_FMC_OFF);
	}

	return len ?: len;
}

static ssize_t bno055_fmc_enable_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct bno055_priv *priv = iio_priv(dev_to_iio_dev(dev));

	return sysfs_emit(buf, "%d\n",
			  priv->operation_mode == BNO055_OPR_MODE_FUSION);
}

static ssize_t bno055_fmc_enable_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t len)
{
	struct bno055_priv *priv = iio_priv(dev_to_iio_dev(dev));
	int ret = 0;

	if (sysfs_streq(buf, "0")) {
		if (priv->operation_mode == BNO055_OPR_MODE_FUSION)
			ret = bno055_operation_mode_set(priv, BNO055_OPR_MODE_FUSION_FMC_OFF);
	} else {
		if (priv->operation_mode == BNO055_OPR_MODE_AMG)
			return -EINVAL;
	}

	return len ?: ret;
}

static ssize_t bno055_in_accel_range_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct bno055_priv *priv = iio_priv(dev_to_iio_dev(dev));
	int val;
	int ret;

	ret = bno055_get_acc_range(priv, &val);
	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%d\n", val);
}

static ssize_t bno055_in_accel_range_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t len)
{
	struct bno055_priv *priv = iio_priv(dev_to_iio_dev(dev));
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;

	mutex_lock(&priv->lock);
	ret = bno055_set_acc_range(priv, val);
	mutex_unlock(&priv->lock);

	return ret ?: len;
}

static ssize_t bno055_in_gyr_range_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	int val;
	int ret;
	struct bno055_priv *priv = iio_priv(dev_to_iio_dev(dev));

	ret = bno055_get_gyr_range(priv, &val);
	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%d\n", val);
}

static ssize_t bno055_in_gyr_range_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t len)
{
	struct bno055_priv *priv = iio_priv(dev_to_iio_dev(dev));
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;

	mutex_lock(&priv->lock);
	ret = bno055_set_gyr_range(priv, val);
	mutex_unlock(&priv->lock);

	return ret ?: len;
}

static ssize_t bno055_get_calib_status(struct device *dev, char *buf, int which)
{
	struct bno055_priv *priv = iio_priv(dev_to_iio_dev(dev));
	static const char * const calib_status[] = {
		"bad", "barely enough", "fair", "good" };
	const char *calib_str;
	int ret;
	int val;

	if (priv->operation_mode == BNO055_OPR_MODE_AMG ||
	    (priv->operation_mode == BNO055_OPR_MODE_FUSION_FMC_OFF &&
	     which == BNO055_CALIB_STAT_MAGN_SHIFT)) {
		calib_str = "idle";
	} else {
		mutex_lock(&priv->lock);
		ret = regmap_read(priv->regmap, BNO055_CALIB_STAT_REG, &val);
		mutex_unlock(&priv->lock);

		if (ret)
			return -EIO;

		val = (val >> which) & BNO055_CALIB_STAT_MASK;
		calib_str = calib_status[val];
	}

	return sysfs_emit(buf, "%s\n", calib_str);
}

static ssize_t unique_id_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct bno055_priv *priv = iio_priv(dev_to_iio_dev(dev));

	return sysfs_emit(buf, "%*ph\n", BNO055_UID_LEN, priv->uid);
}

static ssize_t in_calibration_data_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct bno055_priv *priv = iio_priv(dev_to_iio_dev(dev));
	u8 data[BNO055_CALDATA_LEN];
	int ret;

	mutex_lock(&priv->lock);
	ret = regmap_write(priv->regmap, BNO055_OPR_MODE_REG,
			   BNO055_OPR_MODE_CONFIG);
	if (ret)
		goto unlock;

	ret = regmap_bulk_read(priv->regmap, BNO055_CALDATA_START, data,
			       BNO055_CALDATA_LEN);
	if (ret)
		goto unlock;

	ret = regmap_write(priv->regmap, BNO055_OPR_MODE_REG, priv->operation_mode);
	mutex_unlock(&priv->lock);
	if (ret)
		return ret;

	memcpy(buf, data, BNO055_CALDATA_LEN);

	return BNO055_CALDATA_LEN;
unlock:
	mutex_unlock(&priv->lock);
	return ret;
}

static ssize_t in_autocalibration_status_sys_show(struct device *dev,
						  struct device_attribute *a,
						  char *buf)
{
	return bno055_get_calib_status(dev, buf, BNO055_CALIB_STAT_SYS_SHIFT);
}

static ssize_t in_autocalibration_status_accel_show(struct device *dev,
						    struct device_attribute *a,
						    char *buf)
{
	return bno055_get_calib_status(dev, buf, BNO055_CALIB_STAT_ACCEL_SHIFT);
}

static ssize_t in_autocalibration_status_gyro_show(struct device *dev,
						   struct device_attribute *a,
						   char *buf)
{
	return bno055_get_calib_status(dev, buf, BNO055_CALIB_STAT_GYRO_SHIFT);
}

static ssize_t in_autocalibration_status_magn_show(struct device *dev,
						   struct device_attribute *a,
						   char *buf)
{
	return bno055_get_calib_status(dev, buf, BNO055_CALIB_STAT_MAGN_SHIFT);
}

#ifdef CONFIG_DEBUG_FS
int bno055_debugfs_reg_access(struct iio_dev *iio_dev, unsigned int reg,
			      unsigned int writeval, unsigned int *readval)
{
	struct bno055_priv *priv = iio_priv(iio_dev);

	if (readval)
		return regmap_read(priv->regmap, reg, readval);
	else
		return regmap_write(priv->regmap, reg, writeval);
}

static ssize_t bno055_show_fw_version(struct file *file, char __user *userbuf,
				      size_t count, loff_t *ppos)
{
	struct bno055_priv *priv = file->private_data;
	int rev, ver;
	char *buf;
	int ret;

	ret = regmap_read(priv->regmap, BNO055_SW_REV_LSB_REG, &rev);
	if (ret)
		return ret;

	ret = regmap_read(priv->regmap, BNO055_SW_REV_MSB_REG, &ver);
	if (ret)
		return ret;

	buf = devm_kasprintf(priv->dev, GFP_KERNEL, "ver: 0x%x, rev: 0x%x\n",
			     ver, rev);
	if (!buf)
		return -ENOMEM;

	ret = simple_read_from_buffer(userbuf, count, ppos, buf, strlen(buf));
	devm_kfree(priv->dev, buf);

	return ret;
}

static const struct file_operations bno055_fw_version_ops = {
	.open = simple_open,
	.read = bno055_show_fw_version,
	.llseek = default_llseek,
	.owner = THIS_MODULE,
};

static void bno055_debugfs_init(struct iio_dev *iio_dev)
{
	struct bno055_priv *priv = iio_priv(iio_dev);

	debugfs_create_file("firmware_version", 0400,
			    iio_get_debugfs_dentry(iio_dev), priv,
			    &bno055_fw_version_ops);
}
#else
static void bno055_debugfs_init(struct iio_dev *iio_dev)
{
}

int bno055_debugfs_reg_access(struct iio_dev *iio_dev, unsigned int reg,
			      unsigned int writeval, unsigned int *readval)
{
	return 0;
}
#endif

static IIO_DEVICE_ATTR(fusion_enable, 0644,
		       bno055_fusion_enable_show,
		       bno055_fusion_enable_store, 0);

static IIO_DEVICE_ATTR(fast_magnetometer_calibration_enable, 0644,
		       bno055_fmc_enable_show,
		       bno055_fmc_enable_store, 0);

static IIO_DEVICE_ATTR(in_accel_range, 0644,
		       bno055_in_accel_range_show,
		       bno055_in_accel_range_store, 0);

static IIO_DEVICE_ATTR_RO(in_accel_range_available, 0);

static IIO_DEVICE_ATTR(in_anglvel_range, 0644,
		       bno055_in_gyr_range_show,
		       bno055_in_gyr_range_store, 0);

static IIO_DEVICE_ATTR_RO(in_anglvel_range_available, 0);

static IIO_DEVICE_ATTR_RO(in_autocalibration_status_sys, 0);
static IIO_DEVICE_ATTR_RO(in_autocalibration_status_accel, 0);
static IIO_DEVICE_ATTR_RO(in_autocalibration_status_gyro, 0);
static IIO_DEVICE_ATTR_RO(in_autocalibration_status_magn, 0);
static IIO_DEVICE_ATTR_RO(in_calibration_data, 0);

static IIO_DEVICE_ATTR_RO(unique_id, 0);

static struct attribute *bno055_attrs[] = {
	&iio_dev_attr_in_accel_range_available.dev_attr.attr,
	&iio_dev_attr_in_accel_range.dev_attr.attr,
	&iio_dev_attr_in_anglvel_range_available.dev_attr.attr,
	&iio_dev_attr_in_anglvel_range.dev_attr.attr,
	&iio_dev_attr_fusion_enable.dev_attr.attr,
	&iio_dev_attr_fast_magnetometer_calibration_enable.dev_attr.attr,
	&iio_dev_attr_in_autocalibration_status_sys.dev_attr.attr,
	&iio_dev_attr_in_autocalibration_status_accel.dev_attr.attr,
	&iio_dev_attr_in_autocalibration_status_gyro.dev_attr.attr,
	&iio_dev_attr_in_autocalibration_status_magn.dev_attr.attr,
	&iio_dev_attr_in_calibration_data.dev_attr.attr,
	&iio_dev_attr_unique_id.dev_attr.attr,
	NULL
};

static const struct attribute_group bno055_attrs_group = {
	.attrs = bno055_attrs,
};

static const struct iio_info bno055_info = {
	.read_raw_multi = bno055_read_raw_multi,
	.read_avail = bno055_read_avail,
	.write_raw = bno055_write_raw,
	.attrs = &bno055_attrs_group,
	.debugfs_reg_access = bno055_debugfs_reg_access,
};

/*
 * Reads len samples from the HW, stores them in buf starting from buf_idx,
 * and applies mask to cull (skip) unneeded samples.
 * Updates buf_idx incrementing with the number of stored samples.
 * Samples from HW are transferred into buf, then in-place copy on buf is
 * performed in order to cull samples that need to be skipped.
 * This avoids copies of the first samples until we hit the 1st sample to skip,
 * and also avoids having an extra bounce buffer.
 * buf must be able to contain len elements in spite of how many samples we are
 * going to cull.
 */
static int bno055_scan_xfer(struct bno055_priv *priv,
			    int start_ch, int len, unsigned long mask,
			    __le16 *buf, int *buf_idx)
{
	const int base = BNO055_ACC_DATA_X_LSB_REG;
	bool quat_in_read = false;
	int buf_base = *buf_idx;
	__le16 *dst, *src;
	int offs_fixup = 0;
	int xfer_len = len;
	int ret;
	int i, n;

	/*
	 * All chans are made up 1 16-bit sample, except for quaternion that is
	 * made up 4 16-bit values.
	 * For us the quaternion CH is just like 4 regular CHs.
	 * If our read starts past the quaternion make sure to adjust the
	 * starting offset; if the quaternion is contained in our scan then make
	 * sure to adjust the read len.
	 */
	if (start_ch > BNO055_SCAN_QUATERNION) {
		start_ch += 3;
	} else if ((start_ch <= BNO055_SCAN_QUATERNION) &&
		 ((start_ch + len) > BNO055_SCAN_QUATERNION)) {
		quat_in_read = true;
		xfer_len += 3;
	}

	ret = regmap_bulk_read(priv->regmap,
			       base + start_ch * sizeof(__le16),
			       buf + buf_base,
			       xfer_len * sizeof(__le16));
	if (ret)
		return ret;

	for_each_set_bit(i, &mask, len) {
		if (quat_in_read && ((start_ch + i) > BNO055_SCAN_QUATERNION))
			offs_fixup = 3;

		dst = buf + *buf_idx;
		src = buf + buf_base + offs_fixup + i;

		n = (start_ch + i == BNO055_SCAN_QUATERNION) ? 4 : 1;

		if (dst != src)
			memcpy(dst, src, n * sizeof(__le16));

		*buf_idx += n;
	}
	return 0;
}

static irqreturn_t bno055_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *iio_dev = pf->indio_dev;
	struct bno055_priv *priv = iio_priv(iio_dev);
	int xfer_start, start, end, prev_end;
	bool xfer_pending = false;
	bool first = true;
	unsigned long mask;
	int buf_idx = 0;
	bool thr_hit;
	int quat;
	int ret;

	mutex_lock(&priv->lock);
	for_each_set_bitrange(start, end, iio_dev->active_scan_mask,
			      iio_dev->masklength) {
		if (!xfer_pending)
			xfer_start = start;
		xfer_pending = true;

		if (!first) {
			quat = ((start > BNO055_SCAN_QUATERNION) &&
				(prev_end <= BNO055_SCAN_QUATERNION)) ? 3 : 0;
			thr_hit = (start - prev_end + quat) >
				priv->xfer_burst_break_thr;

			if (thr_hit) {
				mask = *iio_dev->active_scan_mask >> xfer_start;
				ret = bno055_scan_xfer(priv, xfer_start,
						       prev_end - xfer_start + 1,
						       mask, priv->buf.chans, &buf_idx);
				if (ret)
					goto done;
				xfer_pending = false;
			}
			first = false;
		}
		prev_end = end;
	}

	if (xfer_pending) {
		mask = *iio_dev->active_scan_mask >> xfer_start;
		ret = bno055_scan_xfer(priv, xfer_start,
				       end - xfer_start + 1,
				       mask, priv->buf.chans, &buf_idx);
	}

	iio_push_to_buffers_with_timestamp(iio_dev, &priv->buf, pf->timestamp);
done:
	mutex_unlock(&priv->lock);
	iio_trigger_notify_done(iio_dev->trig);
	return IRQ_HANDLED;
}

int bno055_probe(struct device *dev, struct regmap *regmap,
		 int xfer_burst_break_thr)
{
	const struct firmware *caldata;
	struct bno055_priv *priv;
	struct iio_dev *iio_dev;
	struct gpio_desc *rst;
	char *fw_name_buf;
	unsigned int val;
	int ret;

	iio_dev = devm_iio_device_alloc(dev, sizeof(*priv));
	if (!iio_dev)
		return -ENOMEM;

	iio_dev->name = "bno055";
	priv = iio_priv(iio_dev);
	mutex_init(&priv->lock);
	priv->regmap = regmap;
	priv->dev = dev;
	priv->xfer_burst_break_thr = xfer_burst_break_thr;
	rst = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(rst))
		return dev_err_probe(dev, PTR_ERR(rst), "Failed to get reset GPIO");

	priv->clk = devm_clk_get_optional(dev, "clk");
	if (IS_ERR(priv->clk))
		return dev_err_probe(dev, PTR_ERR(priv->clk), "Failed to get CLK");

	ret = clk_prepare_enable(priv->clk);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(dev, bno055_clk_disable, priv);
	if (ret)
		return ret;

	if (rst) {
		usleep_range(5000, 10000);
		gpiod_set_value_cansleep(rst, 0);
		usleep_range(650000, 750000);
	}

	ret = regmap_read(priv->regmap, BNO055_CHIP_ID_REG, &val);
	if (ret)
		return ret;

	if (val != BNO055_CHIP_ID_MAGIC) {
		dev_err(dev, "Unrecognized chip ID 0x%x", val);
		return -ENODEV;
	}

	ret = regmap_bulk_read(priv->regmap, BNO055_UID_LOWER_REG,
			       priv->uid, BNO055_UID_LEN);
	if (ret)
		return ret;

	/*
	 * This has nothing to do with the IMU firmware, this is for sensor
	 * calibration data.
	 */
	fw_name_buf = devm_kasprintf(dev, GFP_KERNEL,
				     BNO055_FW_UID_NAME,
				     BNO055_UID_LEN, priv->uid);
	if (!fw_name_buf)
		return -ENOMEM;

	ret = request_firmware(&caldata, fw_name_buf, dev);
	devm_kfree(dev, fw_name_buf);
	if (ret)
		ret = request_firmware(&caldata, BNO055_FW_GENERIC_NAME, dev);

	if (ret) {
		dev_notice(dev, "Failed to load calibration data firmware file; this has nothing to do with IMU main firmware.\nYou can calibrate your IMU (look for 'in_autocalibration_status*' files in sysfs) and then copy 'in_calibration_data' to your firmware file");
		caldata = NULL;
	}

	ret = bno055_init(priv, caldata);
	if (caldata)
		release_firmware(caldata);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(dev, bno055_uninit, priv);
	if (ret)
		return ret;

	iio_dev->channels = bno055_channels;
	iio_dev->num_channels = ARRAY_SIZE(bno055_channels);
	iio_dev->info = &bno055_info;
	iio_dev->modes = INDIO_DIRECT_MODE;

	ret = devm_iio_triggered_buffer_setup(dev, iio_dev,
					      iio_pollfunc_store_time,
					      bno055_trigger_handler, NULL);
	if (ret)
		return ret;

	ret = devm_iio_device_register(dev, iio_dev);
	if (ret)
		return ret;

	bno055_debugfs_init(iio_dev);

	return 0;
}
EXPORT_SYMBOL_GPL(bno055_probe);

MODULE_AUTHOR("Andrea Merello <andrea.merello@iit.it>");
MODULE_DESCRIPTION("Bosch BNO055 driver");
MODULE_LICENSE("GPL v2");
