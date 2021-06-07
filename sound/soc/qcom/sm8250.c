// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020, Linaro Limited

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/pcm.h>
#include <linux/soundwire/sdw.h>
#include "qdsp6/q6afe.h"
#include "common.h"

#define DRIVER_NAME		"sm8250"
#define MI2S_BCLK_RATE		1536000

struct sm8250_snd_data {
	bool stream_prepared[AFE_PORT_MAX];
	struct snd_soc_card *card;
	struct sdw_stream_runtime *sruntime[AFE_PORT_MAX];
};

static int qcom_audioreach_snd_parse_of(struct snd_soc_card *card)
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
		platform = of_get_child_by_name(np, "platform");
		codec = of_get_child_by_name(np, "codec");
		if (!cpu) {
			dev_err(dev, "%s: Can't find cpu DT node\n", link->name);
			ret = -EINVAL;
			goto err;
		}

		if (!platform) {
			dev_err(dev, "%s: Can't find platform DT node\n", link->name);
			ret = -EINVAL;
			goto err;
		}

		if (!codec) {
			dev_err(dev, "%s: Can't find codec DT node\n", link->name);
			ret = -EINVAL;
			goto err;
		}

		ret = of_parse_phandle_with_args(cpu, "sound-dai", "#sound-dai-cells", 0, &args);
		if (ret) {
			dev_err(card->dev, "%s: error getting cpu phandle\n", link->name);
			goto err;
		}

		link->cpus->of_node = args.np;
		link->id = args.args[0];

		ret = snd_soc_of_get_dai_name(cpu, &link->cpus->dai_name);
		if (ret) {
			if (ret != -EPROBE_DEFER)
				dev_err(card->dev, "%s: error getting cpu dai name: %d\n",
					link->name, ret);
			goto err;
		}

		link->platforms->of_node = of_parse_phandle(platform, "sound-dai", 0);
		if (!link->platforms->of_node) {
			dev_err(card->dev, "%s: platform dai not found\n", link->name);
			ret = -EINVAL;
			goto err;
		}

		ret = snd_soc_of_get_dai_link_codecs(dev, codec, link);
		if (ret < 0) {
			if (ret != -EPROBE_DEFER)
				dev_err(card->dev, "%s: codec dai not found: %d\n",
					link->name, ret);
			goto err;
		}

		/* DPCM backend */
		link->no_pcm = 1;
		link->ignore_pmdown_time = 1;
		link->ignore_suspend = 1;

		link->stream_name = link->name;
		snd_soc_dai_link_set_capabilities(link);
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

static int sm8250_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
				     struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_CHANNELS);

	rate->min = rate->max = 48000;
	channels->min = channels->max = 2;

	return 0;
}

static int sm8250_snd_startup(struct snd_pcm_substream *substream)
{
	unsigned int fmt = SND_SOC_DAIFMT_CBS_CFS;
	unsigned int codec_dai_fmt = SND_SOC_DAIFMT_CBS_CFS;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);

	switch (cpu_dai->id) {
	case TERTIARY_MI2S_RX:
		codec_dai_fmt |= SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_I2S;
		snd_soc_dai_set_sysclk(cpu_dai,
			Q6AFE_LPASS_CLK_ID_TER_MI2S_IBIT,
			MI2S_BCLK_RATE, SNDRV_PCM_STREAM_PLAYBACK);
		snd_soc_dai_set_fmt(cpu_dai, fmt);
		snd_soc_dai_set_fmt(codec_dai, codec_dai_fmt);
		break;
	default:
		break;
	}
	return 0;
}

static int sm8250_snd_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai;
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct sm8250_snd_data *pdata = snd_soc_card_get_drvdata(rtd->card);
	struct sdw_stream_runtime *sruntime;
	int i;

	switch (cpu_dai->id) {
	case WSA_CODEC_DMA_RX_0:
		for_each_rtd_codec_dais(rtd, i, codec_dai) {
			sruntime = snd_soc_dai_get_sdw_stream(codec_dai,
						      substream->stream);
			if (sruntime != ERR_PTR(-ENOTSUPP))
				pdata->sruntime[cpu_dai->id] = sruntime;
		}
		break;
	}

	return 0;

}

static int sm8250_snd_wsa_dma_prepare(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct sm8250_snd_data *data = snd_soc_card_get_drvdata(rtd->card);
	struct sdw_stream_runtime *sruntime = data->sruntime[cpu_dai->id];
	int ret;

	if (!sruntime)
		return 0;

	if (data->stream_prepared[cpu_dai->id]) {
		sdw_disable_stream(sruntime);
		sdw_deprepare_stream(sruntime);
		data->stream_prepared[cpu_dai->id] = false;
	}

	ret = sdw_prepare_stream(sruntime);
	if (ret)
		return ret;

	/**
	 * NOTE: there is a strict hw requirement about the ordering of port
	 * enables and actual WSA881x PA enable. PA enable should only happen
	 * after soundwire ports are enabled if not DC on the line is
	 * accumulated resulting in Click/Pop Noise
	 * PA enable/mute are handled as part of codec DAPM and digital mute.
	 */

	ret = sdw_enable_stream(sruntime);
	if (ret) {
		sdw_deprepare_stream(sruntime);
		return ret;
	}
	data->stream_prepared[cpu_dai->id]  = true;

	return ret;
}

static int sm8250_snd_prepare(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);

	switch (cpu_dai->id) {
	case WSA_CODEC_DMA_RX_0:
	case WSA_CODEC_DMA_RX_1:
		return sm8250_snd_wsa_dma_prepare(substream);
	default:
		break;
	}

	return 0;
}

static int sm8250_snd_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct sm8250_snd_data *data = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	struct sdw_stream_runtime *sruntime = data->sruntime[cpu_dai->id];

	switch (cpu_dai->id) {
	case WSA_CODEC_DMA_RX_0:
	case WSA_CODEC_DMA_RX_1:
		if (sruntime && data->stream_prepared[cpu_dai->id]) {
			sdw_disable_stream(sruntime);
			sdw_deprepare_stream(sruntime);
			data->stream_prepared[cpu_dai->id] = false;
		}
		break;
	default:
		break;
	}

	return 0;
}

static const struct snd_soc_ops sm8250_be_ops = {
	.startup = sm8250_snd_startup,
	.hw_params = sm8250_snd_hw_params,
	.hw_free = sm8250_snd_hw_free,
	.prepare = sm8250_snd_prepare,
};

static void sm8250_add_be_ops(struct snd_soc_card *card)
{
	struct snd_soc_dai_link *link;
	int i;

	for_each_card_prelinks(card, i, link) {
		if (link->no_pcm == 1) {
			link->be_hw_params_fixup = sm8250_be_hw_params_fixup;
			link->ops = &sm8250_be_ops;
		}
	}
}

static int sm8250_platform_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card;
	struct sm8250_snd_data *data;
	struct device *dev = &pdev->dev;
	int ret;

	card = devm_kzalloc(dev, sizeof(*card), GFP_KERNEL);
	if (!card)
		return -ENOMEM;

	/* Allocate the private data */
	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	card->dev = dev;
	dev_set_drvdata(dev, card);
	snd_soc_card_set_drvdata(card, data);
	if (of_device_is_compatible(dev->of_node, "qcom,sm8250-audioreach-sndcard") ||
		of_device_is_compatible(dev->of_node, "qcom,qrb5165-rb5-audioreach-sndcard"))
		ret = qcom_audioreach_snd_parse_of(card);
	else
		ret = qcom_snd_parse_of(card);

	if (ret)
		return ret;

	card->driver_name = DRIVER_NAME;
	sm8250_add_be_ops(card);
	return devm_snd_soc_register_card(dev, card);
}

static const struct of_device_id snd_sm8250_dt_match[] = {
	{.compatible = "qcom,sm8250-sndcard"},
	{.compatible = "qcom,qrb5165-rb5-sndcard"},
	{.compatible = "qcom,sm8250-audioreach-sndcard" },
	{.compatible = "qcom,qrb5165-rb5-audioreach-sndcard" },
	{}
};

MODULE_DEVICE_TABLE(of, snd_sm8250_dt_match);

static struct platform_driver snd_sm8250_driver = {
	.probe  = sm8250_platform_probe,
	.driver = {
		.name = "snd-sm8250",
		.of_match_table = snd_sm8250_dt_match,
	},
};
module_platform_driver(snd_sm8250_driver);
MODULE_AUTHOR("Srinivas Kandagatla <srinivas.kandagatla@linaro.org");
MODULE_DESCRIPTION("SM8250 ASoC Machine Driver");
MODULE_LICENSE("GPL v2");
