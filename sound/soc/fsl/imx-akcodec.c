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
#include <sound/simple_card_utils.h>

#include "fsl_sai.h"

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
 * @one2one_ratio: if mclk/bclk = 1 or not
 * @type: codec type
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
	bool one2one_ratio;
	enum codec_type type;
};

/*
 * struct imx_akcodec_data - platform device data
 *
 * @card: card instance
 * @plat_data: pointer of imx_akcodec_plat_data
 * @dapm_routes: pointer of dapm_routes
 * @num_dapm_routes: number of dapm_routes
 * @slots: slots value
 * @slot_width: slot width value
 * @tdm_mode: tdm mode enabled or not
 * @asrc_rate: asrc rates
 * @asrc_format: asrc format
 */
struct imx_akcodec_data {
	struct snd_soc_card card;
	const struct imx_akcodec_plat_data *plat_data;
	struct snd_soc_dapm_route *dapm_routes;
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

static const struct imx_akcodec_plat_data imx8mq_akcodec = {
	.support_rates = akcodec_rates,
	.num_rates = ARRAY_SIZE(akcodec_rates),
	.support_tdm_rates = akcodec_tdm_rates,
	.num_tdm_rates = ARRAY_SIZE(akcodec_tdm_rates),
	.one2one_ratio = false,
};

static const struct imx_akcodec_plat_data imx_akcodec = {
	.support_rates = akcodec_rates,
	.num_rates = ARRAY_SIZE(akcodec_rates),
	.support_tdm_rates = akcodec_tdm_rates,
	.num_tdm_rates = ARRAY_SIZE(akcodec_tdm_rates),
	.one2one_ratio = true,
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

static bool format_is_tdm(unsigned int dai_fmt)
{
	if ((dai_fmt & SND_SOC_DAIFMT_FORMAT_MASK) == SND_SOC_DAIFMT_DSP_B)
		return true;
	else
		return false;
}

static unsigned long akcodec_get_mclk_rate(struct snd_pcm_substream *substream,
					   struct snd_pcm_hw_params *params,
					   bool tdm_mode)
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
	bool is_dsd = format_is_dsd(params);
	struct device *dev = card->dev;
	struct snd_soc_dai *codec_dai;
	unsigned long mclk_freq;
	unsigned int fmt;
	bool tdm_mode;
	int ret, i;

	tdm_mode = format_is_tdm(rtd->dai_link->dai_fmt);

	if (tdm_mode) {
		data->slots = 8;
		data->slot_width = 32;
		fmt = rtd->dai_link->dai_fmt;
	} else {
		if (is_dsd) {
			data->slots = 1;
			data->slot_width = params_width(params);
			fmt = (rtd->dai_link->dai_fmt & ~SND_SOC_DAIFMT_FORMAT_MASK) |
			      SND_SOC_DAIFMT_PDM;
		} else {
			data->slots = 2;
			data->slot_width = params_physical_width(params);
			fmt = (rtd->dai_link->dai_fmt & ~SND_SOC_DAIFMT_FORMAT_MASK) |
			      SND_SOC_DAIFMT_I2S;
		}
	}

	ret = snd_soc_dai_set_fmt(cpu_dai, fmt);
	if (ret && ret != -ENOTSUPP) {
		dev_err(dev, "failed to set cpu dai fmt: %d\n", ret);
		return ret;
	}
	ret = snd_soc_dai_set_tdm_slot(cpu_dai,
				       BIT(data->slots) - 1,
				       BIT(data->slots) - 1,
				       data->slots, data->slot_width);
	if (ret && ret != -ENOTSUPP) {
		dev_err(dev, "failed to set cpu dai tdm slot: %d\n", ret);
		return ret;
	}

	for_each_rtd_codec_dais(rtd, i, codec_dai) {
		ret = snd_soc_dai_set_fmt(codec_dai, fmt);
		if (ret && ret != -ENOTSUPP) {
			dev_err(dev, "failed to set codec dai[%d] fmt: %d\n", i, ret);
			return ret;
		}

		ret = snd_soc_dai_set_tdm_slot(codec_dai,
					       BIT(data->slots) - 1,
					       BIT(data->slots) - 1,
					       data->slots, data->slot_width);
		if (ret && ret != -ENOTSUPP) {
			dev_err(dev, "failed to set codec dai[%d] tdm slot: %d\n", i, ret);
			return ret;
		}
	}

	/* Set MCLK freq */
	mclk_freq = akcodec_get_mclk_rate(substream, params, tdm_mode);
	/* Use the maximum freq from DSD512 (512*44100 = 22579200) */
	if (is_dsd)
		mclk_freq = 22579200;

	ret = snd_soc_dai_set_sysclk(cpu_dai, FSL_SAI_CLK_MAST1, mclk_freq,
				     SND_SOC_CLOCK_OUT);
	if (ret && ret != -ENOTSUPP)
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

	data->tdm_mode = format_is_tdm(rtd->dai_link->dai_fmt);
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

static struct snd_soc_ops imx_aif_ops = {
	.hw_params = imx_aif_hw_params,
	.startup = imx_aif_startup,
};

static struct snd_soc_ops imx_aif_ops_be = {
	.hw_params = imx_aif_hw_params,
};

static int be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
			      struct snd_pcm_hw_params *params)
{
	struct snd_soc_card *card = rtd->card;
	struct imx_akcodec_data *data = snd_soc_card_get_drvdata(card);
	struct snd_interval *rate;
	struct snd_mask *mask;

	rate = hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	rate->max = data->asrc_rate;
	rate->min = data->asrc_rate;

	mask = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);
	snd_mask_none(mask);
	snd_mask_set(mask, data->asrc_format);

	return 0;
}

static int imx_akcodec_parse_of(struct snd_soc_card *card,
				u32 *asrc_format,
				u32 *asrc_rate,
				enum codec_type *codec_type,
				unsigned int *num_codecs)
{
	struct device_node *np;
	struct device_node *codec = NULL;
	struct device_node *platform = NULL;
	struct device_node *cpu = NULL;
	struct device *dev = card->dev;
	struct snd_soc_dai_link *link;
	struct of_phandle_args args;
	struct snd_soc_dai_link_component *dlc;
	int ret, num_links;
	u32 width;

	ret = snd_soc_of_parse_card_name(card, "model");
	if (ret) {
		dev_err(dev, "Error parsing card name: %d\n", ret);
		return ret;
	}

	/* DAPM routes */
	if (of_property_read_bool(dev->of_node, "audio-routing")) {
		ret = snd_soc_of_parse_audio_routing(card, "audio-routing");
		if (ret)
			return ret;
	}

	/* Populate links */
	num_links = of_get_child_count(dev->of_node);

	/* Allocate the DAI link array */
	card->dai_link = devm_kcalloc(dev, num_links, sizeof(*link), GFP_KERNEL);
	if (!card->dai_link)
		return -ENOMEM;

	card->num_links = num_links;
	link = card->dai_link;

	for_each_child_of_node(dev->of_node, np) {
		dlc = devm_kzalloc(dev, 2 * sizeof(*dlc), GFP_KERNEL);
		if (!dlc) {
			ret = -ENOMEM;
			goto err_put_np;
		}

		link->cpus	= &dlc[0];
		link->platforms	= &dlc[1];

		link->num_cpus		= 1;
		link->num_platforms	= 1;

		ret = of_property_read_string(np, "link-name", &link->name);
		if (ret) {
			dev_err(card->dev, "error getting codec dai_link name\n");
			goto err_put_np;
		}

		cpu = of_get_child_by_name(np, "cpu");
		if (!cpu) {
			dev_err(dev, "%s: Can't find cpu DT node\n", link->name);
			ret = -EINVAL;
			goto err;
		}

		ret = of_parse_phandle_with_args(cpu, "sound-dai",
						 "#sound-dai-cells", 0, &args);
		if (ret) {
			dev_err(card->dev, "%s: error getting cpu phandle\n", link->name);
			goto err;
		}
		link->cpus->of_node = args.np;
		link->platforms->of_node = link->cpus->of_node;
		link->id = args.args[0];

		ret = snd_soc_of_get_dai_name(cpu, &link->cpus->dai_name);
		if (ret) {
			if (ret != -EPROBE_DEFER)
				dev_err(card->dev, "%s: error getting cpu dai name: %d\n",
					link->name, ret);
			goto err;
		}

		codec = of_get_child_by_name(np, "codec");
		if (codec) {
			ret = snd_soc_of_get_dai_link_codecs(dev, codec, link);
			if (ret < 0) {
				if (ret != -EPROBE_DEFER)
					dev_err(dev, "%s: codec dai not found: %d\n",
						link->name, ret);
				goto err;
			}

			*num_codecs = link->num_codecs;

			if (!strcmp(link->codecs->dai_name, "ak4458-aif"))
				*codec_type = CODEC_AK4458;
			else if (!strcmp(link->codecs->dai_name, "ak4497-aif"))
				*codec_type = CODEC_AK4497;
			else if (!strcmp(link->codecs->dai_name, "ak5558-aif"))
				*codec_type = CODEC_AK5558;
			else if (!strcmp(link->codecs->dai_name, "ak5552-aif"))
				*codec_type = CODEC_AK5552;

		} else {
			dlc = devm_kzalloc(dev, sizeof(*dlc), GFP_KERNEL);
			if (!dlc) {
				ret = -ENOMEM;
				goto err;
			}

			link->codecs	 = dlc;
			link->num_codecs = 1;

			link->codecs->dai_name = "snd-soc-dummy-dai";
			link->codecs->name = "snd-soc-dummy";
		}

		if (!strncmp(link->name, "HiFi-ASRC-FE", 12)) {
			/* DPCM frontend */
			link->dynamic = 1;
			link->dpcm_merged_chan = 1;

			ret = of_property_read_u32(args.np, "fsl,asrc-rate", asrc_rate);
			if (ret) {
				dev_err(dev, "failed to get output rate\n");
				ret = -EINVAL;
				goto err;
			}

			ret = of_property_read_u32(args.np, "fsl,asrc-format", asrc_format);
			if (ret) {
				/* Fallback to old binding; translate to asrc_format */
				ret = of_property_read_u32(args.np, "fsl,asrc-width", &width);
				if (ret) {
					dev_err(dev,
						"failed to decide output format\n");
					goto err;
				}

				if (width == 24)
					*asrc_format = SNDRV_PCM_FORMAT_S24_LE;
				else
					*asrc_format = SNDRV_PCM_FORMAT_S16_LE;
			}
		} else if (!strncmp(link->name, "HiFi-ASRC-BE", 12)) {
			/* DPCM backend */
			link->no_pcm = 1;
			link->platforms->of_node = NULL;
			link->platforms->name = "snd-soc-dummy";

			link->be_hw_params_fixup = be_hw_params_fixup;
			link->ops = &imx_aif_ops_be;
		} else {
			link->ops = &imx_aif_ops;
		}

		if (link->no_pcm || link->dynamic)
			snd_soc_dai_link_set_capabilities(link);

		ret = asoc_simple_parse_daifmt(dev, np, codec,
					       NULL, &link->dai_fmt);
		if (ret)
			link->dai_fmt = SND_SOC_DAIFMT_NB_NF |
					SND_SOC_DAIFMT_CBS_CFS |
					SND_SOC_DAIFMT_I2S;

		link->ignore_pmdown_time = 1;
		link->stream_name = link->name;
		link++;

		of_node_put(cpu);
		of_node_put(codec);
		of_node_put(platform);
	}

	return 0;
err:
	of_node_put(cpu);
	of_node_put(codec);
	of_node_put(platform);
err_put_np:
	of_node_put(np);
	return ret;
}

static int imx_akcodec_probe(struct platform_device *pdev)
{
	struct snd_soc_dai_link *link_be = NULL, *link;
	struct imx_akcodec_plat_data *plat_data;
	struct imx_akcodec_data *data;
	int ret, i;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	plat_data = devm_kzalloc(&pdev->dev, sizeof(*plat_data), GFP_KERNEL);
	if (!plat_data)
		return -ENOMEM;

	data->plat_data = plat_data;
	data->card.dev = &pdev->dev;

	memcpy(plat_data, of_device_get_match_data(&pdev->dev),
	       sizeof(struct imx_akcodec_plat_data));

	dev_set_drvdata(&pdev->dev, &data->card);
	snd_soc_card_set_drvdata(&data->card, data);
	ret = imx_akcodec_parse_of(&data->card,
				   &data->asrc_format,
				   &data->asrc_rate,
				   &plat_data->type,
				   &plat_data->num_codecs);
	if (ret)
		return ret;

	data->num_dapm_routes = plat_data->num_codecs + 1;
	data->dapm_routes = devm_kcalloc(&pdev->dev, data->num_dapm_routes,
					 sizeof(struct snd_soc_dapm_route),
					 GFP_KERNEL);
	if (!data->dapm_routes)
		return -ENOMEM;

	/* configure the dapm routes */
	switch (plat_data->type) {
	case CODEC_AK4458:
	case CODEC_AK4497:
		if (plat_data->num_codecs == 1) {
			data->dapm_routes[0].sink = "Playback";
			data->dapm_routes[0].source = "CPU-Playback";
			i = 1;
		} else {
			for (i = 0; i < plat_data->num_codecs; i++) {
				data->dapm_routes[i].sink =
					devm_kasprintf(&pdev->dev, GFP_KERNEL, "%d %s",
						       i + 1, "Playback");
				data->dapm_routes[i].source = "CPU-Playback";
			}
		}
		data->dapm_routes[i].sink = "CPU-Playback";
		data->dapm_routes[i].source = "ASRC-Playback";
		break;
	case CODEC_AK5558:
	case CODEC_AK5552:
		if (plat_data->num_codecs == 1) {
			data->dapm_routes[0].sink = "CPU-Capture";
			data->dapm_routes[0].source = "Capture";
			i = 1;
		} else {
			for (i = 0; i < plat_data->num_codecs; i++) {
				data->dapm_routes[i].source =
					devm_kasprintf(&pdev->dev, GFP_KERNEL, "%d %s",
						       i + 1, "Capture");
				data->dapm_routes[i].sink = "CPU-Capture";
			}
		}
		data->dapm_routes[i].sink = "ASRC-Capture";
		data->dapm_routes[i].source = "CPU-Capture";
		break;
	default:
		break;
	}

	switch (plat_data->type) {
	case CODEC_AK4458:
		plat_data->fs_mul = ak4458_fs_mul;
		plat_data->num_fs_mul = ARRAY_SIZE(ak4458_fs_mul);
		plat_data->tdm_fs_mul = ak4458_tdm_fs_mul;
		plat_data->num_tdm_fs_mul = ARRAY_SIZE(ak4458_tdm_fs_mul);
		plat_data->support_channels = ak4458_channels;
		plat_data->num_channels = ARRAY_SIZE(ak4458_channels);
		plat_data->support_tdm_channels = ak4458_tdm_channels;
		plat_data->num_tdm_channels = ARRAY_SIZE(ak4458_tdm_channels);
		break;
	case CODEC_AK4497:
		plat_data->fs_mul = ak4497_fs_mul;
		plat_data->num_fs_mul = ARRAY_SIZE(ak4497_fs_mul);
		plat_data->support_channels = ak4458_channels;
		plat_data->num_channels = ARRAY_SIZE(ak4458_channels);
		break;
	case CODEC_AK5558:
	case CODEC_AK5552:
		plat_data->fs_mul = ak5558_fs_mul;
		plat_data->num_fs_mul = ARRAY_SIZE(ak5558_fs_mul);
		plat_data->tdm_fs_mul = ak5558_tdm_fs_mul;
		plat_data->num_tdm_fs_mul = ARRAY_SIZE(ak5558_tdm_fs_mul);
		plat_data->support_channels = ak5558_channels;
		plat_data->num_channels = ARRAY_SIZE(ak5558_channels);
		plat_data->support_tdm_channels = ak5558_tdm_channels;
		plat_data->num_tdm_channels = ARRAY_SIZE(ak5558_tdm_channels);
		break;
	default:
		break;
	}

	/* with asrc as front end */
	if (data->card.num_links == 3) {
		data->card.dapm_routes = data->dapm_routes;
		data->card.num_dapm_routes = data->num_dapm_routes;
		for_each_card_prelinks(&data->card, i, link) {
			if (link->no_pcm == 1)
				link_be = link;
		}
		for_each_card_prelinks(&data->card, i, link) {
			if (link->dynamic == 1 && link_be) {
				link->dpcm_playback = link_be->dpcm_playback;
				link->dpcm_capture = link_be->dpcm_capture;
			}
		}
	}

	ret = devm_snd_soc_register_card(&pdev->dev, &data->card);
	if (ret) {
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n", ret);
		return ret;
	}

	return 0;
}

static const struct of_device_id imx_akcodec_dt_ids[] = {
	{ .compatible = "fsl,imx8mq-audio-akcodec", .data = &imx8mq_akcodec, },
	{ .compatible = "fsl,imx-audio-akcodec", .data = &imx_akcodec, },
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
