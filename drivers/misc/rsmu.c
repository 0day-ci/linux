// SPDX-License-Identifier: GPL-2.0+
/*
 * This driver is developed for the IDT ClockMatrix(TM) and 82P33xxx families
 * of timing and synchronization devices. It will be used by Renesas PTP Clock
 * Manager for Linux (pcm4l) software to provide support to GNSS assisted
 * partial timing support (APTS) and other networking timing functions.
 *
 * Please note it must work with Renesas MFD driver to access device through
 * I2C/SPI. 
 * 
 * For pcm4l, please refer the following link for more details
 * https://www.renesas.com/us/en/software-tool/ptp-clock-manager-linux
 *
 * Copyright (C) 2021 Integrated Device Technology, Inc., a Renesas Company.
 */

#include <asm/unaligned.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/mfd/idt8a340_reg.h>
#include <linux/mfd/idt82p33_reg.h>
#include <linux/mfd/rsmu.h>
#include <uapi/linux/rsmu.h>

static DEFINE_IDA(rsmu_cdev_map);

/*
 * struct rsmu_cdev - Driver data for RSMU character device
 * @name: rsmu device name as rsmu[index]
 * @dev: pointer to platform device
 * @miscdev: character device handle
 * @regmap: I2C/SPI regmap handle
 * @lock: mutex to protect operations from being interrupted
 * @type: rsmu device type, passed through platform data
 * @index: rsmu device index
 */
struct rsmu_cdev {
	char name[16];
	struct device *dev;
	struct miscdevice miscdev;
	struct regmap *regmap;
	struct mutex *lock;
	enum rsmu_type type;
	int index;
};

/*
 * Enumerated type listing DPLL combination modes
 */
enum rsmu_dpll_combomode {
	E_COMBOMODE_CURRENT = 0,
	E_COMBOMODE_FASTAVG,
	E_COMBOMODE_SLOWAVG,
	E_COMBOMODE_HOLDOVER,
	E_COMBOMODE_MAX
};

/*
 * An id used to identify the respective child class states.
 */
enum rsmu_class_state {
	E_SRVLOINITIALSTATE = 0,
	E_SRVLOUNQUALIFIEDSTATE = 1,
	E_SRVLOLOCKACQSTATE = 2,
	E_SRVLOFREQUENCYLOCKEDSTATE = 3,
	E_SRVLOTIMELOCKEDSTATE = 4,
	E_SRVLOHOLDOVERINSPECSTATE = 5,
	E_SRVLOHOLDOVEROUTOFSPECSTATE = 6,
	E_SRVLOFREERUNSTATE = 7,
	E_SRVNUMBERLOSTATES = 8,
	E_SRVLOSTATEINVALID = 9,
};

/*======================== Sabre functions start ==========================*/
static int rsmu_sabre_set_combomode(struct rsmu_cdev *rsmu, u8 dpll, u8 mode)
{
	u16 dpll_ctrl_n;
	u8 cfg;
	int err;

	switch (dpll) {
	case 0:
		dpll_ctrl_n = SABRE_DPLL1_OPERATING_MODE_CNFG;
		break;
	case 1:
		dpll_ctrl_n = SABRE_DPLL2_OPERATING_MODE_CNFG;
		break;
	default:
		return -EINVAL;
	}

	if (mode >= E_COMBOMODE_MAX)
		return -EINVAL;

	err = regmap_bulk_read(rsmu->regmap, dpll_ctrl_n, &cfg, sizeof(cfg));
	if (err)
		return err;

	cfg &= ~(SABRE_COMBO_MODE_MASK << SABRE_COMBO_MODE_SHIFT);
	cfg |= mode << SABRE_COMBO_MODE_SHIFT;

	return regmap_bulk_write(rsmu->regmap, dpll_ctrl_n, &cfg, sizeof(cfg));
}

static int rsmu_sabre_get_dpll_state(struct rsmu_cdev *rsmu, u8 dpll, u8 *state)
{
	u16 dpll_sts_n;
	u8 cfg;
	int err;

	switch (dpll) {
	case 0:
		dpll_sts_n = SABRE_DPLL1_OPERATING_STS;
		break;
	case 1:
		dpll_sts_n = SABRE_DPLL2_OPERATING_STS;
		break;
	default:
		return -EINVAL;
	}

	err = regmap_bulk_read(rsmu->regmap, dpll_sts_n, &cfg, sizeof(cfg));
	if (err)
		return err;

	switch (cfg & SABRE_OPERATING_STS_MASK) {
	case SABRE_DPLL_STATE_FREERUN:
		*state = E_SRVLOUNQUALIFIEDSTATE;
		break;
	case SABRE_DPLL_STATE_PRELOCKED2:
	case SABRE_DPLL_STATE_PRELOCKED:
		*state = E_SRVLOLOCKACQSTATE;
		break;
	case SABRE_DPLL_STATE_LOCKED:
		*state = E_SRVLOTIMELOCKEDSTATE;
		break;
	case SABRE_DPLL_STATE_HOLDOVER:
		*state = E_SRVLOHOLDOVERINSPECSTATE;
		break;
	default:
		*state = E_SRVLOSTATEINVALID;
		break;
	}

	return 0;
}

static int rsmu_sabre_get_dpll_ffo(struct rsmu_cdev *rsmu, u8 dpll,
				   struct rsmu_get_ffo *ffo)
{
	u8 buf[8] = {0};
	s64 fcw = 0;
	u16 dpll_freq_n;
	int err;

	/*
	 * IDTDpll_GetCurrentDpllFreqOffset retrieves the FFO integrator only.
	 * In order to get Proportional + Integrator, use the holdover FFO with
	 * the filter bandwidth 0.5 Hz set by TCS file.
	 */
	switch (dpll) {
	case 0:
		dpll_freq_n = SABRE_DPLL1_HOLDOVER_FREQ_CNFG;
		break;
	case 1:
		dpll_freq_n = SABRE_DPLL2_HOLDOVER_FREQ_CNFG;
		break;
	default:
		return -EINVAL;
	}

	err = regmap_bulk_read(rsmu->regmap, dpll_freq_n, buf, 5);
	if (err)
		return err;

	/* Convert to frequency control word */
	fcw = sign_extend64(get_unaligned_le64(buf), 39);

	/* FCW unit is 77760 / ( 1638400 * 2^48) = 1.68615121864946 * 10^-16 */
	ffo->ffo = div_s64(fcw * 2107689, 12500);

	return 0;
}
/*======================== Sabre functions end ==========================*/

/*======================== Clockmatrix functions start ==================*/
static int rsmu_cm_set_combomode(struct rsmu_cdev *rsmu, u8 dpll, u8 mode)
{
	u16 dpll_ctrl_n;
	u8 cfg;
	int err;

	switch (dpll) {
	case 0:
		dpll_ctrl_n = DPLL_CTRL_0;
		break;
	case 1:
		dpll_ctrl_n = DPLL_CTRL_1;
		break;
	case 2:
		dpll_ctrl_n = DPLL_CTRL_2;
		break;
	case 3:
		dpll_ctrl_n = DPLL_CTRL_3;
		break;
	case 4:
		dpll_ctrl_n = DPLL_CTRL_4;
		break;
	case 5:
		dpll_ctrl_n = DPLL_CTRL_5;
		break;
	case 6:
		dpll_ctrl_n = DPLL_CTRL_6;
		break;
	case 7:
		dpll_ctrl_n = DPLL_CTRL_7;
		break;
	default:
		return -EINVAL;
	}

	if (mode >= E_COMBOMODE_MAX)
		return -EINVAL;

	err = regmap_bulk_read(rsmu->regmap, dpll_ctrl_n + DPLL_CTRL_COMBO_MASTER_CFG,
			       &cfg, sizeof(cfg));
	if (err)
		return err;

	/* Only need to enable/disable COMBO_MODE_HOLD. */
	if (mode)
		cfg |= COMBO_MASTER_HOLD;
	else
		cfg &= ~COMBO_MASTER_HOLD;

	return regmap_bulk_write(rsmu->regmap, dpll_ctrl_n + DPLL_CTRL_COMBO_MASTER_CFG,
				 &cfg, sizeof(cfg));
}

static int rsmu_cm_get_dpll_state(struct rsmu_cdev *rsmu, u8 dpll, u8 *state)
{
	u8 cfg;
	int err;

	/* 8 is sys dpll */
	if (dpll > 8)
		return -EINVAL;

	err = regmap_bulk_read(rsmu->regmap, STATUS + DPLL0_STATUS + dpll, &cfg, sizeof(cfg));
	if (err)
		return err;

	switch (cfg & DPLL_STATE_MASK) {
	case DPLL_STATE_FREERUN:
		*state = E_SRVLOUNQUALIFIEDSTATE;
		break;
	case DPLL_STATE_LOCKACQ:
	case DPLL_STATE_LOCKREC:
		*state = E_SRVLOLOCKACQSTATE;
		break;
	case DPLL_STATE_LOCKED:
		*state = E_SRVLOTIMELOCKEDSTATE;
		break;
	case DPLL_STATE_HOLDOVER:
		*state = E_SRVLOHOLDOVERINSPECSTATE;
		break;
	default:
		*state = E_SRVLOSTATEINVALID;
		break;
	}

	return 0;
}

static int rsmu_cm_get_dpll_ffo(struct rsmu_cdev *rsmu, u8 dpll,
				struct rsmu_get_ffo *ffo)
{
	u8 buf[8] = {0};
	s64 fcw = 0;
	u16 dpll_filter_status;
	int err;

	switch (dpll) {
	case 0:
		dpll_filter_status = DPLL0_FILTER_STATUS;
		break;
	case 1:
		dpll_filter_status = DPLL1_FILTER_STATUS;
		break;
	case 2:
		dpll_filter_status = DPLL2_FILTER_STATUS;
		break;
	case 3:
		dpll_filter_status = DPLL3_FILTER_STATUS;
		break;
	case 4:
		dpll_filter_status = DPLL4_FILTER_STATUS;
		break;
	case 5:
		dpll_filter_status = DPLL5_FILTER_STATUS;
		break;
	case 6:
		dpll_filter_status = DPLL6_FILTER_STATUS;
		break;
	case 7:
		dpll_filter_status = DPLL7_FILTER_STATUS;
		break;
	case 8:
		dpll_filter_status = DPLLSYS_FILTER_STATUS;
		break;
	default:
		return -EINVAL;
	}

	err = regmap_bulk_read(rsmu->regmap, STATUS + dpll_filter_status, buf, 6);
	if (err)
		return err;

	/* Convert to frequency control word */
	fcw = sign_extend64(get_unaligned_le64(buf), 47);

	/* FCW unit is 2 ^ -53 = 1.1102230246251565404236316680908e-16 */
	ffo->ffo = fcw * 111;

	return 0;
}
/*======================== Clockmatrix functions end ==================*/

static int
rsmu_set_combomode(struct rsmu_cdev *rsmu, void __user *arg)
{
	struct rsmu_combomode mode;
	int err;

	if (copy_from_user(&mode, arg, sizeof(mode)))
		return -EFAULT;

	mutex_lock(rsmu->lock);
	switch (rsmu->type) {
	case RSMU_CM:
		err = rsmu_cm_set_combomode(rsmu, mode.dpll, mode.mode);
		break;
	case RSMU_SABRE:
		err = rsmu_sabre_set_combomode(rsmu, mode.dpll, mode.mode);
		break;
	default:
		err = -EOPNOTSUPP;
	}
	mutex_unlock(rsmu->lock);

	if (err)
		dev_err(rsmu->dev, "%s failed with %d", __func__, err);

	return err;
}

static int
rsmu_get_dpll_state(struct rsmu_cdev *rsmu, void __user *arg)
{
	struct rsmu_get_state state_request;
	u8 state;
	int err;

	if (copy_from_user(&state_request, arg, sizeof(state_request)))
		return -EFAULT;

	mutex_lock(rsmu->lock);
	switch (rsmu->type) {
	case RSMU_CM:
		err = rsmu_cm_get_dpll_state(rsmu, state_request.dpll, &state);
		break;
	case RSMU_SABRE:
		err = rsmu_sabre_get_dpll_state(rsmu, state_request.dpll, &state);
		break;
	default:
		err = -EOPNOTSUPP;
	}
	mutex_unlock(rsmu->lock);

	if (err) {
		dev_err(rsmu->dev, "%s failed with %d", __func__, err);
		return err;
	}

	state_request.state = state;
	if (copy_to_user(arg, &state_request, sizeof(state_request)))
		return -EFAULT;

	return err;
}

static int
rsmu_get_dpll_ffo(struct rsmu_cdev *rsmu, void __user *arg)
{
	struct rsmu_get_ffo ffo_request;
	int err;

	if (copy_from_user(&ffo_request, arg, sizeof(ffo_request)))
		return -EFAULT;

	mutex_lock(rsmu->lock);
	switch (rsmu->type) {
	case RSMU_CM:
		err = rsmu_cm_get_dpll_ffo(rsmu, ffo_request.dpll, &ffo_request);
		break;
	case RSMU_SABRE:
		err = rsmu_sabre_get_dpll_ffo(rsmu, ffo_request.dpll, &ffo_request);
		break;
	default:
		err = -EOPNOTSUPP;
	}
	mutex_unlock(rsmu->lock);

	if (err) {
		dev_err(rsmu->dev, "%s failed with %d", __func__, err);
		return err;
	}

	if (copy_to_user(arg, &ffo_request, sizeof(ffo_request)))
		return -EFAULT;

	return err;
}

static struct rsmu_cdev *file2rsmu(struct file *file)
{
	return container_of(file->private_data, struct rsmu_cdev, miscdev);
}

static long
rsmu_ioctl(struct file *fptr, unsigned int cmd, unsigned long data)
{
	struct rsmu_cdev *rsmu = file2rsmu(fptr);
	void __user *arg = (void __user *)data;
	int err = 0;

	switch (cmd) {
	case RSMU_SET_COMBOMODE:
		err = rsmu_set_combomode(rsmu, arg);
		break;
	case RSMU_GET_STATE:
		err = rsmu_get_dpll_state(rsmu, arg);
		break;
	case RSMU_GET_FFO:
		err = rsmu_get_dpll_ffo(rsmu, arg);
		break;
	default:
		/* Should not get here */
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static const struct file_operations rsmu_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = rsmu_ioctl,
};

static int
rsmu_probe(struct platform_device *pdev)
{
	struct rsmu_ddata *ddata = dev_get_drvdata(pdev->dev.parent);
	struct rsmu_cdev *rsmu;
	int err;

	rsmu = devm_kzalloc(&pdev->dev, sizeof(*rsmu), GFP_KERNEL);
	if (!rsmu)
		return -ENOMEM;

	/* Save driver private data */
	platform_set_drvdata(pdev, rsmu);

	rsmu->dev = &pdev->dev;
	rsmu->type = ddata->type;
	rsmu->lock = &ddata->lock;
	rsmu->regmap = ddata->regmap;
	rsmu->index = ida_simple_get(&rsmu_cdev_map, 0, MINORMASK + 1, GFP_KERNEL);
	if (rsmu->index < 0) {
		dev_err(rsmu->dev, "Unable to get index %d\n", rsmu->index);
		return rsmu->index;
	}
	snprintf(rsmu->name, sizeof(rsmu->name), "rsmu%d", rsmu->index);

	/* Initialize and register the miscdev */
	rsmu->miscdev.minor = MISC_DYNAMIC_MINOR;
	rsmu->miscdev.fops = &rsmu_fops;
	rsmu->miscdev.name = rsmu->name;
	err = misc_register(&rsmu->miscdev);
	if (err) {
		dev_err(rsmu->dev, "Unable to register device\n");
		ida_simple_remove(&rsmu_cdev_map, rsmu->index);
		return -ENODEV;
	}

	return 0;
}

static int
rsmu_remove(struct platform_device *pdev)
{
	struct rsmu_cdev *rsmu = platform_get_drvdata(pdev);

	misc_deregister(&rsmu->miscdev);
	ida_simple_remove(&rsmu_cdev_map, rsmu->index);

	return 0;
}

static const struct platform_device_id rsmu_id_table[] = {
	{ "8a3400x-cdev", RSMU_CM},
	{ "82p33x1x-cdev", RSMU_SABRE},
	{}
};
MODULE_DEVICE_TABLE(platform, rsmu_id_table);

static struct platform_driver rsmu_driver = {
	.driver = {
		.name = "rsmu-cdev",
	},
	.probe = rsmu_probe,
	.remove =  rsmu_remove,
	.id_table = rsmu_id_table,
};

module_platform_driver(rsmu_driver);

MODULE_DESCRIPTION("Renesas SMU character device driver");
MODULE_LICENSE("GPL");
