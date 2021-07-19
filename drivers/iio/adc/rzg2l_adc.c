// SPDX-License-Identifier: GPL-2.0
/*
 * RZ/G2L A/D Converter driver
 *
 *  Copyright (c) 2021 Renesas Electronics Europe GmbH
 *
 * Author: Lad Prabhakar <prabhakar.mahadev-lad.rj@bp.renesas.com>
 */

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/iio/iio.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>

#define DRIVER_NAME		"rzg2l-adc"

#define RZG2L_ADM(n)			((n) * 0x4)
#define RZG2L_ADM0_ADCE			BIT(0)
#define RZG2L_ADM0_ADBSY		BIT(1)
#define RZG2L_ADM0_PWDWNB		BIT(2)
#define RZG2L_ADM0_SRESB		BIT(15)
#define RZG2L_ADM1_TRG			BIT(0)
#define RZG2L_ADM1_MS			BIT(2)
#define RZG2L_ADM1_BS			BIT(4)
#define RZG2L_ADM1_EGA_CLEAR		~GENMASK(13, 12)
#define RZG2L_ADM2_CHSEL_CLEAR		~GENMASK(7, 0)
#define RZG2L_ADM3_ADSMP		0x578
#define RZG2L_ADM3_ADCMP		(0xe << 16)
#define RZG2L_ADM3_ADIL_CLEAR		~GENMASK(31, 24)

#define RZG2L_ADINT			0x20
#define RZG2L_ADINT_CH_CLEAR		~GENMASK(7, 0)
#define RZG2L_ADINT_CSEEN		BIT(16)
#define RZG2L_ADINT_INTS		BIT(31)
#define RZG2L_ADSTS			0x24
#define RZG2L_ADINT_INTST_MASK		GENMASK(7, 0)
#define RZG2L_ADSTS_CSEST		BIT(16)
#define RZG2L_ADIVC			0x28
#define RZG2L_ADIVC_DIVADC_CLEAR	~GENMASK(8, 0)
#define RZG2L_ADIVC_DIVADC_4		0x4
#define RZG2L_ADFIL			0x2c
#define RZG2L_ADCR(n)			(0x30 + ((n) * 0x4))
#define RZG2L_ADCR_AD_MASK		GENMASK(11, 0)

#define RZG2L_ADC_MAX_CHANNELS		8
#define RZG2L_ADC_CHN_MASK		0x7
#define RZG2L_ADC_TIMEOUT		usecs_to_jiffies(1 * 4)

struct rzg2l_adc_data {
	const struct iio_chan_spec *channels;
	u8 num_channels;
};

struct rzg2l_adc {
	void __iomem *base;
	struct clk *pclk;
	struct clk *adclk;
	struct reset_control *presetn;
	struct reset_control *adrstn;
	struct completion completion;
	const struct rzg2l_adc_data *data;
	u16 last_val[RZG2L_ADC_MAX_CHANNELS];
};

static const char * const rzg2l_adc_channel_name[] = {
	"adc0",
	"adc1",
	"adc2",
	"adc3",
	"adc4",
	"adc5",
	"adc6",
	"adc7",
};

static unsigned int rzg2l_adc_readl(struct rzg2l_adc *adc, u32 reg)
{
	return readl(adc->base + reg);
}

static void rzg2l_adc_writel(struct rzg2l_adc *adc, unsigned int reg, u32 val)
{
	writel(val, adc->base + reg);
}

static void rzg2l_adc_pwr(struct rzg2l_adc *adc, bool on)
{
	u32 reg;

	reg = rzg2l_adc_readl(adc, RZG2L_ADM(0));
	if (on)
		reg |= RZG2L_ADM0_PWDWNB;
	else
		reg &= ~RZG2L_ADM0_PWDWNB;
	rzg2l_adc_writel(adc, RZG2L_ADM(0), reg);
	udelay(2);
}

static void rzg2l_adc_start_stop(struct rzg2l_adc *adc, bool start)
{
	int timeout = 5;
	u32 reg;

	/* stop A/D conversion */
	reg = rzg2l_adc_readl(adc, RZG2L_ADM(0));
	if (start)
		reg |= RZG2L_ADM0_ADCE;
	else
		reg &= ~RZG2L_ADM0_ADCE;
	rzg2l_adc_writel(adc, RZG2L_ADM(0), reg);

	if (start)
		return;

	do {
		usleep_range(100, 200);
		reg = rzg2l_adc_readl(adc, RZG2L_ADM(0));
		timeout--;
		if (!timeout) {
			pr_err("%s stopping ADC timed out\n", __func__);
			break;
		}
	} while (((reg & RZG2L_ADM0_ADBSY) || (reg & RZG2L_ADM0_ADCE)));
}

static void rzg2l_set_trigger(struct rzg2l_adc *adc)
{
	u32 reg;

	/* SW trigger */
	reg = rzg2l_adc_readl(adc, RZG2L_ADM(1));
	reg &= RZG2L_ADM1_EGA_CLEAR;
	reg &= ~RZG2L_ADM1_BS;
	reg |= RZG2L_ADM1_MS;
	reg &= ~RZG2L_ADM1_TRG;
	rzg2l_adc_writel(adc, RZG2L_ADM(1), reg);
}

static int rzg2l_adc_conversion_setup(struct rzg2l_adc *adc, u8 ch)
{
	u32 reg;

	if (rzg2l_adc_readl(adc, RZG2L_ADM(0)) & RZG2L_ADM0_ADBSY)
		return -EBUSY;

	rzg2l_set_trigger(adc);

	/* select channel */
	reg = rzg2l_adc_readl(adc, RZG2L_ADM(2));
	reg &= RZG2L_ADM2_CHSEL_CLEAR;
	reg |= BIT(ch);
	rzg2l_adc_writel(adc, RZG2L_ADM(2), reg);

	reg = rzg2l_adc_readl(adc, RZG2L_ADM(3));
	reg &= RZG2L_ADM3_ADIL_CLEAR;
	reg |= RZG2L_ADM3_ADCMP;
	reg |= RZG2L_ADM3_ADSMP;
	rzg2l_adc_writel(adc, RZG2L_ADM(3), reg);

	reg = rzg2l_adc_readl(adc, RZG2L_ADIVC);
	reg &= RZG2L_ADIVC_DIVADC_CLEAR;
	reg |= RZG2L_ADIVC_DIVADC_4;
	rzg2l_adc_writel(adc, RZG2L_ADIVC, reg);

	reg = rzg2l_adc_readl(adc, RZG2L_ADINT);
	reg &= ~RZG2L_ADINT_INTS;
	reg &= RZG2L_ADINT_CH_CLEAR;
	reg |= RZG2L_ADINT_CSEEN;
	reg |= BIT(ch);
	rzg2l_adc_writel(adc, RZG2L_ADINT, reg);

	return 0;
}

static int rzg2l_adc_set_power(struct iio_dev *indio_dev, bool on)
{
	struct device *dev = indio_dev->dev.parent;

	if (on)
		return pm_runtime_resume_and_get(dev);

	return pm_runtime_put_sync(dev);
}

static int rzg2l_adc_conversion(struct iio_dev *indio_dev, struct rzg2l_adc *adc, u8 ch)
{
	int ret;

	ret = rzg2l_adc_set_power(indio_dev, true);
	if (ret)
		return ret;

	ret = rzg2l_adc_conversion_setup(adc, ch);
	if (ret) {
		rzg2l_adc_set_power(indio_dev, false);
		return ret;
	}

	reinit_completion(&adc->completion);

	rzg2l_adc_start_stop(adc, true);

	if (!wait_for_completion_timeout(&adc->completion, RZG2L_ADC_TIMEOUT)) {
		rzg2l_adc_writel(adc, RZG2L_ADINT,
				 rzg2l_adc_readl(adc, RZG2L_ADINT) & RZG2L_ADINT_CH_CLEAR);
		rzg2l_adc_start_stop(adc, false);
		rzg2l_adc_set_power(indio_dev, false);
		return -ETIMEDOUT;
	}

	return rzg2l_adc_set_power(indio_dev, false);
}

static int rzg2l_adc_read_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int *val, int *val2, long mask)
{
	struct rzg2l_adc *adc = iio_priv(indio_dev);
	int ret;
	u8 ch;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (chan->type != IIO_VOLTAGE)
			return -EINVAL;

		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;

		ch = chan->channel & RZG2L_ADC_CHN_MASK;
		ret = rzg2l_adc_conversion(indio_dev, adc, ch);
		if (ret) {
			iio_device_release_direct_mode(indio_dev);
			return ret;
		}
		*val = adc->last_val[ch];
		iio_device_release_direct_mode(indio_dev);

		return IIO_VAL_INT;

	default:
		return -EINVAL;
	}
}

static int rzg2l_adc_read_label(struct iio_dev *iio_dev,
				const struct iio_chan_spec *chan,
				char *label)
{
	if (chan->channel >= RZG2L_ADC_MAX_CHANNELS)
		return -EINVAL;

	return snprintf(label, PAGE_SIZE, "%s\n", rzg2l_adc_channel_name[chan->channel]);
}

static const struct iio_info rzg2l_adc_iio_info = {
	.read_raw = rzg2l_adc_read_raw,
	.read_label = rzg2l_adc_read_label,
};

static irqreturn_t rzg2l_adc_isr(int irq, void *dev_id)
{
	struct rzg2l_adc *adc = (struct rzg2l_adc *)dev_id;
	u8 intst;
	u32 reg;
	u8 i;

	reg = rzg2l_adc_readl(adc, RZG2L_ADSTS);
	if (reg & RZG2L_ADSTS_CSEST) {
		rzg2l_adc_writel(adc, RZG2L_ADSTS, reg);
		return IRQ_HANDLED;
	}

	intst = reg & RZG2L_ADINT_INTST_MASK;
	if (!intst)
		return IRQ_HANDLED;

	for (i = 0; i < RZG2L_ADC_MAX_CHANNELS; i++) {
		if (intst & BIT(i))
			adc->last_val[i] = rzg2l_adc_readl(adc, RZG2L_ADCR(i)) & RZG2L_ADCR_AD_MASK;
	}

	rzg2l_adc_writel(adc, RZG2L_ADSTS, reg);

	complete(&adc->completion);

	return IRQ_HANDLED;
}

static int rzg2l_adc_parse_of(struct platform_device *pdev, struct rzg2l_adc *adc)
{
	struct iio_chan_spec *chan_array;
	struct fwnode_handle *fwnode;
	struct rzg2l_adc_data *data;
	unsigned int channel;
	int num_channels;
	int ret;
	u8 i;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	num_channels = device_get_child_node_count(&pdev->dev);
	if (!num_channels) {
		dev_err(&pdev->dev, "no channel children\n");
		return -ENODEV;
	}

	if (num_channels > RZG2L_ADC_MAX_CHANNELS) {
		dev_err(&pdev->dev, "num of channel children out of range\n");
		return -EINVAL;
	}

	chan_array = devm_kcalloc(&pdev->dev, num_channels, sizeof(*chan_array),
				  GFP_KERNEL);
	if (!chan_array)
		return -ENOMEM;

	i = 0;
	device_for_each_child_node(&pdev->dev, fwnode) {
		ret = fwnode_property_read_u32(fwnode, "reg", &channel);
		if (ret)
			return ret;

		if (channel >= RZG2L_ADC_MAX_CHANNELS)
			return -EINVAL;

		chan_array[i].type = IIO_VOLTAGE;
		chan_array[i].indexed = 1;
		chan_array[i].channel = channel;
		chan_array[i].info_mask_separate = BIT(IIO_CHAN_INFO_RAW);
		chan_array[i].datasheet_name = rzg2l_adc_channel_name[channel];
		i++;
	}

	data->num_channels = num_channels;
	data->channels = chan_array;
	adc->data = data;

	return 0;
}

static int rzg2l_adc_sw_reset(struct rzg2l_adc *adc)
{
	int timeout = 5;
	u32 val;

	val = rzg2l_adc_readl(adc, RZG2L_ADM(0));
	val |= RZG2L_ADM0_SRESB;
	rzg2l_adc_writel(adc, RZG2L_ADM(0), val);

	while (!(rzg2l_adc_readl(adc, RZG2L_ADM(0)) & RZG2L_ADM0_SRESB)) {
		if (!timeout)
			return -EBUSY;
		timeout--;
		usleep_range(100, 200);
	}

	return 0;
}

static int rzg2l_adc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct iio_dev *indio_dev;
	struct rzg2l_adc *adc;
	int ret;
	int irq;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*adc));
	if (!indio_dev)
		return -ENOMEM;

	adc = iio_priv(indio_dev);

	ret = rzg2l_adc_parse_of(pdev, adc);
	if (ret)
		return ret;

	adc->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(adc->base))
		return PTR_ERR(adc->base);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "no irq resource\n");
		return irq;
	}

	adc->pclk = devm_clk_get(dev, "pclk");
	if (IS_ERR(adc->pclk)) {
		dev_err(dev, "Failed to get pclk");
		return PTR_ERR(adc->pclk);
	}

	adc->adclk = devm_clk_get(dev, "adclk");
	if (IS_ERR(adc->adclk)) {
		dev_err(dev, "Failed to get adclk");
		return PTR_ERR(adc->adclk);
	}

	adc->adrstn = devm_reset_control_get_exclusive(dev, "adrst-n");
	if (IS_ERR(adc->adrstn)) {
		dev_err(dev, "failed to get adrstn\n");
		return PTR_ERR(adc->adrstn);
	}

	adc->presetn = devm_reset_control_get_exclusive(dev, "presetn");
	if (IS_ERR(adc->presetn)) {
		dev_err(dev, "failed to get presetn\n");
		return PTR_ERR(adc->presetn);
	}

	ret = reset_control_deassert(adc->adrstn);
	if (ret)
		return ret;

	ret = reset_control_deassert(adc->presetn);
	if (ret)
		goto assert_adrstn;

	ret = clk_prepare_enable(adc->pclk);
	if (ret)
		goto assert_presetn;

	ret = rzg2l_adc_sw_reset(adc);
	if (ret)
		goto unprepare_pclk;

	clk_disable_unprepare(adc->pclk);

	init_completion(&adc->completion);

	platform_set_drvdata(pdev, indio_dev);

	ret = devm_request_irq(dev, irq, rzg2l_adc_isr,
			       0, dev_name(dev), adc);
	if (ret < 0)
		goto assert_presetn;

	indio_dev->name = DRIVER_NAME;
	indio_dev->info = &rzg2l_adc_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = adc->data->channels;
	indio_dev->num_channels = adc->data->num_channels;

	pm_runtime_enable(dev);

	ret = devm_iio_device_register(dev, indio_dev);
	if (ret)
		goto err_iio_device_register;

	return 0;

unprepare_pclk:
	clk_disable_unprepare(adc->pclk);
err_iio_device_register:
	pm_runtime_disable(dev);
assert_presetn:
	reset_control_assert(adc->presetn);
assert_adrstn:
	reset_control_assert(adc->adrstn);
	return ret;
}

static int rzg2l_adc_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct rzg2l_adc *adc = iio_priv(indio_dev);

	reset_control_assert(adc->presetn);
	reset_control_assert(adc->adrstn);
	pm_runtime_disable(indio_dev->dev.parent);

	return 0;
}

static const struct of_device_id rzg2l_adc_match[] = {
	{ .compatible = "renesas,rzg2l-adc",},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rzg2l_adc_match);

static int __maybe_unused rzg2l_adc_pm_runtime_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct rzg2l_adc *adc = iio_priv(indio_dev);

	rzg2l_adc_pwr(adc, false);
	clk_disable_unprepare(adc->adclk);
	clk_disable_unprepare(adc->pclk);

	return 0;
}

static int __maybe_unused rzg2l_adc_pm_runtime_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct rzg2l_adc *adc = iio_priv(indio_dev);
	int ret;

	ret = clk_prepare_enable(adc->pclk);
	if (ret)
		return ret;

	ret = clk_prepare_enable(adc->adclk);
	if (ret)
		return ret;

	rzg2l_adc_pwr(adc, true);

	return 0;
}

static const struct dev_pm_ops rzg2l_adc_pm_ops = {
	SET_RUNTIME_PM_OPS(rzg2l_adc_pm_runtime_suspend,
			   rzg2l_adc_pm_runtime_resume,
			   NULL)
};

static struct platform_driver rzg2l_adc_driver = {
	.probe		= rzg2l_adc_probe,
	.remove		= rzg2l_adc_remove,
	.driver		= {
		.name		= DRIVER_NAME,
		.of_match_table = rzg2l_adc_match,
		.pm		= &rzg2l_adc_pm_ops,
	},
};

module_platform_driver(rzg2l_adc_driver);

MODULE_AUTHOR("Lad Prabhakar <prabhakar.mahadev-lad.rj@bp.renesas.com>");
MODULE_DESCRIPTION("Renesas RZ/G2L ADC driver");
MODULE_LICENSE("GPL v2");
