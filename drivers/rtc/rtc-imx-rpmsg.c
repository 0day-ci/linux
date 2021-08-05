// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2017-2021 NXP
 */

#include <linux/firmware/imx/rpmsg.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm_qos.h>
#include <linux/rpmsg.h>
#include <linux/rtc.h>

#define RPMSG_TIMEOUT 1000

#define RTC_RPMSG_SEND		0x0
#define RTC_RPMSG_RECEIVE	0x1
#define RTC_RPMSG_NOTIFY	0x2

enum rtc_rpmsg_cmd {
	RTC_RPMSG_SET_TIME,
	RTC_RPMSG_GET_TIME,
	RTC_RPMSG_SET_ALARM,
	RTC_RPMSG_GET_ALARM,
	RTC_RPMSG_ENABLE_ALARM,
};

struct rtc_rpmsg_data {
	struct imx_rpmsg_head header;
	u8 reserved0;
	union {
		u8 reserved1;
		u8 ret;
	};
	union {
		u32 reserved2;
		u32 sec;
	};
	union {
		u8 enable;
		u8 reserved3;
	};
	union {
		u8 pending;
		u8 reserved4;
	};
} __packed;

struct rtc_rpmsg_info {
	struct rpmsg_device *rpdev;
	struct rtc_rpmsg_data *msg;
	struct pm_qos_request pm_qos_req;
	struct completion cmd_complete;
	struct mutex lock;
	struct rtc_device *rtc;
};

static int rtc_send_message(struct rtc_rpmsg_info *info,
			    struct rtc_rpmsg_data *msg, bool ack)
{
	struct device *dev = &info->rpdev->dev;
	int err;

	mutex_lock(&info->lock);

	cpu_latency_qos_add_request(&info->pm_qos_req, 0);
	reinit_completion(&info->cmd_complete);

	err = rpmsg_send(info->rpdev->ept, (void *)msg, sizeof(*msg));
	if (err) {
		dev_err(dev, "rpmsg send failed: %d\n", err);
		goto err_out;
	}

	if (ack) {
		err = wait_for_completion_timeout(&info->cmd_complete,
						  msecs_to_jiffies(RPMSG_TIMEOUT));
		if (!err) {
			dev_err(dev, "rpmsg send timeout\n");
			err = -ETIMEDOUT;
			goto err_out;
		}

		if (info->msg->ret != 0) {
			dev_err(dev, "rpmsg not ack %d\n", info->msg->ret);
			err = -EINVAL;
			goto err_out;
		}

		err = 0;
	}

err_out:
	cpu_latency_qos_remove_request(&info->pm_qos_req);
	mutex_unlock(&info->lock);

	return err;
}

static int imx_rpmsg_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct rtc_rpmsg_info *rtc_rpmsg = dev_get_drvdata(dev);
	struct rtc_rpmsg_data msg;
	int ret;

	msg.header.cate = IMX_RPMSG_RTC;
	msg.header.major = IMX_RMPSG_MAJOR;
	msg.header.minor = IMX_RMPSG_MINOR;
	msg.header.type = RTC_RPMSG_SEND;
	msg.header.cmd = RTC_RPMSG_GET_TIME;

	ret = rtc_send_message(rtc_rpmsg, &msg, true);
	if (ret)
		return ret;

	rtc_time64_to_tm(rtc_rpmsg->msg->sec, tm);

	return 0;
}

static int imx_rpmsg_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct rtc_rpmsg_info *rtc_rpmsg = dev_get_drvdata(dev);
	struct rtc_rpmsg_data msg;
	unsigned long time;
	int ret;

	time = rtc_tm_to_time64(tm);

	msg.header.cate = IMX_RPMSG_RTC;
	msg.header.major = IMX_RMPSG_MAJOR;
	msg.header.minor = IMX_RMPSG_MINOR;
	msg.header.type = RTC_RPMSG_SEND;
	msg.header.cmd = RTC_RPMSG_SET_TIME;
	msg.sec = time;

	ret = rtc_send_message(rtc_rpmsg, &msg, true);
	if (ret)
		return ret;

	return 0;
}

static int imx_rpmsg_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct rtc_rpmsg_info *rtc_rpmsg = dev_get_drvdata(dev);
	struct rtc_rpmsg_data msg;
	int ret;

	msg.header.cate = IMX_RPMSG_RTC;
	msg.header.major = IMX_RMPSG_MAJOR;
	msg.header.minor = IMX_RMPSG_MINOR;
	msg.header.type = RTC_RPMSG_SEND;
	msg.header.cmd = RTC_RPMSG_GET_ALARM;

	ret = rtc_send_message(rtc_rpmsg, &msg, true);
	if (ret)
		return ret;

	rtc_time64_to_tm(rtc_rpmsg->msg->sec, &alrm->time);
	alrm->pending = rtc_rpmsg->msg->pending;

	return 0;
}

static int imx_rpmsg_rtc_alarm_irq_enable(struct device *dev,
	unsigned int enable)
{
	struct rtc_rpmsg_info *rtc_rpmsg = dev_get_drvdata(dev);
	struct rtc_rpmsg_data msg;
	int ret;

	msg.header.cate = IMX_RPMSG_RTC;
	msg.header.major = IMX_RMPSG_MAJOR;
	msg.header.minor = IMX_RMPSG_MINOR;
	msg.header.type = RTC_RPMSG_SEND;
	msg.header.cmd = RTC_RPMSG_ENABLE_ALARM;
	msg.enable = enable;

	ret = rtc_send_message(rtc_rpmsg, &msg, true);
	if (ret)
		return ret;

	return 0;
}

static int imx_rpmsg_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct rtc_rpmsg_info *rtc_rpmsg = dev_get_drvdata(dev);
	struct rtc_rpmsg_data msg;
	unsigned long time;
	int ret;

	time = rtc_tm_to_time64(&alrm->time);

	msg.header.cate = IMX_RPMSG_RTC;
	msg.header.major = IMX_RMPSG_MAJOR;
	msg.header.minor = IMX_RMPSG_MINOR;
	msg.header.type = RTC_RPMSG_SEND;
	msg.header.cmd = RTC_RPMSG_SET_ALARM;
	msg.sec = time;
	msg.enable = alrm->enabled;

	ret = rtc_send_message(rtc_rpmsg, &msg, true);
	if (ret)
		return ret;

	return 0;
}

static const struct rtc_class_ops imx_rpmsg_rtc_ops = {
	.read_time = imx_rpmsg_rtc_read_time,
	.set_time = imx_rpmsg_rtc_set_time,
	.read_alarm = imx_rpmsg_rtc_read_alarm,
	.set_alarm = imx_rpmsg_rtc_set_alarm,
	.alarm_irq_enable = imx_rpmsg_rtc_alarm_irq_enable,
};

static int rtc_rpmsg_probe(struct rpmsg_device *rpdev)
{
	struct device *dev = &rpdev->dev;
	struct rtc_rpmsg_info *rtc_rpmsg;

	dev_info(dev, "new channel: 0x%x -> 0x%x\n", rpdev->src, rpdev->dst);

	rtc_rpmsg = devm_kzalloc(dev, sizeof(*rtc_rpmsg), GFP_KERNEL);
	if (!rtc_rpmsg)
		return -ENOMEM;

	rtc_rpmsg->rpdev = rpdev;
	mutex_init(&rtc_rpmsg->lock);
	init_completion(&rtc_rpmsg->cmd_complete);

	dev_set_drvdata(dev, rtc_rpmsg);

	device_init_wakeup(dev, true);

	rtc_rpmsg->rtc = devm_rtc_device_register(dev, "rtc-rpmsg",
						  &imx_rpmsg_rtc_ops,
						  THIS_MODULE);
	if (IS_ERR(rtc_rpmsg->rtc)) {
		dev_err(dev, "failed to register rtc rpmsg: %ld\n", PTR_ERR(rtc_rpmsg->rtc));
		return PTR_ERR(rtc_rpmsg->rtc);
	}

	return 0;
}

static void rtc_rpmsg_remove(struct rpmsg_device *rpdev)
{
	dev_info(&rpdev->dev, "rtc rpmsg driver is removed\n");
}

static int rtc_rpmsg_cb(struct rpmsg_device *rpdev, void *data, int len,
			void *priv, u32 src)
{
	struct rtc_rpmsg_info *rtc_rpmsg = dev_get_drvdata(&rpdev->dev);
	struct rtc_rpmsg_data *msg = (struct rtc_rpmsg_data *)data;

	rtc_rpmsg->msg = msg;

	if (msg->header.type == RTC_RPMSG_RECEIVE)
		complete(&rtc_rpmsg->cmd_complete);
	else if (msg->header.type == RTC_RPMSG_NOTIFY)
		rtc_update_irq(rtc_rpmsg->rtc, 1, RTC_IRQF);
	else
		dev_err(&rpdev->dev, "wrong command type!\n");

	return 0;
}

static const struct rpmsg_device_id rtc_rpmsg_id_table[] = {
	{ .name	= "rpmsg-rtc-channel" },
	{ },
};

static struct rpmsg_driver rtc_rpmsg_driver = {
	.drv.name	= "imx_rtc_rpmsg",
	.probe		= rtc_rpmsg_probe,
	.remove		= rtc_rpmsg_remove,
	.callback	= rtc_rpmsg_cb,
	.id_table	= rtc_rpmsg_id_table,
};

/*
 * imx m4 has a limitation that we can't read data during ns process.
 * So register rtc a little bit late as rtc core will read data during
 * register process
 */
static int __init rtc_rpmsg_init(void)
{
	return register_rpmsg_driver(&rtc_rpmsg_driver);
}
late_initcall(rtc_rpmsg_init);

MODULE_AUTHOR("Dong Aisheng <aisheng.dong@nxp.com>");
MODULE_DESCRIPTION("NXP i.MX RPMSG RTC Driver");
MODULE_ALIAS("platform:imx_rtc_rpmsg");
MODULE_LICENSE("GPL");
