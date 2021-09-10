// SPDX-License-Identifier: GPL-2.0
//
// rich-custom-card-sample.c
//
// Copyright (C) 2020 Renesas Electronics Corp.
// Copyright (C) 2020 Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
//
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <sound/graph_card.h>

/*
 * Custom driver can have own priv
 * which includes asoc_simple_priv.
 */
struct custom_priv {
	struct asoc_simple_priv simple_priv;

	/* custom driver's own params */
	int custom_params;
};

/* You can get custom_priv from simple_priv */
#define simple_to_custom(simple) container_of((simple), struct custom_priv, simple_priv)

static int custom_card_probe(struct snd_soc_card *card)
{
	struct asoc_simple_priv *simple_priv = snd_soc_card_get_drvdata(card);
	struct custom_priv *custom_priv = simple_to_custom(simple_priv);
	struct device *dev = simple_priv_to_dev(simple_priv);

	dev_info(dev, "custom probe\n");

	custom_priv->custom_params = 1;

	/* you can use generic probe function */
	return asoc_graph_card_probe(card);
}

static int custom_hook_pre(struct asoc_simple_priv *priv)
{
	struct device *dev = simple_priv_to_dev(priv);

	/* You can custom before parsing */
	dev_info(dev, "hook : %s\n", __func__);

	return 0;
}

static int custom_hook_post(struct asoc_simple_priv *priv)
{
	struct device *dev = simple_priv_to_dev(priv);
	struct snd_soc_card *card;

	/* You can custom after parsing */
	dev_info(dev, "hook : %s\n", __func__);

	card = simple_priv_to_card(priv);
	card->probe = custom_card_probe; /* overwrite .probe */

	return 0;
}

static int custom_normal(struct asoc_simple_priv *priv,
			 struct device_node *lnk,
			 struct link_info *li)
{
	struct device *dev = simple_priv_to_dev(priv);

	/* You can custom for DPCM parsing */
	dev_info(dev, "hook : %s\n", __func__);

	return rich_graph_link_dpcm(priv, lnk, li);
}


static int custom_dpcm(struct asoc_simple_priv *priv,
		       struct device_node *lnk,
		       struct link_info *li)
{
	struct device *dev = simple_priv_to_dev(priv);

	/* You can custom for DPCM parsing */
	dev_info(dev, "hook : %s\n", __func__);

	return rich_graph_link_dpcm(priv, lnk, li);
}

static int custom_c2c(struct asoc_simple_priv *priv,
		      struct device_node *lnk,
		      struct link_info *li)
{
	struct device *dev = simple_priv_to_dev(priv);

	/* You can custom for Codec2Codec parsing */
	dev_info(dev, "hook : %s\n", __func__);

	return rich_graph_link_c2c(priv, lnk, li);
}

/*
 * rich-graph-card has many hooks for your customizing.
 */
static struct graph_custom_hooks custom_hooks = {
	.hook_pre	= custom_hook_pre,
	.hook_post	= custom_hook_post,
	.custom_normal	= custom_normal,
	.custom_dpcm	= custom_dpcm,
	.custom_c2c	= custom_c2c,
};

static int custom_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct asoc_simple_priv *priv = snd_soc_card_get_drvdata(rtd->card);
	struct device *dev = simple_priv_to_dev(priv);

	dev_info(dev, "custom startup\n");

	return asoc_simple_startup(substream);
}

/* You can use custom ops */
static const struct snd_soc_ops custom_ops = {
	.startup	= custom_startup,
	.shutdown	= asoc_simple_shutdown,
	.hw_params	= asoc_simple_hw_params,
};

static int custom_probe(struct platform_device *pdev)
{
	struct custom_priv *custom_priv;
	struct asoc_simple_priv *simple_priv;
	struct device *dev = &pdev->dev;
	int ret;

	custom_priv = devm_kzalloc(dev, sizeof(*custom_priv), GFP_KERNEL);
	if (!custom_priv)
		return -ENOMEM;

	simple_priv		= &custom_priv->simple_priv;
	simple_priv->ops	= &custom_ops; /* customize dai_link ops */

	/* use rich-graph-card parsing with own custom hooks */
	ret = rich_graph_parse_of(simple_priv, dev, &custom_hooks);
	if (ret < 0)
		return ret;

	/* customize more if needed */

	return 0;
}

static const struct of_device_id custom_of_match[] = {
	{ .compatible = "rich-custom-card-sample", },
	{},
};
MODULE_DEVICE_TABLE(of, custom_of_match);

static struct platform_driver custom_card = {
	.driver = {
		.name = "rich-custom-card-sample",
		.of_match_table = custom_of_match,
	},
	.probe	= custom_probe,
	.remove	= asoc_simple_remove,
};
module_platform_driver(custom_card);

MODULE_ALIAS("platform:asoc-rich-custom-card-sample");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ASoC Rich Custom Card Sample");
MODULE_AUTHOR("Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>");
