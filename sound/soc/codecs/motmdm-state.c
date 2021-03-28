// SPDX-License-Identifier: GPL-2.0
/*
 * Motorola Mapphone MDM6600 voice call audio support
 * Copyright 2018 - 2020 Tony Lindgren <tony@atomide.com>
 * Copyright 2020 - 2021 Pavel Machek <pavel@ucw.cz>
 *
 * Designed to provide notifications about voice call state to the
 * motmdm.c driver. This one listens on "gsmtty1".
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

#define MOTMDM_HEADER_LEN	5			/* U1234 */
#define MOTMDM_AUDIO_MAX_LEN	128
#define MOTMDM_VOICE_RESP_LEN	7			/* U1234~+CIEV= */

struct motmdm_driver_data {
	struct serdev_device *serdev;
	unsigned char *buf;
	size_t len;
	spinlock_t lock;	/* enable/disabled lock */
};

static BLOCKING_NOTIFIER_HEAD(modem_state_chain_head);

int register_modem_state_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&modem_state_chain_head, nb);
}
EXPORT_SYMBOL_GPL(register_modem_state_notifier);

int unregister_modem_state_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&modem_state_chain_head, nb);
}
EXPORT_SYMBOL_GPL(unregister_modem_state_notifier);

static int modem_state_notifier_call_chain(unsigned long val)
{
	int ret;
	ret = __blocking_notifier_call_chain(&modem_state_chain_head, val, NULL,
					     -1, NULL);
	return notifier_to_errno(ret);
}

/* Parses the voice call state from unsolicited notifications on dlci1 */
static int motmdm_voice_get_state(struct motmdm_driver_data *ddata,
				   const unsigned char *buf,
				   size_t len)
{
	struct device *dev = &ddata->serdev->dev;
	bool enable;
	const unsigned char *state;

	if (len < MOTMDM_HEADER_LEN + MOTMDM_VOICE_RESP_LEN + 5)
		return 0;

	/* We only care about the unsolicted messages */
	if (buf[MOTMDM_HEADER_LEN] != '~')
		return 0;

	if (strncmp(buf + MOTMDM_HEADER_LEN + 1, "+CIEV=", 6))
		return len;

	state = buf + MOTMDM_HEADER_LEN + MOTMDM_VOICE_RESP_LEN;
	dev_info(dev, "%s: ciev=%5s\n", __func__, state);

	if (!strncmp(state, "1,1,0", 5) ||	/* connecting */
	    !strncmp(state, "1,4,0", 5) ||	/* incoming call */
	    !strncmp(state, "1,2,0", 5))	/* connected */
		enable = true;
	else if (!strncmp(state, "1,0,0", 5) ||	/* disconnected */
		!strncmp(state, "1,0,2", 5))	/* call failed */
		enable = false;
	else
		return len;

	modem_state_notifier_call_chain(enable);
	return len;
}

static int voice_receive_data(struct serdev_device *serdev,
			     const unsigned char *buf,
			     size_t len)
{
	struct motmdm_driver_data *ddata = serdev_device_get_drvdata(serdev);

	if (len > MOTMDM_AUDIO_MAX_LEN)
		len = MOTMDM_AUDIO_MAX_LEN;

	if (len <= MOTMDM_HEADER_LEN)
		return 0;

	if (buf[MOTMDM_HEADER_LEN] == '~')
		motmdm_voice_get_state(ddata, buf, len);

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

static int motmdm_soc_probe(struct serdev_device *serdev)
{
	struct device *dev = &serdev->dev;
	struct motmdm_driver_data *ddata;
	int error;

	ddata = devm_kzalloc(dev, sizeof(*ddata), GFP_KERNEL);
	if (!ddata)
		return -ENOMEM;

	ddata->serdev = serdev;
	spin_lock_init(&ddata->lock);
	ddata->len = MOTMDM_AUDIO_MAX_LEN;

	ddata->buf = devm_kzalloc(dev, ddata->len, GFP_KERNEL);
	if (!ddata->buf)
		return -ENOMEM;

	serdev_device_set_drvdata(ddata->serdev, ddata);
	serdev_device_set_client_ops(ddata->serdev, &voice_serdev_ops);

	error = serdev_device_open(ddata->serdev);
	return error;
}

static void motmdm_state_remove(struct serdev_device *serdev)
{
	struct motmdm_driver_data *ddata = serdev_device_get_drvdata(serdev);

	motmdm_free_voice_serdev(ddata);
}

static int motmdm_state_probe(struct serdev_device *serdev)
{
	return motmdm_soc_probe(serdev);
}

#ifdef CONFIG_OF
static const struct of_device_id motmdm_of_match[] = {
	{ .compatible = "motorola,mapphone-mdm6600-modem" },
	{},
};
MODULE_DEVICE_TABLE(of, motmdm_of_match);
#endif

static struct serdev_device_driver motmdm_state_driver = {
	.driver	= {
		.name		= "mot-mdm6600-modem",
		.of_match_table	= of_match_ptr(motmdm_of_match),
	},
	.probe	= motmdm_state_probe,
	.remove	= motmdm_state_remove,
};
module_serdev_device_driver(motmdm_state_driver);

MODULE_ALIAS("platform:motmdm-state");
MODULE_DESCRIPTION("Motorola Mapphone MDM6600 modem state driver");
MODULE_AUTHOR("Pavel Machek <pavel@ucw.cz>");
MODULE_LICENSE("GPL v2");
