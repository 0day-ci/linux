// SPDX-License-Identifier: GPL-2.0
//
// ASoC Audio Graph Sound Card2 support
//
// Copyright (C) 2020 Renesas Solutions Corp.
// Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
//
// based on ${LINUX}/sound/soc/generic/audio-graph-card.c
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <sound/graph_card.h>

/************************************
	daifmt
 ************************************
	ports {
		format = "left_j";
		port@0 {
			bitclock-master;
			sample0: endpoint@0 {
				frame-master;
			};
			sample1: endpoint@1 {
				format = "i2s";
			};
		};
		...
	};

 You can set daifmt at ports/port/endpoint.
 It uses *latest* format, and *share* master settings.
 In above case,
	sample0: left_j, bitclock-master, frame-master
	sample1: i2s,    bitclock-master

 NOTE is that card2 is assuming to use .get_fmt.

 If there was no settings, *Codec* will be
 bitclock/frame master as default.
 see
	graph_parse_daifmt().

 ************************************
	Normal Audio-Graph
 ************************************

 CPU <---> Codec

 sound {
	compatible = "audio-graph-card2";
	links = <&cpu>;
 };

 CPU {
	cpu: port {
		bitclock-master;
		frame-master;
		cpu_ep: endpoint { remote-endpoint = <&codec_ep>; }; };
 };

 Codec {
	port {	codec_ep: endpoint { remote-endpoint = <&cpu_ep>; }; };
 };


 ************************************
	DSP Audio-Graph
 ************************************

	   *******
 PCM0 <--> *     * <--> DAI0: Codec Headset
 PCM1 <--> *     * <--> DAI1: Codec Speakers
 PCM2 <--> * DSP * <--> DAI2: MODEM
 PCM3 <--> *     * <--> DAI3: BT
	   *     * <--> DAI4: DMIC
	   *     * <--> DAI5: FM
	   *******

 sound {
	compatible = "audio-graph-card2";

	// indicate routing
	routing = "xxx Playback", "xxx Playback",
		  "xxx Playback", "xxx Playback",
		  "xxx Playback", "xxx Playback";

	// indicate all Front-End, Back-End in DPCM case
	links = <&dsp_fe0, &dsp_fe1, &dsp_fe2, &dsp_fe3,
		 &dsp_be0, &dsp_be1, &dsp_be2, &dsp_be3, &dsp_be4, &dsp_be5>;
 };

 DSP {
	compatible = "audio-graph-card2-dsp";

	// Front-End
	ports@0 {
		dsp_fe0: port@0 { dsp_fe0_ep: endpoint { remote-endpoint = <&pcm0_ep>; }; };
		dsp_fe1: port@1 { dsp_fe1_ep: endpoint { remote-endpoint = <&pcm1_ep>; }; };
		...
	};

	// Back-End
	ports@1 {
		dsp_be0: port@0 { dsp_be0_ep: endpoint { remote-endpoint = <&dai0_ep>; }; };
		dsp_be1: port@1 { dsp_be1_ep: endpoint { remote-endpoint = <&dai1_ep>; }; };
		...
	};
	...
 };

 CPU {
	ports {
		bitclock-master;
		frame-master;
		port@0 { pcm0_ep: endpoint { remote-endpoint = <&dsp_fe0_ep>; }; };
		port@1 { pcm1_ep: endpoint { remote-endpoint = <&dsp_fe1_ep>; }; };
		...
	};
 };

 Codec {
	ports {
		port@0 { dai0_ep: endpoint { remote-endpoint = <&dsp_be0_ep>; }; };
		port@1 { dai1_ep: endpoint { remote-endpoint = <&dsp_be1_ep>; }; };
		...
	};
 };
*/

enum graph_type {
	GRAPH_NORMAL,
	GRAPH_DPCM,
};

#define GRAPH_COMPATIBLE_DPCM	"audio-graph-card2-dsp"

#define port_to_endpoint(port) of_get_child_by_name(port, "endpoint")

static enum graph_type graph_get_type(struct asoc_simple_priv *priv,
				      struct device_node *link)
{
	struct device_node *top;
	const char *string;
	enum graph_type type = GRAPH_NORMAL;
	int ret;

	/* link is port or ports */
	top = of_get_parent(link);
	if (of_node_name_eq(top, "ports")) {
		of_node_put(top);
		top = of_get_parent(top);
	}

	ret = of_property_read_string(top, "compatible", &string);
	if (ret < 0)
		goto end;

	if (strcmp(string, GRAPH_COMPATIBLE_DPCM) == 0)
		type = GRAPH_DPCM;
end:
#ifdef DEBUG
	{
		const char *str = "Normal";
		struct device *dev = simple_priv_to_dev(priv);

		switch (type) {
		case GRAPH_DPCM:
			if (asoc_graph_is_ports0(link))
				str = "DPCM Front-End";
			else
				str = "DPCM Back-End";
			break;
		default:
			break;
		}
		dev_dbg(dev, "%pOF (%s)", link, str);
	}
#endif
	of_node_put(top);

	return type;
}

static const struct snd_soc_ops graph_ops = {
	.startup	= asoc_simple_startup,
	.shutdown	= asoc_simple_shutdown,
	.hw_params	= asoc_simple_hw_params,
};

static int graph_get_dai_id(struct device_node *ep)
{
	struct device_node *node;
	struct device_node *endpoint;
	struct of_endpoint info;
	int i, id;
	const u32 *reg;
	int ret;

	/* use driver specified DAI ID if exist */
	ret = snd_soc_get_dai_id(ep);
	if (ret != -ENOTSUPP)
		return ret;

	/* use endpoint/port reg if exist */
	ret = of_graph_parse_endpoint(ep, &info);
	if (ret == 0) {
		/*
		 * Because it will count port/endpoint if it doesn't have "reg".
		 * But, we can't judge whether it has "no reg", or "reg = <0>"
		 * only of_graph_parse_endpoint().
		 * We need to check "reg" property
		 */
		if (of_get_property(ep,   "reg", NULL))
			return info.id;

		node = of_get_parent(ep);
		reg = of_get_property(node, "reg", NULL);
		of_node_put(node);
		if (reg)
			return info.port;
	}
	node = of_graph_get_port_parent(ep);

	/*
	 * Non HDMI sound case, counting port/endpoint on its DT
	 * is enough. Let's count it.
	 */
	i = 0;
	id = -1;
	for_each_endpoint_of_node(node, endpoint) {
		if (endpoint == ep)
			id = i;
		i++;
	}

	of_node_put(node);

	if (id < 0)
		return -ENODEV;

	return id;
}

static int asoc_simple_parse_dai(struct device_node *ep,
				 struct snd_soc_dai_link_component *dlc,
				 int *is_single_link)
{
	struct device_node *node;
	struct of_phandle_args args;
	int ret;

	if (!ep)
		return 0;

	node = of_graph_get_port_parent(ep);

	/* Get dai->name */
	args.np		= node;
	args.args[0]	= graph_get_dai_id(ep);
	args.args_count	= (of_graph_get_endpoint_count(node) > 1);

	/*
	 * FIXME
	 *
	 * Here, dlc->dai_name is pointer to CPU/Codec DAI name.
	 * If user unbinded CPU or Codec driver, but not for Sound Card,
	 * dlc->dai_name is keeping unbinded CPU or Codec
	 * driver's pointer.
	 *
	 * If user re-bind CPU or Codec driver again, ALSA SoC will try
	 * to rebind Card via snd_soc_try_rebind_card(), but because of
	 * above reason, it might can't bind Sound Card.
	 * Because Sound Card is pointing to released dai_name pointer.
	 *
	 * To avoid this rebind Card issue,
	 * 1) It needs to alloc memory to keep dai_name eventhough
	 *    CPU or Codec driver was unbinded, or
	 * 2) user need to rebind Sound Card everytime
	 *    if he unbinded CPU or Codec.
	 */
	ret = snd_soc_get_dai_name(&args, &dlc->dai_name);
	if (ret < 0)
		return ret;

	dlc->of_node = node;

	if (is_single_link)
		*is_single_link = of_graph_get_endpoint_count(node) == 1;

	return 0;
}

static void graph_parse_convert(struct device_node *ep,
				struct simple_dai_props *props)
{
	struct device_node *port = of_get_parent(ep);
	struct device_node *ports = of_get_parent(port);
	struct asoc_simple_data *adata = &props->adata;

	if (of_node_name_eq(ports, "ports"))
		asoc_simple_parse_convert(ports, NULL, adata);
	asoc_simple_parse_convert(port,  NULL,   adata);
	asoc_simple_parse_convert(ep,    NULL,   adata);

	of_node_put(port);
	of_node_put(ports);
}

static void graph_parse_mclk_fs(struct device_node *ep,
				struct simple_dai_props *props)
{
	struct device_node *port	= of_get_parent(ep);
	struct device_node *ports	= of_get_parent(port);

	if (of_node_name_eq(ports, "ports"))
		of_property_read_u32(ports, "mclk-fs", &props->mclk_fs);
	of_property_read_u32(port,	"mclk-fs", &props->mclk_fs);
	of_property_read_u32(ep,	"mclk-fs", &props->mclk_fs);

	of_node_put(port);
	of_node_put(ports);
}

static int graph_dai_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dai *dai;
	struct device *dev = rtd->dev;
	int i;

	/*
	 * Indicate assumption for a while.
	 * It will be removed.
	 */
	for_each_rtd_dais(rtd, i, dai)
		if (!dai->driver->ops ||
		    !dai->driver->ops->auto_selectable_formats) {
			dev_warn_once(dev, "audio-graph-card2 is assuming "
				"DAI driver (%s) has .auto_selectable_formats\n", dai->name);
			break;
		}

	return asoc_simple_dai_init(rtd);
}

static int graph_parse_node(struct asoc_simple_priv *priv,
			    struct device_node *ep,
			    struct link_info *li,
			    int idx, int *cpu)
{
	struct device *dev = simple_priv_to_dev(priv);
	struct snd_soc_dai_link *dai_link = simple_priv_to_link(priv, li->link);
	struct simple_dai_props *dai_props = simple_priv_to_props(priv, li->link);
	struct snd_soc_dai_link_component *dlc;
	struct asoc_simple_dai *dai;
	int ret;

	if (cpu) {
		dlc = asoc_link_to_cpu(dai_link, idx);
		dai = simple_props_to_dai_cpu(dai_props, idx);
	} else {
		dlc = asoc_link_to_codec(dai_link, idx);
		dai = simple_props_to_dai_codec(dai_props, idx);
	}

	graph_parse_mclk_fs(ep, dai_props);

	ret = asoc_simple_parse_dai(ep, dlc, cpu);
	if (ret < 0)
		return ret;

	ret = asoc_simple_parse_tdm(ep, dai);
	if (ret < 0)
		return ret;

	ret = asoc_simple_parse_clk(dev, ep, dai, dlc);
	if (ret < 0)
		return ret;

	return 0;
}

static void graph_parse_daifmt(struct device_node *node,
			       unsigned int *daifmt, unsigned int *bit_frame)
{
	unsigned int fmt;

	/*
	 * see also above "daifmt" explanation
	 * and samples.
	 */

	/*
	 *	ports {
	 * (A)
	 *		port {
	 * (B)
	 *			endpoint {
	 * (C)
	 *			};
	 *		};
	 *	};
	 * };
	 */

	/*
	 * clock_provider:
	 *
	 * It can be judged it is provider
	 * if (A) or (B) or (C) has bitclock-master / frame-master flag.
	 *
	 * use "or"
	 */
	*bit_frame |= snd_soc_daifmt_parse_clock_provider_as_bitmap(node, NULL);

#define update_daifmt(name)					\
	if (!(*daifmt & SND_SOC_DAIFMT_##name##_MASK) &&	\
		 (fmt & SND_SOC_DAIFMT_##name##_MASK))		\
		*daifmt |= fmt & SND_SOC_DAIFMT_##name##_MASK;

	/*
	 * format
	 *
	 * This function is called by (C) -> (B) -> (A) order.
	 * Set if applicable part was not yet set.
	 */
	fmt = snd_soc_daifmt_parse_format(node, NULL);
	update_daifmt(FORMAT);
	update_daifmt(CLOCK);
	update_daifmt(INV);
}

static int graph_link_init(struct asoc_simple_priv *priv,
			   struct device_node *ep,
			   struct link_info *li,
			   int is_cpu_node,
			   char *name)
{
	struct device *dev = simple_priv_to_dev(priv);
	struct snd_soc_dai_link *dai_link = simple_priv_to_link(priv, li->link);
	struct device_node *port  = of_get_parent(ep);
	struct device_node *ports = of_get_parent(port);
	unsigned int daifmt = 0, daiclk = 0;
	unsigned int bit_frame = 0;

	/*
	 *	ports {
	 * (A)
	 *		port {
	 * (B)
	 *			endpoint {
	 * (C)
	 *			};
	 *		};
	 *	};
	 * };
	 */
	graph_parse_daifmt(ep,    &daifmt, &bit_frame);		/* (C) */
	graph_parse_daifmt(port,  &daifmt, &bit_frame);		/* (B) */
	if (of_node_name_eq(ports, "ports"))
		graph_parse_daifmt(ports, &daifmt, &bit_frame);	/* (A) */

	/*
	 * convert bit_frame
	 * We need to flip clock_provider if it was CPU node,
	 * because it is Codec base.
	 */
	daiclk = snd_soc_daifmt_clock_provider_from_bitmap(bit_frame);
	if (is_cpu_node)
		daiclk = snd_soc_daifmt_clock_provider_fliped(daiclk);

	if (daifmt)
		dev_warn(dev, "don't use format. implemente .set_fmt instead (%pOFf)\n", port);

	dai_link->dai_fmt	= daifmt | daiclk;
	dai_link->init		= graph_dai_init;
	dai_link->ops		= &graph_ops;
	if (priv->ops)
		dai_link->ops	= priv->ops;

	return asoc_simple_set_dailink_name(dev, dai_link, name);
}

int audio_graph2_link_normal(struct asoc_simple_priv *priv,
			     struct device_node *lnk,
			     struct link_info *li)
{
	struct snd_soc_dai_link *dai_link = simple_priv_to_link(priv, li->link);
	struct device_node *cpu_port = lnk;
	struct device_node *cpu_ep = port_to_endpoint(cpu_port);
	struct device_node *codec_ep = of_graph_get_remote_endpoint(cpu_ep);
	struct snd_soc_dai_link_component *cpus = asoc_link_to_cpu(dai_link, 0);
	struct snd_soc_dai_link_component *codecs = asoc_link_to_codec(dai_link, 0);
	char dai_name[64];
	int ret, is_single_links = 0;

	ret = graph_parse_node(priv, cpu_ep, li, 0, &is_single_links);
	if (ret < 0)
		goto err;

	ret = graph_parse_node(priv, codec_ep, li, 0, NULL);
	if (ret < 0)
		goto err;

	snprintf(dai_name, sizeof(dai_name),
		 "%s-%s", cpus->dai_name, codecs->dai_name);

	asoc_simple_canonicalize_cpu(cpus, is_single_links);

	ret = graph_link_init(priv, cpu_ep, li, 1, dai_name);
	if (ret < 0)
		goto err;

err:
	of_node_put(cpu_ep);
	of_node_put(codec_ep);

	return ret;
}
EXPORT_SYMBOL_GPL(audio_graph2_link_normal);

int audio_graph2_link_dpcm(struct asoc_simple_priv *priv,
			   struct device_node *lnk,
			   struct link_info *li)
{
	struct device_node *ep = port_to_endpoint(lnk);
	struct device_node *rep = of_graph_get_remote_endpoint(ep);
	struct snd_soc_dai_link *dai_link = simple_priv_to_link(priv, li->link);
	struct simple_dai_props *dai_props = simple_priv_to_props(priv, li->link);
	char dai_name[64];
	int is_cpu = asoc_graph_is_ports0(lnk);
	int ret;

	if (is_cpu) {
		struct snd_soc_dai_link_component *cpus = asoc_link_to_cpu(dai_link, 0);
		int is_single_links = 0;

		/*
		 * DSP {
		 *	compatible = "audio-graph-card2-dsp";
		 *
		 *	// Front-End
		 *	ports@0 {
		 * =>		lnk: port@0 { ep: endpoint { remote-endpoint = <&rep>; }; };
		 *		 ...
		 *	};
		 *	// Back-End
		 *	ports@0 {
		 *		 ...
		 *	};
		 * };
		 *
		 * CPU {
		 *	rports: ports {
		 *		rport: port@0 { rep: endpoint { ... }; };
		 *	}
		 * }
		 */
		/*
		 * setup CPU here, Codec is already set as dummy.
		 * see
		 *	asoc_simple_init_priv()
		 */
		dai_link->dynamic		= 1;
		dai_link->dpcm_merged_format	= 1;

		ret = graph_parse_node(priv, rep, li, 0, &is_single_links);
		if (ret)
			goto err;

		snprintf(dai_name, sizeof(dai_name),
			 "fe.%pOFP.%s", cpus->of_node, cpus->dai_name);

		asoc_simple_canonicalize_cpu(cpus, is_single_links);
	} else {
		struct snd_soc_dai_link_component *codecs = asoc_link_to_codec(dai_link, 0);
		struct snd_soc_codec_conf *cconf = simple_props_to_codec_conf(dai_props, 0);
		struct device_node *rport;
		struct device_node *rports;

		/*
		 * DSP {
		 *	compatible = "audio-graph-card2-dsp";
		 *
		 *	// Front-End
		 *	ports@0 {
		 *		 ...
		 *	};
		 *	// Back-End
		 *	ports@0 {
		 * =>		lnk: port@0 { ep: endpoint { remote-endpoint = <&rep>; }; };
		 *		 ...
		 *	};
		 * };
		 *
		 * Codec {
		 *	rports: ports {
		 *		rport: port@0 { rep: endpoint { ... }; };
		 *	}
		 * }
		 */
		/*
		 * setup Codec here, CPU is already set as dummy.
		 * see
		 *	asoc_simple_init_priv()
		 */

		/* BE settings */
		dai_link->no_pcm		= 1;
		dai_link->be_hw_params_fixup	= asoc_simple_be_hw_params_fixup;

		ret = graph_parse_node(priv, rep, li, 0, NULL);
		if (ret < 0)
			goto err;

		snprintf(dai_name, sizeof(dai_name),
			 "be.%pOFP.%s", codecs->of_node, codecs->dai_name);

		/* check "prefix" from top node */
		rport  = of_get_parent(rep);
		rports = of_get_parent(rport);

		if (of_node_name_eq(rports, "ports"))
			snd_soc_of_parse_node_prefix(rports, cconf, codecs->of_node, "prefix");
		snd_soc_of_parse_node_prefix(rport,  cconf, codecs->of_node, "prefix");

		of_node_put(rport);
		of_node_put(rports);
	}

	graph_parse_convert(rep, dai_props);

	snd_soc_dai_link_set_capabilities(dai_link);

	ret = graph_link_init(priv, rep, li, is_cpu, dai_name);
err:
	of_node_put(ep);
	of_node_put(rep);

	return ret;
}
EXPORT_SYMBOL_GPL(audio_graph2_link_dpcm);

static int graph_link(struct asoc_simple_priv *priv,
		      struct graph_custom_hooks *hooks,
		      enum graph_type gtype,
		      struct device_node *lnk,
		      struct link_info *li)
{
	struct device *dev = simple_priv_to_dev(priv);
	GRAPH_CUSTOM func = NULL;
	int ret = -EINVAL;

	switch (gtype) {
	case GRAPH_NORMAL:
		if (hooks && hooks->custom_normal)
			func = hooks->custom_normal;
		else
			func = audio_graph2_link_normal;
		break;
	case GRAPH_DPCM:
		if (hooks && hooks->custom_dpcm)
			func = hooks->custom_dpcm;
		else
			func = audio_graph2_link_dpcm;
		break;
	}

	if (!func) {
		dev_err(dev, "non supported gtype (%d)\n", gtype);
		goto err;
	}

	ret = func(priv, lnk, li);
	if (ret < 0)
		goto err;

	li->link++;
err:
	return ret;
}

static int graph_count_normal(struct asoc_simple_priv *priv,
			      struct device_node *lnk,
			      struct link_info *li)
{
	/*
	 *	CPU {
	 * =>		lnk: port { endpoint { .. }; };
	 *	};
	 */
	li->num[li->link].cpus		= 1;
	li->num[li->link].codecs	= 1;

	return 0;
}

static int graph_count_dsp(struct asoc_simple_priv *priv,
			   struct device_node *lnk,
			   struct link_info *li)
{
	/*
	 *	DSP {
	 *		compatible = "audio-graph-card2-dsp";
	 *
	 *		// Front-End
	 *		ports@0 {
	 * =>			lnk: port@0 { endpoint { ... }; };
	 *			 ...
	 *		};
	 *		// Back-End
	 *		ports@1 {
	 * =>			lnk: port@0 { endpoint { ... }; };
	 *			 ...
	 *		};
	 * };
	 */
	if (asoc_graph_is_ports0(lnk)) {
		/* Front-End */
		li->num[li->link].cpus		= 1;
	} else {
		/* Back-End */
		li->num[li->link].codecs	= 1;
	}

	return 0;
}

static int graph_count(struct asoc_simple_priv *priv,
		       struct graph_custom_hooks *hooks,
		       enum graph_type gtype,
		       struct device_node *lnk,
		       struct link_info *li)
{
	struct device *dev = simple_priv_to_dev(priv);
	GRAPH_CUSTOM func = NULL;
	int ret = -EINVAL;

	if (li->link >= SNDRV_MAX_LINKS) {
		dev_err(dev, "too many links\n");
		return ret;
	}

	switch (gtype) {
	case GRAPH_NORMAL:
		func = graph_count_normal;
		break;
	case GRAPH_DPCM:
		func = graph_count_dsp;
		break;
	}

	if (!func) {
		dev_err(dev, "non supported gtype (%d)\n", gtype);
		goto err;
	}

	ret = func(priv, lnk, li);
	if (ret < 0)
		goto err;

	li->link++;
err:
	return ret;
}

static int graph_for_each_link(struct asoc_simple_priv *priv,
			       struct graph_custom_hooks *hooks,
			       struct link_info *li,
			       int (*func)(struct asoc_simple_priv *priv,
					   struct graph_custom_hooks *hooks,
					   enum graph_type gtype,
					   struct device_node *lnk,
					   struct link_info *li))
{
	struct of_phandle_iterator it;
	struct device *dev = simple_priv_to_dev(priv);
	struct device_node *node = dev->of_node;
	struct device_node *lnk;
	enum graph_type gtype;
	int rc, ret;

	/* loop for all listed CPU port */
	of_for_each_phandle(&it, rc, node, "links", NULL, 0) {
		lnk = it.node;

		gtype = graph_get_type(priv, lnk);

		ret = func(priv, hooks, gtype, lnk, li);
		if (ret < 0)
			return ret;
	}

	return 0;
}

int audio_graph2_parse_of(struct asoc_simple_priv *priv, struct device *dev,
			  struct graph_custom_hooks *hooks)
{
	struct snd_soc_card *card = simple_priv_to_card(priv);
	struct link_info *li;
	int ret;

	dev_warn(dev, "Audio Graph Card2 is still under Experimental stage\n");

	li = devm_kzalloc(dev, sizeof(*li), GFP_KERNEL);
	if (!li)
		return -ENOMEM;

	card->probe	= asoc_graph_card_probe;
	card->owner	= THIS_MODULE;
	card->dev	= dev;

	if ((hooks) && (hooks)->hook_pre) {
		ret = (hooks)->hook_pre(priv);
		if (ret < 0)
			goto err;
	}

	ret = graph_for_each_link(priv, hooks, li, graph_count);
	if (!li->link)
		ret = -EINVAL;
	if (ret < 0)
		goto err;

	ret = asoc_simple_init_priv(priv, li);
	if (ret < 0)
		goto err;

	priv->pa_gpio = devm_gpiod_get_optional(dev, "pa", GPIOD_OUT_LOW);
	if (IS_ERR(priv->pa_gpio)) {
		ret = PTR_ERR(priv->pa_gpio);
		dev_err(dev, "failed to get amplifier gpio: %d\n", ret);
		goto err;
	}

	ret = asoc_simple_parse_widgets(card, NULL);
	if (ret < 0)
		goto err;

	ret = asoc_simple_parse_routing(card, NULL);
	if (ret < 0)
		goto err;

	memset(li, 0, sizeof(*li));
	ret = graph_for_each_link(priv, hooks, li, graph_link);
	if (ret < 0)
		goto err;

	ret = asoc_simple_parse_card_name(card, NULL);
	if (ret < 0)
		goto err;

	snd_soc_card_set_drvdata(card, priv);

	if ((hooks) && (hooks)->hook_post) {
		ret = (hooks)->hook_post(priv);
		if (ret < 0)
			goto err;
	}

	asoc_simple_debug_info(priv);

	ret = devm_snd_soc_register_card(dev, card);
err:
	devm_kfree(dev, li);

	if ((ret < 0) && (ret != -EPROBE_DEFER))
		dev_err(dev, "parse error %d\n", ret);

	return ret;
}
EXPORT_SYMBOL_GPL(audio_graph2_parse_of);

static int graph_probe(struct platform_device *pdev)
{
	struct asoc_simple_priv *priv;
	struct device *dev = &pdev->dev;

	/* Allocate the private data and the DAI link array */
	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	return audio_graph2_parse_of(priv, dev, NULL);
}

static const struct of_device_id graph_of_match[] = {
	{ .compatible = "audio-graph-card2", },
	{},
};
MODULE_DEVICE_TABLE(of, graph_of_match);

static struct platform_driver graph_card = {
	.driver = {
		.name = "asoc-audio-graph-card2",
		.pm = &snd_soc_pm_ops,
		.of_match_table = graph_of_match,
	},
	.probe	= graph_probe,
	.remove	= asoc_simple_remove,
};
module_platform_driver(graph_card);

MODULE_ALIAS("platform:asoc-audio-graph-card2");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ASoC Audio Graph Sound Card2");
MODULE_AUTHOR("Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>");
