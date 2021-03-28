// SPDX-License-Identifier: GPL-2.0
/*
 * Motorola Mapphone MDM6600 voice call audio support
 * Copyright 2018 - 2020 Tony Lindgren <tony@atomide.com>
 * Copyright 2020 - 2021 Pavel Machek <pavel@ucw.cz>
 *
 * This one handles audio configuration on "gsmtty2"; it needs to know
 * whether we are in call or not, and that part is in motmdm-state.c
 * and listens on "gsmtty1".
 */

#include <linux/init.h>
#include <linux/kfifo.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/serdev.h>

#include <sound/soc.h>
#include <sound/tlv.h>

#include "motmdm-state.h"

#define MOTMDM_HEADER_LEN	5			/* U1234 */

#define MOTMDM_AUDIO_RESP_LEN	6			/* U1234+XXXX= */
#define MOTMDM_AUDIO_MAX_LEN	128

#define MOTMDM_VOICE_RESP_LEN	7			/* U1234~+CIEV= */

struct motmdm_driver_data {
	struct notifier_block notifier;
	struct snd_soc_component *component;
	struct snd_soc_dai *master_dai;
	struct device *modem;
	struct serdev_device *serdev;
	struct regmap *regmap;
	unsigned char *buf;
	size_t len;
	unsigned int parsed:1;
	unsigned int enabled:1;
	spinlock_t lock;	/* enable/disabled lock */
	struct mutex mutex;	/* for sending commands */
	wait_queue_head_t read_queue;

	unsigned int dtmf_val;
	unsigned int dtmf_en;
};

enum motmdm_cmd {
	CMD_AT_EACC,
	CMD_AT_CLVL,
	CMD_AT_NREC,
};

const char * const motmd_read_fmt[] = {
	[CMD_AT_EACC] = "AT+EACC?",
	[CMD_AT_CLVL] = "AT+CLVL?",
	[CMD_AT_NREC] = "AT+NREC?",
};

const char * const motmd_write_fmt[] = {
	[CMD_AT_EACC] = "AT+EACC=%u,0",
	[CMD_AT_CLVL] = "AT+CLVL=%u",
	[CMD_AT_NREC] = "AT+NREC=%u",
};

/*
 * Currently unconfigured additional inactive (error producing) options
 * seem to be:
 * "TTY Headset", "HCQ Headset", "VCQ Headset", "No-Mic Headset",
 * "Handset Fluence Med", "Handset Fluence Low", "Car Dock", "Lapdock"
 */
static const char * const motmdm_out_mux_texts[] = {
	"Handset", "Headset", "Speakerphone", "Bluetooth",
};

static SOC_ENUM_SINGLE_EXT_DECL(motmdm_out_enum, motmdm_out_mux_texts);

static const DECLARE_TLV_DB_SCALE(motmdm_gain_tlv, 0, 100, 0);

int motmdm_send_command(struct motmdm_driver_data *ddata,
			     const u8 *buf, int len)
{
	struct device *dev = ddata->component->dev;
	const int timeout_ms = 1000;
	unsigned char cmd[MOTMDM_AUDIO_MAX_LEN];
	int ret, cmdlen;

	cmdlen = len + 5 + 1;
	if (cmdlen > MOTMDM_AUDIO_MAX_LEN)
		return -EINVAL;

	mutex_lock(&ddata->mutex);
	memset(ddata->buf, 0, ddata->len);
	ddata->parsed = false;
	snprintf(cmd, cmdlen, "U%04li%s", jiffies % 10000, buf);

	ret = serdev_device_write(ddata->serdev, cmd, cmdlen, MAX_SCHEDULE_TIMEOUT);
	if (ret < 0)
		goto out_unlock;

	serdev_device_wait_until_sent(ddata->serdev, 0);

	ret = wait_event_timeout(ddata->read_queue, ddata->parsed,
				 msecs_to_jiffies(timeout_ms));
	if (ret == 0) {
		ret = -ETIMEDOUT;
		goto out_unlock;
	} else if (ret < 0) {
		goto out_unlock;
	}

	if (strstr(ddata->buf, "ERROR")) {
		dev_err(dev, "command %s error %s\n", cmd, ddata->buf);
		ret = -EPIPE;
	}

	ret = len;

out_unlock:
	mutex_unlock(&ddata->mutex);
	printk("send_command -- ret %d\n", ret);

	return ret;
}

static int motmdm_read_reg(void *context, unsigned int reg,
			   unsigned int *value)
{
	struct snd_soc_component *component = context;
	struct motmdm_driver_data *ddata = snd_soc_component_get_drvdata(component);
	const unsigned char *cmd;
	unsigned int val;
	int error;

	cmd = motmd_read_fmt[reg];
	error = motmdm_send_command(ddata, cmd, strlen(cmd));
	if (error < 0) {
		dev_err(component->dev, "%s: %s failed with %i\n",
			__func__, cmd, error);

		return error;
	}

	error = kstrtouint(ddata->buf + MOTMDM_AUDIO_RESP_LEN, 0, &val);
	if (error)
		return -ENODEV;

	*value = val;

	return error;
}

static int motmdm_write_reg(void *context, unsigned int reg,
			    unsigned int value)
{
	struct snd_soc_component *component = context;
	struct motmdm_driver_data *ddata = snd_soc_component_get_drvdata(component);
	const unsigned char *fmt, *cmd;
	int error;

	fmt = motmd_write_fmt[reg];
	cmd = kasprintf(GFP_KERNEL, fmt, value);
	if (!cmd) {
		error = -ENOMEM;
		goto free;
	}

	error = motmdm_send_command(ddata, cmd, strlen(cmd));
	if (error < 0)
		dev_err(component->dev, "%s: %s failed with %i\n",
			__func__, cmd, error);

free:
	kfree(cmd);

	return error;
}

static const struct reg_default motmdm_reg_defaults[] = {
	{ CMD_AT_EACC, 0x0 },
	{ CMD_AT_CLVL, 0x0 },
};

static const struct regmap_config motmdm_regmap = {
	.reg_bits = 32,
	.reg_stride = 1,
	.val_bits = 32,
	.max_register = CMD_AT_NREC,
	.reg_defaults = motmdm_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(motmdm_reg_defaults),
	.cache_type = REGCACHE_RBTREE,
	.reg_read = motmdm_read_reg,
	.reg_write = motmdm_write_reg,
};

static int motmdm_value_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol,
			    enum motmdm_cmd reg,
			    int cmd_base)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct motmdm_driver_data *ddata = snd_soc_component_get_drvdata(component);
	unsigned int val;
	int error;

	error = regmap_read(ddata->regmap, reg, &val);
	if (error)
		return error;

	if (val >= cmd_base)
		val -= cmd_base;

	ucontrol->value.enumerated.item[0] = val;

	return 0;
}

static int motmdm_value_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol,
			    enum motmdm_cmd reg,
			    int cmd_base)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct motmdm_driver_data *ddata = snd_soc_component_get_drvdata(component);
	int error;

	error = regmap_write(ddata->regmap, reg,
			     ucontrol->value.enumerated.item[0] + cmd_base);
	if (error)
		return error;

	regcache_mark_dirty(ddata->regmap);

	return error;
}

static int motmdm_audio_out_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	return motmdm_value_get(kcontrol, ucontrol, CMD_AT_EACC, 1);
}

static int motmdm_audio_out_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	return motmdm_value_put(kcontrol, ucontrol, CMD_AT_EACC, 1);
}

static int motmdm_gain_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	return motmdm_value_get(kcontrol, ucontrol, CMD_AT_CLVL, 0);
}

static int motmdm_gain_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	return motmdm_value_put(kcontrol, ucontrol, CMD_AT_CLVL, 0);
}

static int motmdm_noise_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	return motmdm_value_get(kcontrol, ucontrol, CMD_AT_NREC, 0);
}

static int motmdm_noise_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	return motmdm_value_put(kcontrol, ucontrol, CMD_AT_NREC, 0);
}

static const char * const motmdm_tonegen_dtmf_key_txt[] = {
	"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "A", "B", "C", "D",
	"*", "#"
};

static SOC_ENUM_SINGLE_EXT_DECL(motmd_tonegen_dtmf_enum,
				motmdm_tonegen_dtmf_key_txt);

static int motmdm_dtmf_get(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct motmdm_driver_data *ddata =
		snd_soc_component_get_drvdata(component);

	ucontrol->value.enumerated.item[0] = ddata->dtmf_val;

	return 0;
}

static int motmdm_dtmf_put(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct motmdm_driver_data *ddata =
		snd_soc_component_get_drvdata(component);

	ddata->dtmf_val = ucontrol->value.enumerated.item[0];

	return 0;
}

static int motmdm_tonegen_dtmf_send_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct motmdm_driver_data *ddata =
		snd_soc_component_get_drvdata(component);

	ucontrol->value.enumerated.item[0] = ddata->dtmf_en;

	return 0;
}

static int motmdm_tonegen_dtmf_send_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct motmdm_driver_data *ddata =
		snd_soc_component_get_drvdata(component);
	const unsigned char *cmd, *fmt = "AT+DTSE=%s,%i";
	const char *tone = "";
	int error;

	if (!ddata->enabled)
		return 0;

	ddata->dtmf_en = ucontrol->value.enumerated.item[0];
	if (ddata->dtmf_en)
		tone = motmdm_tonegen_dtmf_key_txt[ddata->dtmf_val];

	/* Value 0 enables tone generator, 1 disables it */
	cmd = kasprintf(GFP_KERNEL, fmt, tone, !ddata->dtmf_en);

	error = motmdm_send_command(ddata, cmd, strlen(cmd));
	if (error < 0) {
		dev_err(component->dev, "%s: %s failed with %i\n",
			__func__, cmd, error);
		goto free;
	}

free:
	kfree(cmd);

	return error;
}

static int
motmdm_enable_primary_dai(struct snd_soc_component *component)
{
	struct motmdm_driver_data *ddata =
		snd_soc_component_get_drvdata(component);
	int error;

	if (!ddata->master_dai)
		return -ENODEV;

	error = snd_soc_dai_set_sysclk(ddata->master_dai, 1, 19200000,
				       SND_SOC_CLOCK_OUT);
	if (error)
		return error;

	error = snd_soc_dai_set_fmt(ddata->master_dai,
				    SND_SOC_DAIFMT_I2S |
				    SND_SOC_DAIFMT_NB_NF |
				    SND_SOC_DAIFMT_CBM_CFM);
	if (error)
		return error;

	error = snd_soc_dai_set_tdm_slot(ddata->master_dai, 0, 1, 1, 8);
	if (error)
		return error;

	return error;
}

static int
motmdm_disable_primary_dai(struct snd_soc_component *component)
{
	struct motmdm_driver_data *ddata =
		snd_soc_component_get_drvdata(component);
	int error;

	if (!ddata->master_dai) {
		return -ENODEV;
	}

	error = snd_soc_dai_set_sysclk(ddata->master_dai, 0, 26000000,
				       SND_SOC_CLOCK_OUT);
	if (error) {
		return error;
	}

	error = snd_soc_dai_set_fmt(ddata->master_dai,
				    SND_SOC_DAIFMT_CBM_CFM);
	if (error) {
		return error;
	}

	error = snd_soc_dai_set_tdm_slot(ddata->master_dai, 0, 0, 0, 48);
	if (error) {
		return error;
	}

	return error;
}

static int motmdm_find_primary_dai(struct snd_soc_component *component,
	const char *name)
{
	struct motmdm_driver_data *ddata =
		snd_soc_component_get_drvdata(component);
	struct device_node *bitclkmaster = NULL, *framemaster = NULL;
	struct device_node *ep, *master_ep, *master = NULL;
	struct snd_soc_dai_link_component dlc = { 0 };
	unsigned int daifmt;

	ep = of_graph_get_next_endpoint(component->dev->of_node, NULL);
	if (!ep)
		return -ENODEV;

	master_ep = of_graph_get_remote_endpoint(ep);
	of_node_put(ep);
	if (!master_ep)
		return -ENODEV;

	daifmt = snd_soc_of_parse_daifmt(master_ep, NULL,
					 &bitclkmaster, &framemaster);
	of_node_put(master_ep);
	if (bitclkmaster && framemaster)
		master = of_graph_get_port_parent(bitclkmaster);
	of_node_put(bitclkmaster);
	of_node_put(framemaster);
	if (!master)
		return -ENODEV;

	dlc.of_node = master;
	dlc.dai_name = name;
	ddata->master_dai = snd_soc_find_dai(&dlc);
	of_node_put(master);
	if (!ddata->master_dai)
		return -EPROBE_DEFER;

	dev_info(component->dev, "Master DAI is %s\n",
		 dev_name(ddata->master_dai->dev));

	return 0;
}

static int motmdm_parse_tdm(struct snd_soc_component *component)
{
	return motmdm_find_primary_dai(component, "cpcap-voice");
}

static const struct snd_kcontrol_new motmdm_snd_controls[] = {
	SOC_ENUM_EXT("Call Output", motmdm_out_enum,
		     motmdm_audio_out_get,
		     motmdm_audio_out_put),
	SOC_SINGLE_EXT_TLV("Call Volume",
			   0, 0, 7, 0,
			   motmdm_gain_get,
			   motmdm_gain_put,
			   motmdm_gain_tlv),
	SOC_SINGLE_BOOL_EXT("Call Noise Cancellation", 0,
			    motmdm_noise_get,
			    motmdm_noise_put),
	SOC_ENUM_EXT("Call DTMF", motmd_tonegen_dtmf_enum,
		     motmdm_dtmf_get,
		     motmdm_dtmf_put),
	SOC_SINGLE_BOOL_EXT("Call DTMF Send", 0,
			    motmdm_tonegen_dtmf_send_get,
			    motmdm_tonegen_dtmf_send_put),
};

static struct snd_soc_dai_driver motmdm_dai[] = {
	{
		.name = "mdm-call",
		.playback = {
			.stream_name = "Voice Call Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
		.capture = {
			.stream_name = "Voice Call Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
	},
};

static int
motmdm_notifier_call(struct notifier_block *bl, unsigned long state,
							void *unused)
{
	struct motmdm_driver_data *ddata =
		container_of(bl, struct motmdm_driver_data, notifier);
	bool enable, notify = false;
	unsigned long flags;

	enable = !!state;

	spin_lock_irqsave(&ddata->lock, flags);
	if (ddata->enabled != enable) {
		ddata->enabled = enable;
		notify = true;
	}
	spin_unlock_irqrestore(&ddata->lock, flags);

	if (!notify)
		return NOTIFY_DONE;

	if (enable)
		motmdm_enable_primary_dai(ddata->component);
	else
		motmdm_disable_primary_dai(ddata->component);

	return NOTIFY_DONE;
}

static int voice_receive_data(struct serdev_device *serdev,
			     const unsigned char *buf,
			     size_t len)
{
	struct motmdm_driver_data *ddata = serdev_device_get_drvdata(serdev);
	struct device *dev = ddata->component->dev;

	printk("voice_receive_data: have %s %d\n", buf, len);

	if (len > MOTMDM_AUDIO_MAX_LEN)
		len = MOTMDM_AUDIO_MAX_LEN;

	if (len <= MOTMDM_HEADER_LEN)
		return 0;

	printk("voice_receive_data: command reply? -- %s %d\n", buf, len);

	snprintf(ddata->buf, len - MOTMDM_HEADER_LEN, buf + MOTMDM_HEADER_LEN);
	dev_info(dev, "%s: received: %s\n", __func__, ddata->buf);
	ddata->parsed = true;
	wake_up(&ddata->read_queue);

	return len;
}

static const struct serdev_device_ops voice_serdev_ops = {
	.receive_buf    = voice_receive_data,
	.write_wakeup   = serdev_device_write_wakeup,
};

static void motmdm_free_voice_serdev(struct motmdm_driver_data *ddata)
{
	serdev_device_close(ddata->serdev);
}

static int motmdm_soc_probe(struct snd_soc_component *component)
{
	struct motmdm_driver_data *ddata;
	const unsigned char *cmd = "AT+CMUT=0";
	int error;
	u32 line;

	ddata = devm_kzalloc(component->dev, sizeof(*ddata), GFP_KERNEL);
	if (!ddata)
		return -ENOMEM;

	error = of_property_read_u32(component->dev->of_node, "reg", &line);
	if (error)
		return error;

	ddata->serdev = (struct serdev_device *) component->dev;
	ddata->component = component;
	ddata->modem = component->dev->parent;
	mutex_init(&ddata->mutex);
	init_waitqueue_head(&ddata->read_queue);
	ddata->len = PAGE_SIZE;
	spin_lock_init(&ddata->lock);
	snd_soc_component_set_drvdata(component, ddata);
	ddata->len = MOTMDM_AUDIO_MAX_LEN;

	ddata->buf = devm_kzalloc(component->dev, ddata->len, GFP_KERNEL);
	if (!ddata->buf)
		return -ENOMEM;

	ddata->regmap = devm_regmap_init(component->dev, NULL, component,
					 &motmdm_regmap);
	if (IS_ERR(ddata->regmap)) {
		error = PTR_ERR(ddata->regmap);
		dev_err(component->dev, "%s: Failed to allocate regmap: %d\n",
			__func__, error);

		return error;
	}

	serdev_device_set_drvdata(ddata->serdev, ddata);
	serdev_device_set_client_ops(ddata->serdev, &voice_serdev_ops);

	error = serdev_device_open(ddata->serdev);
	if (error)
		return error;

	error = motmdm_parse_tdm(component);
	if (error)
		goto unregister_serdev;

	regcache_sync(ddata->regmap);

	error = motmdm_send_command(ddata, cmd, strlen(cmd));
	if (error < 0)
		goto unregister_serdev;

	error = motmdm_disable_primary_dai(ddata->component);
	if (error)
		goto unregister_serdev;

	ddata->notifier.notifier_call = motmdm_notifier_call;
	register_modem_state_notifier(&ddata->notifier);

	return 0;

unregister_serdev:
	motmdm_free_voice_serdev(ddata);
	serdev_device_close(ddata->serdev);

	return error;
}

static void motmdm_soc_remove(struct snd_soc_component *component)
{
	struct motmdm_driver_data *ddata = snd_soc_component_get_drvdata(component);

	unregister_modem_state_notifier(&ddata->notifier);

	motmdm_free_voice_serdev(ddata);
}

static struct snd_soc_component_driver soc_codec_dev_motmdm = {
	.probe = motmdm_soc_probe,
	.remove = motmdm_soc_remove,
	.controls = motmdm_snd_controls,
	.num_controls = ARRAY_SIZE(motmdm_snd_controls),
	.idle_bias_on = 1,
	.use_pmdown_time = 1,
	.endianness = 1,
	.non_legacy_dai_naming = 1,
};

static int motmdm_codec_probe(struct serdev_device *serdev)
{
	return devm_snd_soc_register_component(&serdev->dev,
					       &soc_codec_dev_motmdm,
					       motmdm_dai,
					       ARRAY_SIZE(motmdm_dai));
}

#ifdef CONFIG_OF
static const struct of_device_id motmdm_of_match[] = {
	{ .compatible = "motorola,mapphone-mdm6600-codec" },
	{},
};
MODULE_DEVICE_TABLE(of, motmdm_of_match);
#endif

static struct serdev_device_driver motmdm_driver = {
	.probe = motmdm_codec_probe,
	.driver = {
		.name = "mot-mdm6600-codec",
		.of_match_table = of_match_ptr(motmdm_of_match),
	},
};
module_serdev_device_driver(motmdm_driver);

MODULE_ALIAS("platform:motmdm-codec");
MODULE_DESCRIPTION("ASoC Motorola Mapphone MDM6600 codec driver");
MODULE_AUTHOR("Tony Lindgren <tony@atomide.com>");
MODULE_LICENSE("GPL v2");
