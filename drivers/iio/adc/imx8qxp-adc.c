// SPDX-License-Identifier: GPL-2.0+
/*
 * NXP i.MX8QXP ADC driver
 */

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/iio/iio.h>
#include <linux/iio/driver.h>
#include <linux/iio/sysfs.h>

#define ADC_DRIVER_NAME		"imx8qxp-adc"

/* Register map definition */
#define ADC_CTRL		0x10
#define ADC_STAT		0x14
#define ADC_IE			0x18
#define ADC_DE			0x1c
#define ADC_CFG			0x20
#define ADC_FCTRL		0x30
#define ADC_SWTRIG		0x34
#define ADC_TCTRL(tid)		(0xc0 + tid * 4)
#define ADC_CMDH(cid)		(0x100 + cid * 8)
#define ADC_CMDL(cid)		(0x104 + cid * 8)
#define ADC_RESFIFO		0x300
#define ADC_TST			0xffc

/* ADC bit shift */
#define ADC_IE_FWMIE_SHIFT		(0)
#define ADC_CTRL_FIFO_RESET_SHIFT	(8)
#define ADC_CTRL_SOFTWARE_RESET_SHIFT	(1)
#define ADC_CTRL_ADC_ENABLE		(0)
#define ADC_TCTRL_TCMD_SHIFT		(24)
#define ADC_TCTRL_TDLY_SHIFT		(16)
#define ADC_TCTRL_TPRI_SHIFT		(8)
#define ADC_TCTRL_HTEN_SHIFT		(0)
#define ADC_CMDL_CSCALE_SHIFT		(8)
#define ADC_CMDL_MODE_SHIFT		(7)
#define ADC_CMDL_DIFF_SHIFT		(6)
#define ADC_CMDL_ABSEL_SHIFT		(5)
#define ADC_CMDL_ADCH_SHIFT		(0)
#define ADC_CMDH_NEXT_SHIFT		(24)
#define ADC_CMDH_LOOP_SHIFT		(16)
#define ADC_CMDH_AVGS_SHIFT		(12)
#define ADC_CMDH_STS_SHIFT		(8)
#define ADC_CMDH_LWI_SHIFT		(7)
#define ADC_CMDH_CMPEN_SHIFT		(0)
#define ADC_CFG_PWREN_SHIFT		(28)
#define ADC_CFG_PUDLY_SHIFT		(16)
#define ADC_CFG_REFSEL_SHIFT		(6)
#define ADC_CFG_PWRSEL_SHIFT		(4)
#define ADC_CFG_TPRICTRL_SHIFT		(0)
#define ADC_FCTRL_FWMARK_SHIFT		(16)
#define ADC_FCTRL_FWMARK_MASK		(0x1f << 16)
#define ADC_FCTRL_FCOUNT_MASK		(0x1f)

/* ADC PARAMETER*/
#define ADC_CMDL_CHANNEL_SCALE_FULL		(0x3f)
#define ADC_CMDL_SEL_A_A_B_CHANNEL		(0)
#define ADC_CMDL_STANDARD_RESOLUTION		(0)
#define ADC_CMDL_MODE_SINGLE			(0)
#define ADC_CMDH_LWI_INCREMENT_DISABLE		(0)
#define ADC_CMDH_CMPEN_DISABLE			(0)
#define ADC_PAUSE_ENABLE			(0x80000000)
#define ADC_TCTRL_TPRI_PRIORITY_HIGH		(0)

#define ADC_TCTRL_HTEN_HARDWARE_TIRGGER_DISABLE	(0)

#define MAX_CMD (8)
#define MAX_TRIG (8)

#define ADC_TIMEOUT		msecs_to_jiffies(100)

struct imx8qxp_adc_trigger_ctrl {
	u32 tcmd;
	u32 tdly;
	u32 tpri;
	u32 hten;
};

struct imx8qxp_adc_cmd_l {
	u32 scale;
	u32 mode;
	u32 diff;
	u32 absel;
	u32 adch;
};

struct imx8qxp_adc_cmd_h {
	u32 next;
	u32 loop;
	u32 avgs;
	u32 sts;
	u32 lwi;
	u32 cmpen;
};

struct imx8qxp_adc_cfg {
	u32 pwren;
	u32 pudly;
	u32 refsel;
	u32 pwrsel;
	u32 tprictrl;
};

struct imx8qxp_adc {
	struct device *dev;
	void __iomem *regs;
	struct clk *clk;
	struct clk *ipg_clk;

	u32 vref_uv;
	u32 value;
	u32 channel_id;
	u32 trigger_id;
	u32 cmd_id;

	struct regulator *vref;
	struct imx8qxp_adc_cmd_l adc_cmd_l[MAX_CMD + 1];
	struct imx8qxp_adc_cmd_h adc_cmd_h[MAX_CMD + 1];
	struct imx8qxp_adc_trigger_ctrl adc_trigger_ctrl[MAX_TRIG + 1];
	struct imx8qxp_adc_cfg adc_cfg;
	struct completion completion;
};

#define IMX8QXP_ADC_CHAN(_idx) {				\
	.type = IIO_VOLTAGE,					\
	.indexed = 1,						\
	.channel = (_idx),					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |	\
				BIT(IIO_CHAN_INFO_SAMP_FREQ),	\
}

static const struct iio_chan_spec imx8qxp_adc_iio_channels[] = {
	IMX8QXP_ADC_CHAN(0),
	IMX8QXP_ADC_CHAN(1),
	IMX8QXP_ADC_CHAN(2),
	IMX8QXP_ADC_CHAN(3),
	IMX8QXP_ADC_CHAN(4),
	IMX8QXP_ADC_CHAN(5),
	IMX8QXP_ADC_CHAN(6),
	IMX8QXP_ADC_CHAN(7),
};

static void imx8qxp_adc_feature_prepare(struct imx8qxp_adc *adc)
{
	u32 i;

	adc->trigger_id = 0;
	adc->cmd_id = 0;
	adc->channel_id = 0;

	for (i = 0; i < MAX_CMD + 1; i++) {
		adc->adc_cmd_l[i].scale = 1;
		adc->adc_cmd_l[i].mode = 0;
		adc->adc_cmd_l[i].diff = 0;
		adc->adc_cmd_l[i].absel = 0;
		adc->adc_cmd_l[i].adch = 0;
		adc->adc_cmd_h[i].next = 0;
		adc->adc_cmd_h[i].loop = 0;
		adc->adc_cmd_h[i].avgs = 0;
		adc->adc_cmd_h[i].sts = 0;
		adc->adc_cmd_h[i].lwi = 0;
		adc->adc_cmd_h[i].cmpen = 0;
	}

	for (i = 0; i < MAX_TRIG; i++) {
		adc->adc_trigger_ctrl[i].tcmd = 0;
		adc->adc_trigger_ctrl[i].tdly = 0;
		adc->adc_trigger_ctrl[i].tpri = 0;
		adc->adc_trigger_ctrl[i].hten = 0;
	}

	adc->adc_cfg.pwren = 0;
	adc->adc_cfg.pudly = 0;
	adc->adc_cfg.refsel = 0;
	adc->adc_cfg.pwrsel = 0;
	adc->adc_cfg.tprictrl = 0;
}

static void imx8qxp_adc_reset(struct imx8qxp_adc *adc)
{
	u32 ctrl;

	/*software reset, need to clear the set bit*/
	ctrl = readl(adc->regs + ADC_CTRL);
	ctrl |= 1 << ADC_CTRL_SOFTWARE_RESET_SHIFT;
	writel(ctrl, adc->regs + ADC_CTRL);
	udelay(10);
	ctrl &= ~(1 << ADC_CTRL_SOFTWARE_RESET_SHIFT);
	writel(ctrl, adc->regs + ADC_CTRL);

	/* reset the fifo */
	ctrl |= 1 << ADC_CTRL_FIFO_RESET_SHIFT;
	writel(ctrl, adc->regs + ADC_CTRL);
}

static void imx8qxp_adc_reg_config(struct imx8qxp_adc *adc)
{
	u32 adc_cfg, adc_tctrl, adc_cmdl, adc_cmdh;
	u32 t_id, c_id;

	adc_cfg = adc->adc_cfg.pwren << ADC_CFG_PWREN_SHIFT |
		  adc->adc_cfg.pudly << ADC_CFG_PUDLY_SHIFT |
		  adc->adc_cfg.refsel << ADC_CFG_REFSEL_SHIFT |
		  adc->adc_cfg.pwrsel << ADC_CFG_PWRSEL_SHIFT |
		  adc->adc_cfg.tprictrl << ADC_CFG_TPRICTRL_SHIFT;
	writel(adc_cfg, adc->regs + ADC_CFG);

	t_id = adc->trigger_id;
	adc_tctrl = adc->adc_trigger_ctrl[t_id].tcmd << ADC_TCTRL_TCMD_SHIFT |
		    adc->adc_trigger_ctrl[t_id].tdly << ADC_TCTRL_TDLY_SHIFT |
		    adc->adc_trigger_ctrl[t_id].tpri << ADC_TCTRL_TPRI_SHIFT |
		    adc->adc_trigger_ctrl[t_id].hten << ADC_TCTRL_HTEN_SHIFT;
	writel(adc_tctrl, adc->regs + ADC_TCTRL(t_id));

	c_id = adc->cmd_id - 1;
	adc_cmdl = adc->adc_cmd_l[c_id].scale << ADC_CMDL_CSCALE_SHIFT |
		   adc->adc_cmd_l[c_id].mode << ADC_CMDL_MODE_SHIFT |
		   adc->adc_cmd_l[c_id].diff << ADC_CMDL_DIFF_SHIFT |
		   adc->adc_cmd_l[c_id].absel << ADC_CMDL_ABSEL_SHIFT |
		   adc->adc_cmd_l[c_id].adch << ADC_CMDL_ADCH_SHIFT;
	writel(adc_cmdl, adc->regs + ADC_CMDL(c_id));

	adc_cmdh = adc->adc_cmd_h[c_id].next << ADC_CMDH_NEXT_SHIFT |
		   adc->adc_cmd_h[c_id].loop << ADC_CMDH_LOOP_SHIFT |
		   adc->adc_cmd_h[c_id].avgs << ADC_CMDH_AVGS_SHIFT |
		   adc->adc_cmd_h[c_id].sts << ADC_CMDH_STS_SHIFT |
		   adc->adc_cmd_h[c_id].lwi << ADC_CMDH_LWI_SHIFT |
		   adc->adc_cmd_h[c_id].cmpen << ADC_CMDH_CMPEN_SHIFT;
	writel(adc_cmdh, adc->regs + ADC_CMDH(c_id));
}

static void imx8qxp_adc_mode_config(struct imx8qxp_adc *adc)
{
	u32 cmd_id, trigger_id, channel_id;

	channel_id = adc->channel_id;
	cmd_id = adc->cmd_id - 1;
	trigger_id = adc->trigger_id;

	/* config the cmd */
	adc->adc_cmd_l[cmd_id].scale = ADC_CMDL_CHANNEL_SCALE_FULL;
	adc->adc_cmd_l[cmd_id].mode = ADC_CMDL_STANDARD_RESOLUTION;
	adc->adc_cmd_l[cmd_id].diff = ADC_CMDL_MODE_SINGLE;
	adc->adc_cmd_l[cmd_id].absel = ADC_CMDL_SEL_A_A_B_CHANNEL;
	adc->adc_cmd_l[cmd_id].adch = channel_id;

	adc->adc_cmd_h[cmd_id].next = 0;
	adc->adc_cmd_h[cmd_id].loop = 0;
	adc->adc_cmd_h[cmd_id].avgs = 7;
	adc->adc_cmd_h[cmd_id].sts = 0;
	adc->adc_cmd_h[cmd_id].lwi = ADC_CMDH_LWI_INCREMENT_DISABLE;
	adc->adc_cmd_h[cmd_id].cmpen = ADC_CMDH_CMPEN_DISABLE;

	/* config the trigger control */
	adc->adc_trigger_ctrl[trigger_id].tcmd = adc->cmd_id;
	adc->adc_trigger_ctrl[trigger_id].tdly = 0;
	adc->adc_trigger_ctrl[trigger_id].tpri = ADC_TCTRL_TPRI_PRIORITY_HIGH;
	adc->adc_trigger_ctrl[trigger_id].hten = ADC_TCTRL_HTEN_HARDWARE_TIRGGER_DISABLE;

	/* ADC configuration */
	adc->adc_cfg.pwren = 1;
	adc->adc_cfg.pudly = 0x80;
	adc->adc_cfg.refsel = 0;
	adc->adc_cfg.pwrsel = 3;
	adc->adc_cfg.tprictrl = 0;

	imx8qxp_adc_reg_config(adc);
}

static void imx8qxp_adc_fifo_config(struct imx8qxp_adc *adc)
{
	u32 fifo_ctrl, interrupt_en;

	fifo_ctrl = readl(adc->regs + ADC_FCTRL);
	fifo_ctrl &= ~ADC_FCTRL_FWMARK_MASK;
	/* set the watermark level to 1 */
	fifo_ctrl |= 0 << ADC_FCTRL_FWMARK_SHIFT;
	writel(fifo_ctrl, adc->regs + ADC_FCTRL);

	/* FIFO Watermark Interrupt Enable */
	interrupt_en = readl(adc->regs + ADC_IE);
	interrupt_en |= 1 << ADC_IE_FWMIE_SHIFT;
	writel(interrupt_en, adc->regs + ADC_IE);
}

static void imx8qxp_adc_disable(struct imx8qxp_adc *adc)
{
	u32 ctrl;

	ctrl = readl(adc->regs + ADC_CTRL) & ~(1 << ADC_CTRL_ADC_ENABLE);
	writel(ctrl, adc->regs + ADC_CTRL);
}

static int imx8qxp_adc_read_raw(struct iio_dev *indio_dev, struct iio_chan_spec const *chan,
				int *val, int *val2, long mask)
{
	struct imx8qxp_adc *adc = iio_priv(indio_dev);
	struct device *dev = adc->dev;

	u32 channel;
	u32 ctrl;
	long ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		pm_runtime_get_sync(dev);

		mutex_lock(&indio_dev->mlock);
		reinit_completion(&adc->completion);

		channel = chan->channel & 0x07;
		adc->channel_id = channel;
		adc->cmd_id = 1;
		adc->trigger_id = 0;
		imx8qxp_adc_mode_config(adc);

		imx8qxp_adc_fifo_config(adc);

		/* adc enable */
		ctrl = readl(adc->regs + ADC_CTRL) | (1 << ADC_CTRL_ADC_ENABLE);
		writel(ctrl, adc->regs + ADC_CTRL);
		/* adc start */
		writel(1 << adc->trigger_id, adc->regs + ADC_SWTRIG);

		ret = wait_for_completion_interruptible_timeout
				(&adc->completion, ADC_TIMEOUT);

		pm_runtime_mark_last_busy(dev);
		pm_runtime_put_sync_autosuspend(dev);

		if (ret == 0) {
			mutex_unlock(&indio_dev->mlock);
			return -ETIMEDOUT;
		}
		if (ret < 0) {
			mutex_unlock(&indio_dev->mlock);
			return ret;
		}

		*val = adc->value;
		mutex_unlock(&indio_dev->mlock);
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		adc->vref_uv = regulator_get_voltage(adc->vref);
		*val = adc->vref_uv / 1000;
		*val2 = 12;
		return IIO_VAL_FRACTIONAL_LOG2;

	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = clk_get_rate(adc->clk) / 3;
		return IIO_VAL_INT;

	default:
		return -EINVAL;
	}
}

static irqreturn_t imx8qxp_adc_isr(int irq, void *dev_id)
{
	struct imx8qxp_adc *adc = (struct imx8qxp_adc *)dev_id;

	u32 fifo_count;

	fifo_count = readl(adc->regs + ADC_FCTRL)
			& ADC_FCTRL_FCOUNT_MASK;

	if (fifo_count) {
		adc->value = (readl(adc->regs + ADC_RESFIFO) & 0xffff) >> 3;
		complete(&adc->completion);
	}

	return IRQ_HANDLED;
}

static int imx8qxp_adc_reg_access(struct iio_dev *indio_dev, unsigned int reg,
				  unsigned int writeval, unsigned int *readval)
{
	struct imx8qxp_adc *adc = iio_priv(indio_dev);
	struct device *dev = adc->dev;

	if (!readval || reg % 4 || reg > ADC_TST)
		return -EINVAL;

	pm_runtime_get_sync(dev);

	*readval = readl(adc->regs + reg);

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_sync_autosuspend(dev);

	return 0;
}

static const struct iio_info imx8qxp_adc_iio_info = {
	.read_raw = &imx8qxp_adc_read_raw,
	.debugfs_reg_access = &imx8qxp_adc_reg_access,
};

static int imx8qxp_adc_probe(struct platform_device *pdev)
{
	struct imx8qxp_adc *adc;
	struct iio_dev *indio_dev;
	struct device *dev = &pdev->dev;
	int irq;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*adc));
	if (!indio_dev) {
		dev_err(dev, "Failed allocating iio device\n");
		return -ENOMEM;
	}

	adc = iio_priv(indio_dev);
	adc->dev = dev;

	adc->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(adc->regs))
		return PTR_ERR(adc->regs);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	adc->clk = devm_clk_get(dev, "per");
	if (IS_ERR(adc->clk)) {
		ret = PTR_ERR(adc->clk);
		dev_err(dev, "Failed getting clock, err = %d\n", ret);
		return ret;
	}

	adc->ipg_clk = devm_clk_get(dev, "ipg");
	if (IS_ERR(adc->ipg_clk)) {
		ret = PTR_ERR(adc->ipg_clk);
		dev_err(dev, "Failed getting clock, err = %d\n", ret);
		return ret;
	}

	adc->vref = devm_regulator_get(dev, "vref");
	if (IS_ERR(adc->vref)) {
		ret = PTR_ERR(adc->vref);
		dev_err(dev,
			"Failed getting reference voltage, err = %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, indio_dev);

	init_completion(&adc->completion);

	indio_dev->name = dev_name(dev);
	indio_dev->info = &imx8qxp_adc_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = imx8qxp_adc_iio_channels;
	indio_dev->num_channels = ARRAY_SIZE(imx8qxp_adc_iio_channels);

	ret = devm_request_irq(dev, irq, imx8qxp_adc_isr, 0, dev_name(dev), adc);
	if (ret < 0) {
		dev_err(dev, "Failed requesting irq, irq = %d\n", irq);
		return ret;
	}

	imx8qxp_adc_feature_prepare(adc);
	imx8qxp_adc_reset(adc);

	ret = iio_device_register(indio_dev);
	if (ret) {
		imx8qxp_adc_disable(adc);
		dev_err(dev, "Couldn't register the device.\n");
		return ret;
	}

	pm_runtime_set_active(dev);
	pm_runtime_set_autosuspend_delay(dev, 50);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_enable(dev);

	return 0;
}

static int imx8qxp_adc_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct imx8qxp_adc *adc = iio_priv(indio_dev);
	struct device *dev = adc->dev;

	pm_runtime_get_sync(dev);

	iio_device_unregister(indio_dev);

	imx8qxp_adc_disable(adc);

	pm_runtime_disable(dev);
	pm_runtime_put_noidle(dev);

	return 0;
}

static int imx8qxp_adc_runtime_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct imx8qxp_adc *adc = iio_priv(indio_dev);

	imx8qxp_adc_disable(adc);

	clk_disable_unprepare(adc->clk);
	clk_disable_unprepare(adc->ipg_clk);
	regulator_disable(adc->vref);

	return 0;
}

static int imx8qxp_adc_runtime_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct imx8qxp_adc *adc = iio_priv(indio_dev);
	int ret;

	ret = regulator_enable(adc->vref);
	if (ret) {
		dev_err(dev, "Can't enable adc reference top voltage, err = %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(adc->clk);
	if (ret) {
		dev_err(dev, "Could not prepare or enable clock.\n");
		regulator_disable(adc->vref);
		return ret;
	}

	ret = clk_prepare_enable(adc->ipg_clk);
	if (ret) {
		dev_err(dev, "Could not prepare or enable clock.\n");
		clk_disable_unprepare(adc->clk);
		regulator_disable(adc->vref);
		return ret;
	}
	imx8qxp_adc_reset(adc);

	return 0;
}

static const struct dev_pm_ops imx8qxp_adc_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend, pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(imx8qxp_adc_runtime_suspend, imx8qxp_adc_runtime_resume, NULL)
};

static const struct of_device_id imx8qxp_adc_match[] = {
	{ .compatible = "nxp,imx8qxp-adc", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx8qxp_adc_match);

static struct platform_driver imx8qxp_adc_driver = {
	.probe		= imx8qxp_adc_probe,
	.remove		= imx8qxp_adc_remove,
	.driver		= {
		.name	= ADC_DRIVER_NAME,
		.of_match_table = imx8qxp_adc_match,
		.pm	= &imx8qxp_adc_pm_ops,
	},
};

module_platform_driver(imx8qxp_adc_driver);

MODULE_DESCRIPTION("i.MX8QuadXPlus ADC driver");
MODULE_LICENSE("GPL v2");
