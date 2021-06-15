// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ALSA SoC DMIC Audio Layer
 *
 * Copyright 2021 Ban Tao <fengzheng923@gmail.com>
 *
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/of_device.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <sound/dmaengine_pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>


#define SUN50I_DMIC_EN_CTL		(0x00)
	#define SUN50I_DMIC_EN_CTL_GLOBE			BIT(8)
	#define SUN50I_DMIC_EN_CTL_CHAN(v)		((v) << 0)
	#define SUN50I_DMIC_EN_CTL_CHAN_MASK		GENMASK(7, 0)
#define SUN50I_DMIC_SR			(0x04)
	#define SUN50I_DMIC_SR_SAMOLE_RATE(v)		((v) << 0)
	#define SUN50I_DMIC_SR_SAMOLE_RATE_MASK		GENMASK(2, 0)
#define SUN50I_DMIC_CTL			(0x08)
	#define SUN50I_DMIC_CTL_OVERSAMPLE_RATE		BIT(0)
#define SUN50I_DMIC_DATA			(0x10)
#define SUN50I_DMIC_INTC			(0x14)
	#define SUN50I_DMIC_FIFO_DRQ_EN			BIT(2)
#define SUN50I_DMIC_INT_STA		(0x18)
	#define SUN50I_DMIC_INT_STA_OVERRUN_IRQ_PENDING	BIT(1)
	#define SUN50I_DMIC_INT_STA_DATA_IRQ_PENDING	BIT(0)
#define SUN50I_DMIC_RXFIFO_CTL		(0x1c)
	#define SUN50I_DMIC_RXFIFO_CTL_FLUSH		BIT(31)
	#define SUN50I_DMIC_RXFIFO_CTL_MODE		BIT(9)
	#define SUN50I_DMIC_RXFIFO_CTL_RESOLUTION	BIT(8)
#define SUN50I_DMIC_CH_NUM		(0x24)
	#define SUN50I_DMIC_CH_NUM_N(v)			((v) << 0)
	#define SUN50I_DMIC_CH_NUM_N_MASK		GENMASK(2, 0)
#define SUN50I_DMIC_CNT			(0x2c)
	#define SUN50I_DMIC_CNT_N			BIT(0)
#define SUN50I_DMIC_HPF_CTRL		(0x38)
#define SUN50I_DMIC_VERSION		(0x50)


struct sun50i_dmic_dev {
	struct platform_device *pdev;
	struct clk *dmic_clk;
	struct clk *apb_clk;
	struct reset_control *rst;
	struct regmap *regmap;
	struct snd_dmaengine_dai_dma_data dma_params_rx;
	unsigned int chan_en;
};

struct dmic_rate {
	unsigned int samplerate;
	unsigned int rate_bit;
};

static void sun50i_snd_rxctrl_enable(struct snd_pcm_substream *substream,
				    struct sun50i_dmic_dev *host, bool enable)
{
	if (enable) {
		/* DRQ ENABLE */
		regmap_update_bits(host->regmap, SUN50I_DMIC_INTC,
				   SUN50I_DMIC_FIFO_DRQ_EN,
				   SUN50I_DMIC_FIFO_DRQ_EN);
		regmap_update_bits(host->regmap, SUN50I_DMIC_EN_CTL,
				   SUN50I_DMIC_EN_CTL_CHAN_MASK,
				   SUN50I_DMIC_EN_CTL_CHAN(host->chan_en));
		/* Global enable */
		regmap_update_bits(host->regmap, SUN50I_DMIC_EN_CTL,
				   SUN50I_DMIC_EN_CTL_GLOBE,
				   SUN50I_DMIC_EN_CTL_GLOBE);
	} else {
		/* DRQ DISABLE */
		regmap_update_bits(host->regmap, SUN50I_DMIC_INTC,
				   SUN50I_DMIC_FIFO_DRQ_EN, 0);
		regmap_update_bits(host->regmap, SUN50I_DMIC_EN_CTL,
				   SUN50I_DMIC_EN_CTL_CHAN_MASK,
				   SUN50I_DMIC_EN_CTL_CHAN(0));
		/* Global disable */
		regmap_update_bits(host->regmap, SUN50I_DMIC_EN_CTL,
				   SUN50I_DMIC_EN_CTL_GLOBE, 0);
	}
}

static int sun50i_dmic_startup(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *cpu_dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct sun50i_dmic_dev *host = snd_soc_dai_get_drvdata(rtd->cpu_dai);

	/* only support capture */
	if (substream->stream != SNDRV_PCM_STREAM_CAPTURE)
		return -EINVAL;

	regmap_write(host->regmap, SUN50I_DMIC_INT_STA,
		     SUN50I_DMIC_INT_STA_OVERRUN_IRQ_PENDING |
		     SUN50I_DMIC_INT_STA_DATA_IRQ_PENDING);
	regmap_update_bits(host->regmap, SUN50I_DMIC_RXFIFO_CTL,
			   SUN50I_DMIC_RXFIFO_CTL_FLUSH,
			   SUN50I_DMIC_RXFIFO_CTL_FLUSH);
	regmap_write(host->regmap, SUN50I_DMIC_CNT, SUN50I_DMIC_CNT_N);

	return 0;
}

static int sun50i_dmic_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *cpu_dai)
{
	int i = 0;
	unsigned long rate = params_rate(params);
	unsigned int channels = params_channels(params);
	struct sun50i_dmic_dev *host = snd_soc_dai_get_drvdata(cpu_dai);
	struct platform_device *pdev = host->pdev;
	struct dmic_rate dmic_rate_s[] = {
		{44100, 0x0},
		{48000, 0x0},
		{22050, 0x2},
		{24000, 0x2},
		{11025, 0x4},
		{12000, 0x4},
		{32000, 0x1},
		{16000, 0x3},
		{8000,  0x5},
	};

	/* DMIC num is N+1 */
	regmap_update_bits(host->regmap, SUN50I_DMIC_CH_NUM,
			   SUN50I_DMIC_CH_NUM_N_MASK,
			   SUN50I_DMIC_CH_NUM_N(channels - 1));
	host->chan_en = (1 << channels) - 1;
	regmap_write(host->regmap, SUN50I_DMIC_HPF_CTRL, host->chan_en);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		regmap_update_bits(host->regmap, SUN50I_DMIC_RXFIFO_CTL,
				   SUN50I_DMIC_RXFIFO_CTL_MODE,
				   SUN50I_DMIC_RXFIFO_CTL_MODE);
		regmap_update_bits(host->regmap, SUN50I_DMIC_RXFIFO_CTL,
				   SUN50I_DMIC_RXFIFO_CTL_RESOLUTION, 0);
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		regmap_update_bits(host->regmap, SUN50I_DMIC_RXFIFO_CTL,
				   SUN50I_DMIC_RXFIFO_CTL_MODE, 0);
		regmap_update_bits(host->regmap, SUN50I_DMIC_RXFIFO_CTL,
				   SUN50I_DMIC_RXFIFO_CTL_RESOLUTION,
				   SUN50I_DMIC_RXFIFO_CTL_RESOLUTION);
		break;
	default:
		dev_err(&pdev->dev, "Invalid format!\n");
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(dmic_rate_s); i++) {
		if (dmic_rate_s[i].samplerate == rate) {
			regmap_update_bits(host->regmap, SUN50I_DMIC_SR,
					   SUN50I_DMIC_SR_SAMOLE_RATE_MASK,
					   SUN50I_DMIC_SR_SAMOLE_RATE(dmic_rate_s[i].rate_bit));
			break;
		}
	}
	if (i == ARRAY_SIZE(dmic_rate_s)) {
		dev_err(&pdev->dev, "Invalid rate!\n");
		return -EINVAL;
	}

	/* oversamplerate adjust */
	if (params_rate(params) >= 24000)
		regmap_update_bits(host->regmap, SUN50I_DMIC_CTL,
				   SUN50I_DMIC_CTL_OVERSAMPLE_RATE,
				   SUN50I_DMIC_CTL_OVERSAMPLE_RATE);
	else
		regmap_update_bits(host->regmap, SUN50I_DMIC_CTL,
				   SUN50I_DMIC_CTL_OVERSAMPLE_RATE, 0);

	return 0;
}

static int sun50i_dmic_trigger(struct snd_pcm_substream *substream, int cmd,
			       struct snd_soc_dai *dai)
{
	int ret = 0;
	struct sun50i_dmic_dev *host = snd_soc_dai_get_drvdata(dai);

	if (substream->stream != SNDRV_PCM_STREAM_CAPTURE)
		return -EINVAL;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		sun50i_snd_rxctrl_enable(substream, host, true);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		sun50i_snd_rxctrl_enable(substream, host, false);
		break;

	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int sun50i_dmic_set_sysclk(struct snd_soc_dai *dai, int clk_id,
				 unsigned int freq, int dir)
{
	struct sun50i_dmic_dev *host = snd_soc_dai_get_drvdata(dai);

	if (clk_set_rate(host->dmic_clk, freq)) {
		dev_err(dai->dev, "Freq : %u not support\n", freq);
		return -EINVAL;
	}

	return 0;
}

static int sun50i_dmic_soc_dai_probe(struct snd_soc_dai *dai)
{
	struct sun50i_dmic_dev *host = snd_soc_dai_get_drvdata(dai);

	snd_soc_dai_init_dma_data(dai, NULL, &host->dma_params_rx);

	return 0;
}

static const struct snd_soc_dai_ops sun50i_dmic_dai_ops = {
	.startup	= sun50i_dmic_startup,
	.trigger	= sun50i_dmic_trigger,
	.hw_params	= sun50i_dmic_hw_params,
	.set_sysclk	= sun50i_dmic_set_sysclk,
};

static const struct regmap_config sun50i_dmic_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = SUN50I_DMIC_VERSION,
	.cache_type = REGCACHE_NONE,
};

#define	SUN50I_DMIC_RATES (SNDRV_PCM_RATE_8000_48000 | SNDRV_PCM_RATE_KNOT)
#define SUN50I_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE)

static struct snd_soc_dai_driver sun50i_dmic_dai = {
	.capture = {
		.channels_min = 1,
		.channels_max = 8,
		.rates = SUN50I_DMIC_RATES,
		.formats = SUN50I_FORMATS,
	},
	.probe = sun50i_dmic_soc_dai_probe,
	.ops = &sun50i_dmic_dai_ops,
	.name = "dmic",
};

static const struct of_device_id sun50i_dmic_of_match[] = {
	{
		.compatible = "allwinner,sun50i-h6-dmic",
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sun50i_dmic_of_match);

static const struct snd_soc_component_driver sun50i_dmic_component = {
	.name		= "sun50i-dmic",
};

static int sun50i_dmic_runtime_suspend(struct device *dev)
{
	struct sun50i_dmic_dev *host  = dev_get_drvdata(dev);

	clk_disable_unprepare(host->dmic_clk);
	clk_disable_unprepare(host->apb_clk);

	return 0;
}

static int sun50i_dmic_runtime_resume(struct device *dev)
{
	struct sun50i_dmic_dev *host  = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(host->dmic_clk);
	if (ret)
		return ret;
	ret = clk_prepare_enable(host->apb_clk);
	if (ret)
		clk_disable_unprepare(host->dmic_clk);

	return ret;
}

static int sun50i_dmic_probe(struct platform_device *pdev)
{
	struct sun50i_dmic_dev *host;
	struct resource *res;
	int ret;
	void __iomem *base;

	host = devm_kzalloc(&pdev->dev, sizeof(*host), GFP_KERNEL);
	if (!host)
		return -ENOMEM;

	host->pdev = pdev;

	/* Get the addresses */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return dev_err_probe(&pdev->dev, PTR_ERR(base),
				     "get resource failed.\n");

	host->regmap = devm_regmap_init_mmio(&pdev->dev, base,
						&sun50i_dmic_regmap_config);

	/* Clocks */
	host->apb_clk = devm_clk_get(&pdev->dev, "apb");
	if (IS_ERR(host->apb_clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(host->apb_clk),
				     "failed to get apb clock.\n");

	host->dmic_clk = devm_clk_get(&pdev->dev, "dmic");
	if (IS_ERR(host->dmic_clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(host->dmic_clk),
				     "failed to get dmic clock.\n");

	host->dma_params_rx.addr = res->start + SUN50I_DMIC_DATA;
	host->dma_params_rx.maxburst = 8;
	host->dma_params_rx.addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;

	platform_set_drvdata(pdev, host);

	host->rst = devm_reset_control_get_optional_exclusive(&pdev->dev, NULL);
	if (IS_ERR(host->rst) && PTR_ERR(host->rst) == -EPROBE_DEFER) {
		ret = -EPROBE_DEFER;
		dev_err(&pdev->dev, "Failed to get reset: %d\n", ret);
		return ret;
	}
	if (!IS_ERR(host->rst))
		reset_control_deassert(host->rst);

	ret = devm_snd_soc_register_component(&pdev->dev,
				&sun50i_dmic_component, &sun50i_dmic_dai, 1);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to register component.\n");

	pm_runtime_enable(&pdev->dev);
	if (!pm_runtime_enabled(&pdev->dev)) {
		ret = sun50i_dmic_runtime_resume(&pdev->dev);
		if (ret)
			goto err_unregister;
	}

	ret = devm_snd_dmaengine_pcm_register(&pdev->dev, NULL, 0);
	if (ret)
		goto err_suspend;

	return 0;
err_suspend:
	if (!pm_runtime_status_suspended(&pdev->dev))
		sun50i_dmic_runtime_suspend(&pdev->dev);
err_unregister:
	pm_runtime_disable(&pdev->dev);
	return ret;
}

static int sun50i_dmic_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		sun50i_dmic_runtime_suspend(&pdev->dev);

	return 0;
}

static const struct dev_pm_ops sun50i_dmic_pm = {
	SET_RUNTIME_PM_OPS(sun50i_dmic_runtime_suspend,
			   sun50i_dmic_runtime_resume, NULL)
};

static struct platform_driver sun50i_dmic_driver = {
	.driver		= {
		.name	= "sun50i-dmic",
		.of_match_table = of_match_ptr(sun50i_dmic_of_match),
		.pm	= &sun50i_dmic_pm,
	},
	.probe		= sun50i_dmic_probe,
	.remove		= sun50i_dmic_remove,
};

module_platform_driver(sun50i_dmic_driver);

MODULE_DESCRIPTION("Allwinner sun50i DMIC SoC Interface");
MODULE_AUTHOR("Ban Tao <fengzheng923@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:sun50i-dmic");
