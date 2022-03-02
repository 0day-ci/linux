// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 Microchip.
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rtc.h>
#include <linux/tee_drv.h>

#define RTC_INFO_VERSION	0x1

#define TA_CMD_RTC_GET_INFO		0x0
#define TA_CMD_RTC_GET_TIME		0x1
#define TA_CMD_RTC_SET_TIME		0x2
#define TA_CMD_RTC_GET_OFFSET		0x3
#define TA_CMD_RTC_SET_OFFSET		0x4

#define TA_RTC_FEATURE_CORRECTION	BIT(0)

struct optee_rtc_time {
	u32 tm_sec;
	u32 tm_min;
	u32 tm_hour;
	u32 tm_mday;
	u32 tm_mon;
	u32 tm_year;
	u32 tm_wday;
};

struct optee_rtc_info {
	u64 version;
	u64 features;
	struct optee_rtc_time range_min;
	struct optee_rtc_time range_max;
};

/**
 * struct optee_rtc - OP-TEE RTC private data
 * @dev:		OP-TEE based RTC device.
 * @ctx:		OP-TEE context handler.
 * @session_id:		RTC TA session identifier.
 * @entropy_shm_pool:	Memory pool shared with RTC device.
 * @features:		Bitfield of RTC features
 */
struct optee_rtc {
	struct device *dev;
	struct tee_context *ctx;
	u32 session_id;
	struct tee_shm *entropy_shm_pool;
	u64 features;
};

static int optee_rtc_readtime(struct device *dev, struct rtc_time *tm)
{
	struct optee_rtc *priv = dev_get_drvdata(dev);
	struct tee_ioctl_invoke_arg inv_arg = {0};
	struct optee_rtc_time *optee_tm;
	struct tee_param param[4] = {0};
	int ret = 0;

	inv_arg.func = TA_CMD_RTC_GET_TIME;
	inv_arg.session = priv->session_id;
	inv_arg.num_params = 4;

	/* Fill invoke cmd params */
	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_OUTPUT;
	param[0].u.memref.shm = priv->entropy_shm_pool;
	param[0].u.memref.size = sizeof(struct optee_rtc_time);
	param[0].u.memref.shm_offs = 0;

	ret = tee_client_invoke_func(priv->ctx, &inv_arg, param);
	if ((ret < 0) || (inv_arg.ret != 0)) {
		dev_err(dev, "TA_CMD_RTC_GET_TIME invoke err: %x\n",
			inv_arg.ret);
		return ret;
	}

	optee_tm = tee_shm_get_va(priv->entropy_shm_pool, 0);
	if (IS_ERR(optee_tm)) {
		dev_err(dev, "tee_shm_get_va failed\n");
		return PTR_ERR(optee_tm);
	}

	if (param[0].u.memref.size != sizeof(*optee_tm)) {
		dev_err(dev, "Invalid read size from OPTEE\n");
		return -EINVAL;
	}

	tm->tm_sec = optee_tm->tm_sec;
	tm->tm_min = optee_tm->tm_min;
	tm->tm_hour = optee_tm->tm_hour;
	tm->tm_mday = optee_tm->tm_mday;
	tm->tm_mon = optee_tm->tm_mon;
	tm->tm_year = optee_tm->tm_year - 1900;
	tm->tm_wday = optee_tm->tm_wday;
	tm->tm_yday = rtc_year_days(tm->tm_mday, tm->tm_mon, tm->tm_year);

	return 0;
}

static int optee_rtc_settime(struct device *dev, struct rtc_time *tm)
{
	struct optee_rtc *priv = dev_get_drvdata(dev);
	struct tee_ioctl_invoke_arg inv_arg = {0};
	struct optee_rtc_time optee_tm;
	struct tee_param param[4] = {0};
	void *rtc_data;
	int ret = 0;

	optee_tm.tm_sec = tm->tm_sec;
	optee_tm.tm_min = tm->tm_min;
	optee_tm.tm_hour = tm->tm_hour;
	optee_tm.tm_mday = tm->tm_mday;
	optee_tm.tm_mon = tm->tm_mon;
	optee_tm.tm_year = tm->tm_year + 1900;
	optee_tm.tm_wday = tm->tm_wday;

	inv_arg.func = TA_CMD_RTC_SET_TIME;
	inv_arg.session = priv->session_id;
	inv_arg.num_params = 4;

	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT;
	param[0].u.memref.shm = priv->entropy_shm_pool;
	param[0].u.memref.size = sizeof(struct optee_rtc_time);
	param[0].u.memref.shm_offs = 0;

	rtc_data = tee_shm_get_va(priv->entropy_shm_pool, 0);
	if (IS_ERR(rtc_data)) {
		dev_err(dev, "tee_shm_get_va failed\n");
		return PTR_ERR(rtc_data);
	}

	memcpy(rtc_data, &optee_tm, sizeof(struct optee_rtc_time));

	ret = tee_client_invoke_func(priv->ctx, &inv_arg, param);
	if ((ret < 0) || (inv_arg.ret != 0)) {
		dev_err(dev, "TA_CMD_RTC_GET_TIME invoke err: %x\n",
			inv_arg.ret);
		return ret;
	}

	return 0;
}

static int optee_rtc_readoffset(struct device *dev, long *offset)
{
	struct optee_rtc *priv = dev_get_drvdata(dev);
	struct tee_ioctl_invoke_arg inv_arg = {0};
	struct tee_param param[4] = {0};
	int ret = 0;

	if (!(priv->features & TA_RTC_FEATURE_CORRECTION))
		return -EOPNOTSUPP;

	inv_arg.func = TA_CMD_RTC_GET_OFFSET;
	inv_arg.session = priv->session_id;
	inv_arg.num_params = 4;

	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_OUTPUT;

	ret = tee_client_invoke_func(priv->ctx, &inv_arg, param);
	if ((ret < 0) || (inv_arg.ret != 0)) {
		dev_err(dev, "TA_CMD_RTC_GET_RNG_INFO invoke err: %x\n",
			inv_arg.ret);
		return -EINVAL;
	}

	*offset = param[0].u.value.a;

	return 0;
}

static int optee_rtc_setoffset(struct device *dev, long offset)
{
	struct optee_rtc *priv = dev_get_drvdata(dev);
	struct tee_ioctl_invoke_arg inv_arg = {0};
	struct tee_param param[4] = {0};
	int ret = 0;

	if (!(priv->features & TA_RTC_FEATURE_CORRECTION))
		return -EOPNOTSUPP;

	inv_arg.func = TA_CMD_RTC_SET_OFFSET;
	inv_arg.session = priv->session_id;
	inv_arg.num_params = 4;

	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT;
	param[0].u.value.a = offset;

	ret = tee_client_invoke_func(priv->ctx, &inv_arg, param);
	if ((ret < 0) || (inv_arg.ret != 0)) {
		dev_err(dev, "TA_CMD_RTC_GET_RNG_INFO invoke err: %x\n",
			inv_arg.ret);
		return -EINVAL;
	}

	return 0;
}
static const struct rtc_class_ops optee_rtc_ops = {
	.read_time	= optee_rtc_readtime,
	.set_time	= optee_rtc_settime,
	.set_offset	= optee_rtc_setoffset,
	.read_offset	= optee_rtc_readoffset,
};

static int optee_rtc_read_info(struct device *dev, struct rtc_device *rtc,
			       u64 *features)
{
	struct optee_rtc *priv = dev_get_drvdata(dev);
	struct tee_ioctl_invoke_arg inv_arg = {0};
	struct tee_param param[4] = {0};
	struct optee_rtc_info *info;
	struct optee_rtc_time *tm;
	int ret = 0;

	inv_arg.func = TA_CMD_RTC_GET_INFO;
	inv_arg.session = priv->session_id;
	inv_arg.num_params = 4;

	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_OUTPUT;
	param[0].u.memref.shm = priv->entropy_shm_pool;
	param[0].u.memref.size = sizeof(*info);
	param[0].u.memref.shm_offs = 0;

	ret = tee_client_invoke_func(priv->ctx, &inv_arg, param);
	if ((ret < 0) || (inv_arg.ret != 0)) {
		dev_err(dev, "TA_CMD_RTC_GET_RNG_INFO invoke err: %x\n",
			inv_arg.ret);
		return -EINVAL;
	}

	info = tee_shm_get_va(priv->entropy_shm_pool, 0);
	if (IS_ERR(info)) {
		dev_err(dev, "tee_shm_get_va failed\n");
		return PTR_ERR(info);
	}

	if (param[0].u.memref.size < sizeof(*info)) {
		dev_err(dev, "Invalid read size from OPTEE\n");
		return -EINVAL;
	}

	if (info->version != RTC_INFO_VERSION) {
		dev_err(dev, "Unsupported information version %llu\n",
			      info->version);
		return -EINVAL;
	}

	*features = info->features;

	tm = &info->range_min;
	rtc->range_min = mktime64(tm->tm_year, tm->tm_mon, tm->tm_mday,
				  tm->tm_hour, tm->tm_min, tm->tm_sec);
	tm = &info->range_max;
	rtc->range_max = mktime64(tm->tm_year, tm->tm_mon, tm->tm_mday,
				  tm->tm_hour, tm->tm_min, tm->tm_sec);

	return 0;
}

static int optee_ctx_match(struct tee_ioctl_version_data *ver, const void *data)
{
	if (ver->impl_id == TEE_IMPL_ID_OPTEE)
		return 1;
	else
		return 0;
}

static int optee_rtc_probe(struct device *dev)
{
	struct tee_client_device *rtc_device = to_tee_client_device(dev);
	struct tee_ioctl_open_session_arg sess_arg;
	struct tee_shm *entropy_shm_pool = NULL;
	int ret = 0, err = -ENODEV;
	struct optee_rtc *priv;
	struct rtc_device *rtc;

	memset(&sess_arg, 0, sizeof(sess_arg));

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	rtc = devm_rtc_allocate_device(dev);
	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	/* Open context with TEE driver */
	priv->ctx = tee_client_open_context(NULL, optee_ctx_match, NULL,
					       NULL);
	if (IS_ERR(priv->ctx))
		return -ENODEV;

	/* Open session with rtc Trusted App */
	memcpy(sess_arg.uuid, rtc_device->id.uuid.b, TEE_IOCTL_UUID_LEN);
	sess_arg.clnt_login = TEE_IOCTL_LOGIN_REE_KERNEL;
	sess_arg.num_params = 0;

	ret = tee_client_open_session(priv->ctx, &sess_arg, NULL);
	if ((ret < 0) || (sess_arg.ret != 0)) {
		dev_err(dev, "tee_client_open_session failed, err: %x\n",
			sess_arg.ret);
		err = -EINVAL;
		goto out_ctx;
	}
	priv->session_id = sess_arg.session;

	entropy_shm_pool = tee_shm_alloc(priv->ctx,
					 sizeof(struct optee_rtc_info),
					 TEE_SHM_MAPPED | TEE_SHM_DMA_BUF);
	if (IS_ERR(entropy_shm_pool)) {
		dev_err(priv->dev, "tee_shm_alloc failed\n");
		err = PTR_ERR(entropy_shm_pool);
		goto out_sess;
	}

	priv->entropy_shm_pool = entropy_shm_pool;
	priv->dev = dev;
	dev_set_drvdata(dev, priv);

	rtc->ops = &optee_rtc_ops;

	err = optee_rtc_read_info(dev, rtc, &priv->features);
	if (err) {
		dev_err(dev, "Failed to get RTC features from OP-TEE\n");
		goto out_sess;
	}

	err = devm_rtc_register_device(rtc);
	if (err)
		goto out_sess;

	/*
	 * We must clear this bit after registering because rtc_register_device
	 * will set it if it sees that .set_offset is provided.
	 */
	if (!(priv->features & TA_RTC_FEATURE_CORRECTION))
		clear_bit(RTC_FEATURE_CORRECTION, rtc->features);

	return 0;

out_sess:
	tee_client_close_session(priv->ctx, priv->session_id);
out_ctx:
	tee_client_close_context(priv->ctx);

	return err;
}

static int optee_rtc_remove(struct device *dev)
{
	struct optee_rtc *priv = dev_get_drvdata(dev);

	tee_client_close_session(priv->ctx, priv->session_id);
	tee_client_close_context(priv->ctx);

	return 0;
}

static const struct tee_client_device_id optee_rtc_id_table[] = {
	{UUID_INIT(0xf389f8c8, 0x845f, 0x496c,
		   0x8b, 0xbe, 0xd6, 0x4b, 0xd2, 0x4c, 0x92, 0xfd)},
	{}
};

MODULE_DEVICE_TABLE(tee, optee_rtc_id_table);

static struct tee_client_driver optee_rtc_driver = {
	.id_table	= optee_rtc_id_table,
	.driver		= {
		.name		= "optee_rtc",
		.bus		= &tee_bus_type,
		.probe		= optee_rtc_probe,
		.remove		= optee_rtc_remove,
	},
};

static int __init optee_rtc_mod_init(void)
{
	return driver_register(&optee_rtc_driver.driver);
}

static void __exit optee_rtc_mod_exit(void)
{
	driver_unregister(&optee_rtc_driver.driver);
}

module_init(optee_rtc_mod_init);
module_exit(optee_rtc_mod_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Clément Léger <clement.leger@bootlin.com>");
MODULE_DESCRIPTION("OP-TEE based RTC driver");
