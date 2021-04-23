// SPDX-License-Identifier: GPL-2.0+
// Copyright 2017-2021 NXP

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/gpio/consumer.h>
#include <linux/of_device.h>
#include <linux/i2c.h>
#include <linux/of_gpio.h>
#include <linux/clk.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include <sound/pcm.h>
#include <sound/soc-dapm.h>

#include "fsl_sai.h"

#define MAX_CODECS 2

enum codec_type {
	CODEC_AK5558 = 1,
	CODEC_AK4458,
	CODEC_AK4497,
	CODEC_AK5552,
};

/*
 * Mapping LRCK fs and frame width, table 3 & 4 in datasheet
 * @rmin: min rate
 * @rmax: max rate
 * @wmin: min frame ratio
 * @wmax: max frame ratio
 */
struct imx_akcodec_fs_mul {
	unsigned int rmin;
	unsigned int rmax;
	unsigned int wmin;
	unsigned int wmax;
};

/*
 * Mapping TDM mode and frame width
 */
struct imx_akcodec_tdm_fs_mul {
	unsigned int min;
	unsigned int max;
	unsigned int mul;
};

/*
 * struct imx_akcodec_plat_data - specific info for codecs
 *
 * @fs_mul: ratio of mclk/fs for normal mode
 * @tdm_fs_mul: ratio of mclk/fs for tdm mode
 * @support_rates: supported sample rate
 * @support_tdm_rates: supported sample rate for tdm mode
 * @support_channels: supported channels
 * @support_tdm_channels: supported channels for tdm mode
 * @num_fs_mul: ARRAY_SIZE of fs_mul
 * @num_tdm_fs_mul: ARRAY_SIZE of tdm_fs_mul
 * @num_rates: ARRAY_SIZE of support_rates
 * @num_tdm_rates: ARRAY_SIZE of support_tdm_rates
 * @num_channels: ARRAY_SIZE of support_channels
 * @num_tdm_channels: ARRAY_SIZE of support_tdm_channels
 * @num_codecs: codec number, maximum is MAX_CODECS
 * @playback_only: only playback supported
 * @capture_only: only capture supported
 * @one2one_ratio: if mclk/bclk = 1 or not
 * @tdm_is_support: if this codec support tdm
 * @type: codec type
 * @dai_name: codec dai name
 */
struct imx_akcodec_plat_data {
	struct imx_akcodec_fs_mul  *fs_mul;
	struct imx_akcodec_tdm_fs_mul  *tdm_fs_mul;
	const u32 *support_rates;
	const u32 *support_tdm_rates;
	const u32 *support_channels;
	const u32 *support_tdm_channels;
	unsigned int num_fs_mul;
	unsigned int num_tdm_fs_mul;
	unsigned int num_rates;
	unsigned int num_tdm_rates;
	unsigned int num_channels;
	unsigned int num_tdm_channels;
	unsigned int num_codecs;
	unsigned int playback_only;
	unsigned int capture_only;
	bool one2one_ratio;
	bool tdm_is_support;
	enum codec_type type;
	const char *dai_name;
};

/*
 * struct imx_akcodec_data - platform device data
 *
 * @card: card instance
 * @codec_conf: pointer of codec conf
 * @plat_data: pointer of imx_akcodec_plat_data
 * @dai_link: snd_soc_dai_link
 * @codecs: codec dai link component
 * @dapm_routes: pointer of dapm_routes
 * @asrc_pdev: pointer of asrc_pdev
 * @num_codec_conf: number of codec conf
 * @num_dapm_routes: number of dapm_routes
 * @slots: slots value
 * @slot_width: slot width value
 * @tdm_mode: tdm mode enabled or not
 * @asrc_rate: asrc rates
 * @asrc_format: asrc format
 */
struct imx_akcodec_data {
	struct snd_soc_card card;
	struct snd_soc_codec_conf *codec_conf;
	const struct imx_akcodec_plat_data *plat_data;
	struct snd_soc_dai_link dai_link[3];
	struct snd_soc_dai_link_component codecs[MAX_CODECS];
	struct snd_soc_dapm_route *dapm_routes;
	struct platform_device *asrc_pdev;
	int num_codec_conf;
	int num_dapm_routes;
	unsigned int slots;
	unsigned int slot_width;
	bool tdm_mode;
	u32 asrc_rate;
	u32 asrc_format;
};

struct imx_akcodec_fs_mul ak4458_fs_mul[] = {
	/* Normal, < 32kHz */
	{ .rmin = 8000,   .rmax = 24000,  .wmin = 1024, .wmax = 1024, },
	/* Normal, 32kHz */
	{ .rmin = 32000,  .rmax = 32000,  .wmin = 256,  .wmax = 1024, },
	/* Normal */
	{ .rmin = 44100,  .rmax = 48000,  .wmin = 256,  .wmax = 768,  },
	/* Double */
	{ .rmin = 88200,  .rmax = 96000,  .wmin = 256,  .wmax = 512,  },
	/* Quad */
	{ .rmin = 176400, .rmax = 192000, .wmin = 128,  .wmax = 256,  },
	/* Oct */
	{ .rmin = 352800, .rmax = 384000, .wmin = 32,   .wmax = 128,  },
	/* Hex */
	{ .rmin = 705600, .rmax = 768000, .wmin = 16,   .wmax = 64,   },
};

struct imx_akcodec_tdm_fs_mul ak4458_tdm_fs_mul[] = {
	/*
	 * Table 13	- Audio Interface Format
	 * For TDM mode, MCLK should is set to
	 * obtained from 2 * slots * slot_width
	 */
	{ .min = 128,	.max = 128,	.mul = 256  }, /* TDM128 */
	{ .min = 256,	.max = 256,	.mul = 512  }, /* TDM256 */
	{ .min = 512,	.max = 512,	.mul = 1024  }, /* TDM512 */
};

struct imx_akcodec_fs_mul ak4497_fs_mul[] = {
	/**
	 * Table 7      - mapping multiplier and speed mode
	 * Tables 8 & 9 - mapping speed mode and LRCK fs
	 */
	{ .rmin = 8000,   .rmax = 32000,  .wmin = 1024, .wmax = 1024, }, /* Normal, <= 32kHz */
	{ .rmin = 44100,  .rmax = 48000,  .wmin = 512,  .wmax = 512, }, /* Normal */
	{ .rmin = 88200,  .rmax = 96000,  .wmin = 256,  .wmax = 256, }, /* Double */
	{ .rmin = 176400, .rmax = 192000, .wmin = 128,  .wmax = 128, }, /* Quad */
	{ .rmin = 352800, .rmax = 384000, .wmin = 128,  .wmax = 128, }, /* Oct */
	{ .rmin = 705600, .rmax = 768000, .wmin = 64,   .wmax = 64, }, /* Hex */
};

/*
 * Auto MCLK selection based on LRCK for Normal Mode
 * (Table 4 from datasheet)
 */
struct imx_akcodec_fs_mul ak5558_fs_mul[] = {
	{ .rmin = 8000,   .rmax = 32000,  .wmin = 1024, .wmax = 1024, },
	{ .rmin = 44100,  .rmax = 48000,  .wmin = 512,  .wmax = 512, },
	{ .rmin = 88200,  .rmax = 96000,  .wmin = 256,  .wmax = 256, },
	{ .rmin = 176400, .rmax = 192000, .wmin = 128,  .wmax = 128, },
	{ .rmin = 352800, .rmax = 384000, .wmin = 64,   .wmax = 64, },
	{ .rmin = 705600, .rmax = 768000, .wmin = 32,   .wmax = 32, },
};

/*
 * MCLK and BCLK selection based on TDM mode
 * because of SAI we also add the restriction: MCLK >= 2 * BCLK
 * (Table 9 from datasheet)
 */
struct imx_akcodec_tdm_fs_mul ak5558_tdm_fs_mul[] = {
	{ .min = 128,	.max = 128,	.mul = 256 },
	{ .min = 256,	.max = 256,	.mul = 512 },
	{ .min = 512,	.max = 512,	.mul = 1024 },
};

static const u32 akcodec_rates[] = {
	8000, 11025, 16000, 22050, 32000, 44100, 48000, 88200,
	96000, 176400, 192000, 352800, 384000, 705600, 768000,
};

static const u32 akcodec_tdm_rates[] = {
	8000, 16000, 32000, 48000, 96000,
};

static const u32 ak4458_channels[] = {
	1, 2, 4, 6, 8, 10, 12, 14, 16,
};

static const u32 ak4458_tdm_channels[] = {
	1, 2, 3, 4, 5, 6, 7, 8, 16,
};

static const u32 ak5558_channels[] = {
	1, 2, 4, 6, 8,
};

static const u32 ak5558_tdm_channels[] = {
	1, 2, 3, 4, 5, 6, 7, 8,
};

static const struct imx_akcodec_plat_data imx8mq_ak4458 = {
	.fs_mul = ak4458_fs_mul,
	.num_fs_mul = ARRAY_SIZE(ak4458_fs_mul),
	.tdm_fs_mul = ak4458_tdm_fs_mul,
	.num_tdm_fs_mul = ARRAY_SIZE(ak4458_tdm_fs_mul),
	.support_rates = akcodec_rates,
	.num_rates = ARRAY_SIZE(akcodec_rates),
	.support_tdm_rates = akcodec_tdm_rates,
	.num_tdm_rates = ARRAY_SIZE(akcodec_tdm_rates),
	.support_channels = ak4458_channels,
	.num_channels = ARRAY_SIZE(ak4458_channels),
	.support_tdm_channels = ak4458_tdm_channels,
	.num_tdm_channels = ARRAY_SIZE(ak4458_tdm_channels),
	.one2one_ratio = false,
	.dai_name = "ak4458-aif",
	.num_codecs = MAX_CODECS,
	.tdm_is_support = true,
	.playback_only = 1,
	.capture_only = 0,
	.type = CODEC_AK4458,
};

static const struct imx_akcodec_plat_data imx_ak4458 = {
	.fs_mul = ak4458_fs_mul,
	.num_fs_mul = ARRAY_SIZE(ak4458_fs_mul),
	.tdm_fs_mul = ak4458_tdm_fs_mul,
	.num_tdm_fs_mul = ARRAY_SIZE(ak4458_tdm_fs_mul),
	.support_rates = akcodec_rates,
	.num_rates = ARRAY_SIZE(akcodec_rates),
	.support_tdm_rates = akcodec_tdm_rates,
	.num_tdm_rates = ARRAY_SIZE(akcodec_tdm_rates),
	.support_channels = ak4458_channels,
	.num_channels = ARRAY_SIZE(ak4458_channels),
	.support_tdm_channels = ak4458_tdm_channels,
	.num_tdm_channels = ARRAY_SIZE(ak4458_tdm_channels),
	.one2one_ratio = true,
	.dai_name = "ak4458-aif",
	.num_codecs = MAX_CODECS,
	.tdm_is_support = true,
	.playback_only = 1,
	.capture_only = 0,
	.type = CODEC_AK4458,
};

static const struct imx_akcodec_plat_data imx8mq_ak4497 = {
	.fs_mul = ak4497_fs_mul,
	.num_fs_mul = ARRAY_SIZE(ak4497_fs_mul),
	.support_rates = akcodec_rates,
	.num_rates = ARRAY_SIZE(akcodec_rates),
	.support_channels = ak4458_channels,
	.num_channels = ARRAY_SIZE(ak4458_channels),
	.one2one_ratio = false,
	.dai_name = "ak4497-aif",
	.num_codecs = 1,
	.tdm_is_support = false,
	.playback_only = 1,
	.capture_only = 0,
	.type = CODEC_AK4497,
};

static const struct imx_akcodec_plat_data imx_ak4497 = {
	.fs_mul = ak4497_fs_mul,
	.num_fs_mul = ARRAY_SIZE(ak4497_fs_mul),
	.support_rates = akcodec_rates,
	.num_rates = ARRAY_SIZE(akcodec_rates),
	.support_channels = ak4458_channels,
	.num_channels = ARRAY_SIZE(ak4458_channels),
	.one2one_ratio = true,
	.dai_name = "ak4497-aif",
	.num_codecs = 1,
	.tdm_is_support = false,
	.playback_only = 1,
	.capture_only = 0,
	.type = CODEC_AK4497,
};

static const struct imx_akcodec_plat_data imx8mq_ak5558 = {
	.fs_mul = ak5558_fs_mul,
	.num_fs_mul = ARRAY_SIZE(ak5558_fs_mul),
	.tdm_fs_mul = ak5558_tdm_fs_mul,
	.num_tdm_fs_mul = ARRAY_SIZE(ak5558_tdm_fs_mul),
	.support_rates = akcodec_rates,
	.num_rates = ARRAY_SIZE(akcodec_rates),
	.support_tdm_rates = akcodec_tdm_rates,
	.num_tdm_rates = ARRAY_SIZE(akcodec_tdm_rates),
	.support_channels = ak5558_channels,
	.num_channels = ARRAY_SIZE(ak5558_channels),
	.support_tdm_channels = ak5558_tdm_channels,
	.num_tdm_channels = ARRAY_SIZE(ak5558_tdm_channels),
	.one2one_ratio = false,
	.dai_name = "ak5558-aif",
	.num_codecs = 1,
	.tdm_is_support = true,
	.playback_only = 0,
	.capture_only = 1,
	.type = CODEC_AK5558,
};

static const struct imx_akcodec_plat_data imx_ak5558 = {
	.fs_mul = ak5558_fs_mul,
	.num_fs_mul = ARRAY_SIZE(ak5558_fs_mul),
	.tdm_fs_mul = ak5558_tdm_fs_mul,
	.num_tdm_fs_mul = ARRAY_SIZE(ak5558_tdm_fs_mul),
	.support_rates = akcodec_rates,
	.num_rates = ARRAY_SIZE(akcodec_rates),
	.support_tdm_rates = akcodec_tdm_rates,
	.num_tdm_rates = ARRAY_SIZE(akcodec_tdm_rates),
	.support_channels = ak5558_channels,
	.num_channels = ARRAY_SIZE(ak5558_channels),
	.support_tdm_channels = ak5558_tdm_channels,
	.num_tdm_channels = ARRAY_SIZE(ak5558_tdm_channels),
	.one2one_ratio = true,
	.dai_name = "ak5558-aif",
	.num_codecs = 1,
	.tdm_is_support = true,
	.playback_only = 0,
	.capture_only = 1,
	.type = CODEC_AK5558,
};

static bool format_is_dsd(struct snd_pcm_hw_params *params)
{
	snd_pcm_format_t format = params_format(params);

	switch (format) {
	case SNDRV_PCM_FORMAT_DSD_U8:
	case SNDRV_PCM_FORMAT_DSD_U16_LE:
	case SNDRV_PCM_FORMAT_DSD_U16_BE:
	case SNDRV_PCM_FORMAT_DSD_U32_LE:
	case SNDRV_PCM_FORMAT_DSD_U32_BE:
		return true;
	default:
		return false;
	}
}

static struct snd_soc_dapm_widget imx_akcodec_dapm_widgets[] = {
	SND_SOC_DAPM_LINE("Line Out", NULL),
	SND_SOC_DAPM_LINE("Line In", NULL),
};

static unsigned long akcodec_get_mclk_rate(struct snd_pcm_substream *substream,
					   struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct imx_akcodec_data *data = snd_soc_card_get_drvdata(rtd->card);
	const struct imx_akcodec_plat_data *plat_data = data->plat_data;
	unsigned int width = data->slots * data->slot_width;
	unsigned int rate = params_rate(params);
	int i;

	if (data->tdm_mode) {
		for (i = 0; i < plat_data->num_tdm_fs_mul; i++) {
			/* min = max = slots * slots_width */
			if (width != plat_data->tdm_fs_mul[i].min)
				continue;
			return rate * plat_data->tdm_fs_mul[i].mul;
		}
	} else {
		for (i = 0; i < plat_data->num_fs_mul; i++) {
			if (rate >= plat_data->fs_mul[i].rmin &&
			    rate <= plat_data->fs_mul[i].rmax) {
				width = max(width, plat_data->fs_mul[i].wmin);
				width = min(width, plat_data->fs_mul[i].wmax);

				/* Adjust SAI bclk:mclk ratio */
				width *= plat_data->one2one_ratio ? 1 : 2;

				return rate * width;
			}
		}
	}

	/* Let DAI manage clk frequency by default */
	return 0;
}

static int imx_aif_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct snd_soc_card *card = rtd->card;
	struct imx_akcodec_data *data = snd_soc_card_get_drvdata(card);
	unsigned int fmt = SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS;
	bool is_dsd = format_is_dsd(params);
	struct device *dev = card->dev;
	unsigned long mclk_freq;
	int ret, i;

	if (is_dsd) {
		data->slots = 1;
		data->slot_width = params_width(params);
		fmt |= SND_SOC_DAIFMT_PDM;
	} else if (data->tdm_mode) {
		data->slots = 8;
		data->slot_width = 32;
		fmt |= SND_SOC_DAIFMT_DSP_B;
	} else {
		data->slots = 2;
		data->slot_width = params_physical_width(params);
		fmt |= SND_SOC_DAIFMT_I2S;
	}

	ret = snd_soc_dai_set_fmt(cpu_dai, fmt);
	if (ret) {
		dev_err(dev, "failed to set cpu dai fmt: %d\n", ret);
		return ret;
	}
	ret = snd_soc_dai_set_tdm_slot(cpu_dai,
				       BIT(data->slots) - 1,
				       BIT(data->slots) - 1,
				       data->slots, data->slot_width);
	if (ret) {
		dev_err(dev, "failed to set cpu dai tdm slot: %d\n", ret);
		return ret;
	}

	for (i = 0; i < rtd->num_codecs; i++) {
		struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, i);

		ret = snd_soc_dai_set_fmt(codec_dai, fmt);
		if (ret) {
			dev_err(dev, "failed to set codec dai[%d] fmt: %d\n", i, ret);
			return ret;
		}
		ret = snd_soc_dai_set_tdm_slot(codec_dai,
					       BIT(data->slots) - 1,
					       BIT(data->slots) - 1,
					       data->slots, data->slot_width);
		if (ret) {
			dev_err(dev, "failed to set codec dai[%d] tdm slot: %d\n", i, ret);
			return ret;
		}
	}

	/* Set MCLK freq */
	mclk_freq = akcodec_get_mclk_rate(substream, params);
	/* Use the maximum freq from DSD512 (512*44100 = 22579200) */
	if (is_dsd)
		mclk_freq = 22579200;

	ret = snd_soc_dai_set_sysclk(cpu_dai, FSL_SAI_CLK_MAST1, mclk_freq,
				     SND_SOC_CLOCK_OUT);
	if (ret < 0)
		dev_err(dev, "failed to set cpui dai mclk1 rate (%lu): %d\n",
			mclk_freq, ret);
	return ret;
}

static int ak5558_hw_rule_rate(struct snd_pcm_hw_params *p, struct snd_pcm_hw_rule *r)
{
	struct imx_akcodec_data *data = r->private;
	struct snd_interval t = { .min = 8000, .max = 8000, };
	unsigned long mclk_freq;
	unsigned int fs;
	int i;

	fs = hw_param_interval(p, SNDRV_PCM_HW_PARAM_SAMPLE_BITS)->min;
	fs *= data->tdm_mode ? 8 : 2;

	/* Identify maximum supported rate */
	for (i = 0; i < ARRAY_SIZE(akcodec_rates); i++) {
		mclk_freq = fs * akcodec_rates[i];
		/* Adjust SAI bclk:mclk ratio */
		mclk_freq *= data->plat_data->one2one_ratio ? 1 : 2;

		/* Skip rates for which MCLK is beyond supported value */
		if (mclk_freq > 36864000)
			continue;

		if (t.max < akcodec_rates[i])
			t.max = akcodec_rates[i];
	}

	return snd_interval_refine(hw_param_interval(p, r->var), &t);
}

static int imx_aif_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct imx_akcodec_data *data = snd_soc_card_get_drvdata(card);
	static struct snd_pcm_hw_constraint_list constraint_rates;
	static struct snd_pcm_hw_constraint_list constraint_channels;
	int ret = 0;

	if (data->tdm_mode) {
		constraint_channels.list = data->plat_data->support_tdm_channels;
		constraint_channels.count = data->plat_data->num_tdm_channels;
		constraint_rates.list = data->plat_data->support_tdm_rates;
		constraint_rates.count = data->plat_data->num_tdm_rates;
	} else {
		constraint_channels.list = data->plat_data->support_channels;
		constraint_channels.count = data->plat_data->num_channels;
		constraint_rates.list = data->plat_data->support_rates;
		constraint_rates.count = data->plat_data->num_rates;
	}

	ret = snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
					 &constraint_channels);
	if (ret)
		return ret;

	ret = snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
					 &constraint_rates);
	if (ret)
		return ret;

	if (data->plat_data->type == CODEC_AK5558)
		ret = snd_pcm_hw_rule_add(substream->runtime, 0,
					  SNDRV_PCM_HW_PARAM_RATE,
					  ak5558_hw_rule_rate, data,
					  SNDRV_PCM_HW_PARAM_SAMPLE_BITS, -1);

	return ret;
}

static int imx_aif_hw_params_be(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	struct snd_soc_card *card = rtd->card;
	struct device *dev = card->dev;
	struct imx_akcodec_data *data = snd_soc_card_get_drvdata(card);
	unsigned int slots, slot_width;
	unsigned long mclk_freq;
	unsigned int fmt;
	int ret;

	/* In order to support odd channels, force tdm mode for FE-BE case */
	if (data->plat_data->tdm_is_support) {
		fmt = SND_SOC_DAIFMT_DSP_B | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS;
		/* support TDM256 (8 slots * 32 bits/per slot) */
		slots = 8;
		slot_width = 32;
	} else {
		fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS;
		slots = 2;
		slot_width = 32;
	}

	ret = snd_soc_dai_set_fmt(cpu_dai, fmt);
	if (ret) {
		dev_err(dev, "failed to set cpu dai fmt: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_fmt(codec_dai, fmt);
	if (ret) {
		dev_err(dev, "failed to set codec dai fmt: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_tdm_slot(cpu_dai, BIT(slots) - 1, BIT(slots) - 1,
				       slots, slot_width);
	if (ret) {
		dev_err(dev, "failed to set cpu dai tdm slot: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_tdm_slot(codec_dai, BIT(slots) - 1, BIT(slots) - 1,
				       slots, slot_width);
	if (ret) {
		dev_err(dev, "failed to set codec dai fmt: %d\n", ret);
		return ret;
	}

	mclk_freq = akcodec_get_mclk_rate(substream, params);
	ret = snd_soc_dai_set_sysclk(cpu_dai, FSL_SAI_CLK_MAST1, mclk_freq,
				     SND_SOC_CLOCK_OUT);
	if (ret < 0)
		dev_err(dev, "failed to set cpu_dai mclk1 rate %lu\n",
			mclk_freq);

	return ret;
}

static struct snd_soc_ops imx_aif_ops = {
	.hw_params = imx_aif_hw_params,
	.startup = imx_aif_startup,
};

static struct snd_soc_ops imx_aif_ops_be = {
	.hw_params = imx_aif_hw_params_be,
};

static int be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
			      struct snd_pcm_hw_params *params)
{
	struct snd_soc_card *card = rtd->card;
	struct imx_akcodec_data *data = snd_soc_card_get_drvdata(card);
	struct snd_interval *rate;
	struct snd_mask *mask;

	if (!data->asrc_pdev)
		return -EINVAL;

	rate = hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	rate->max = data->asrc_rate;
	rate->min = data->asrc_rate;

	mask = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);
	snd_mask_none(mask);
	snd_mask_set(mask, data->asrc_format);

	return 0;
}

static int imx_akcodec_probe(struct platform_device *pdev)
{
	struct snd_soc_dai_link_component *dlc;
	struct device_node *codec_np[MAX_CODECS];
	struct device_node *cpu_np = NULL;
	struct imx_akcodec_data *data;
	struct device_node *asrc_np = NULL;
	char tmp[MAX_CODECS][8];
	int ret, i;
	u32 width;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->plat_data = of_device_get_match_data(&pdev->dev);

	dlc = devm_kzalloc(&pdev->dev, 9 * sizeof(*dlc), GFP_KERNEL);
	if (!dlc)
		return -ENOMEM;

	cpu_np = of_parse_phandle(pdev->dev.of_node, "audio-cpu", 0);
	if (!cpu_np) {
		dev_err(&pdev->dev, "audio dai phandle missing or invalid\n");
		ret = -EINVAL;
		goto fail;
	}

	/* Parse codec node */
	for (i = 0; i < data->plat_data->num_codecs; i++) {
		codec_np[i] = of_parse_phandle(pdev->dev.of_node, "audio-codec", i);
		if (!codec_np[i]) {
			dev_err(&pdev->dev, "audio codec phandle missing or invalid\n");
			ret = -EINVAL;
			goto fail;
		}
	}

	asrc_np = of_parse_phandle(pdev->dev.of_node, "audio-asrc", 0);
	if (asrc_np)
		data->asrc_pdev = of_find_device_by_node(asrc_np);

	if (of_find_property(pdev->dev.of_node, "fsl,tdm", NULL) &&
	    data->plat_data->tdm_is_support)
		data->tdm_mode = true;

	data->num_codec_conf = data->plat_data->num_codecs;
	data->codec_conf = devm_kzalloc(&pdev->dev,
					data->num_codec_conf * sizeof(struct snd_soc_codec_conf),
					GFP_KERNEL);
	if (!data->codec_conf) {
		ret = -ENOMEM;
		goto fail;
	}

	data->num_dapm_routes = data->plat_data->num_codecs + 1;
	data->dapm_routes = devm_kcalloc(&pdev->dev, data->num_dapm_routes,
					 sizeof(struct snd_soc_dapm_route),
					 GFP_KERNEL);
	if (!data->dapm_routes) {
		ret = -ENOMEM;
		goto fail;
	}

	data->dai_link[0].name = "akcodec";
	data->dai_link[0].stream_name = "Audio";
	data->dai_link[0].cpus = &dlc[0];
	data->dai_link[0].platforms = &dlc[1];
	data->dai_link[0].codecs = data->codecs;
	data->dai_link[0].num_cpus = 1;
	data->dai_link[0].num_platforms = 1;
	data->dai_link[0].num_codecs = data->plat_data->num_codecs;
	data->dai_link[0].cpus->of_node = cpu_np;
	data->dai_link[0].platforms->of_node = cpu_np;

	for (i = 0; i < data->plat_data->num_codecs; i++) {
		sprintf(tmp[i], "%d", i);
		data->codec_conf[i].name_prefix = tmp[i];
		data->codec_conf[i].dlc.of_node = codec_np[i];
		data->codecs[i].of_node = codec_np[i];
		data->codecs[i].dai_name = data->plat_data->dai_name;

		if (data->plat_data->playback_only) {
			data->dapm_routes[i].sink =
				devm_kasprintf(&pdev->dev, GFP_KERNEL, "%s %s",
					       tmp[i], "Playback");
			data->dapm_routes[i].source = "CPU-Playback";
		} else {
			data->dapm_routes[i].source =
				devm_kasprintf(&pdev->dev, GFP_KERNEL, "%s %s",
					       tmp[i], "Capture");
			data->dapm_routes[i].sink = "CPU-Capture";
		}
	}

	if (data->plat_data->playback_only) {
		data->dapm_routes[i].sink = "CPU-Playback";
		data->dapm_routes[i].source = "ASRC-Playback";
	} else {
		data->dapm_routes[i].sink = "ASRC-Capture";
		data->dapm_routes[i].source = "CPU-Capture";
	}

	data->dai_link[0].ops = &imx_aif_ops;
	data->dai_link[0].ignore_pmdown_time = 1;
	data->dai_link[0].playback_only = data->plat_data->playback_only;
	data->dai_link[0].capture_only = data->plat_data->capture_only;

	data->card.codec_conf = data->codec_conf;
	data->card.num_configs = data->num_codec_conf;
	data->card.dai_link = &data->dai_link[0];
	data->card.num_links = 1;
	data->card.dev = &pdev->dev;
	data->card.owner = THIS_MODULE;
	data->card.dapm_widgets = imx_akcodec_dapm_widgets;
	data->card.num_dapm_widgets = ARRAY_SIZE(imx_akcodec_dapm_widgets);

	/*if there is no asrc controller, we only enable one device*/
	if (data->asrc_pdev) {
		data->dai_link[1].name = "HiFi-ASRC-FE";
		data->dai_link[1].stream_name = "HiFi-ASRC-FE";
		data->dai_link[1].dynamic = 1;
		data->dai_link[1].ignore_pmdown_time = 1;
		data->dai_link[1].dpcm_playback = data->plat_data->playback_only;
		data->dai_link[1].dpcm_capture = data->plat_data->capture_only;
		data->dai_link[1].dpcm_merged_chan = 1;

		data->dai_link[1].cpus = &dlc[2];
		data->dai_link[1].platforms = &dlc[3];
		data->dai_link[1].codecs = &dlc[4];
		data->dai_link[1].num_cpus = 1;
		data->dai_link[1].num_platforms = 1;
		data->dai_link[1].num_codecs = 1;
		data->dai_link[1].cpus->of_node  = asrc_np;
		data->dai_link[1].platforms->of_node  = asrc_np;
		data->dai_link[1].codecs->name = "snd-soc-dummy";
		data->dai_link[1].codecs->dai_name = "snd-soc-dummy-dai";

		data->dai_link[2].cpus = &dlc[5];
		data->dai_link[2].platforms = &dlc[6];
		data->dai_link[2].codecs = data->codecs;
		data->dai_link[2].num_cpus = 1;
		data->dai_link[2].num_platforms = 1;
		data->dai_link[2].num_codecs = data->plat_data->num_codecs;

		data->dai_link[2].name = "HiFi-ASRC-BE";
		data->dai_link[2].stream_name = "HiFi-ASRC-BE";
		data->dai_link[2].cpus->of_node = cpu_np;
		data->dai_link[2].platforms->name = "snd-soc-dummy";
		data->dai_link[2].ops = &imx_aif_ops_be;
		data->dai_link[2].be_hw_params_fixup = be_hw_params_fixup;
		data->dai_link[2].no_pcm = 1;
		data->dai_link[2].ignore_pmdown_time = 1;
		data->dai_link[2].dpcm_playback = data->plat_data->playback_only;
		data->dai_link[2].dpcm_capture = data->plat_data->capture_only;

		data->card.dapm_routes = data->dapm_routes;
		data->card.num_dapm_routes = data->num_dapm_routes;
		data->card.num_links = 3;

		ret = of_property_read_u32(asrc_np, "fsl,asrc-rate",
					   &data->asrc_rate);
		if (ret) {
			dev_err(&pdev->dev, "failed to get output rate\n");
			ret = -EINVAL;
			goto fail;
		}

		ret = of_property_read_u32(asrc_np, "fsl,asrc-format",
					   &data->asrc_format);
		if (ret) {
			/* Fallback to old binding; translate to asrc_format */
			ret = of_property_read_u32(asrc_np, "fsl,asrc-width",
						   &width);
			if (ret) {
				dev_err(&pdev->dev,
					"failed to decide output format\n");
				goto fail;
			}

			if (width == 24)
				data->asrc_format = SNDRV_PCM_FORMAT_S24_LE;
			else
				data->asrc_format = SNDRV_PCM_FORMAT_S16_LE;
		}
	}

	ret = snd_soc_of_parse_card_name(&data->card, "model");
	if (ret)
		goto fail;

	snd_soc_card_set_drvdata(&data->card, data);
	ret = devm_snd_soc_register_card(&pdev->dev, &data->card);
	if (ret) {
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n", ret);
		goto fail;
	}

	ret = 0;
fail:
	if (cpu_np)
		of_node_put(cpu_np);

	for (i = 0; i < data->plat_data->num_codecs; i++)
		if (codec_np[i])
			of_node_put(codec_np[i]);

	return ret;
}

static const struct of_device_id imx_akcodec_dt_ids[] = {
	{ .compatible = "fsl,imx8mq-audio-ak4458", .data = &imx8mq_ak4458, },
	{ .compatible = "fsl,imx8mq-audio-ak4497", .data = &imx8mq_ak4497, },
	{ .compatible = "fsl,imx8mq-audio-ak5558", .data = &imx8mq_ak5558, },
	{ .compatible = "fsl,imx-audio-ak4458", .data = &imx_ak4458, },
	{ .compatible = "fsl,imx-audio-ak4497", .data = &imx_ak4497, },
	{ .compatible = "fsl,imx-audio-ak5558", .data = &imx_ak5558, },
	{ .compatible = "fsl,imx-audio-ak5552", .data = &imx_ak5558, },
	{ },
};
MODULE_DEVICE_TABLE(of, imx_akcodec_dt_ids);

static struct platform_driver imx_akcodec_driver = {
	.driver = {
		.name = "imx-akcodec",
		.pm = &snd_soc_pm_ops,
		.of_match_table = imx_akcodec_dt_ids,
	},
	.probe = imx_akcodec_probe,
};
module_platform_driver(imx_akcodec_driver);

MODULE_DESCRIPTION("Freescale i.MX AK CODEC ASoC machine driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:imx-akcodec");
