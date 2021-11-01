// SPDX-License-Identifier: GPL-2.0-or-later

/*												  */
/* The OCOTP driver for Sunplus	SP7021								  */
/*												  */
/* Copyright (C) 2019 Sunplus Technology Inc., All rights reseerved.				  */
/*												  */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>

#include "sunplus-ocotp.h"

static int efuse0_sunplus_platform_probe(struct platform_device *dev)
{
	return sp_ocotp_probe(dev);
}

const struct sp_otp_vX_t  sp_otp_v0 = {
	.size = QAC628_OTP_SIZE,
};

static const struct of_device_id sp_ocotp0_dt_ids[] = {
	{ .compatible = "sunplus,sp7021-ocotp", .data = &sp_otp_v0  },
	{ }
};
MODULE_DEVICE_TABLE(of, sp_ocotp0_dt_ids);

static struct platform_driver sp_otp0_driver = {
	.probe     = efuse0_sunplus_platform_probe,
	.remove    = sp_ocotp_remove,
	.driver    = {
		.name           = "sunplus,sp7021-ocotp",
		.of_match_table = sp_ocotp0_dt_ids,
	}
};

static int __init sp_otp0_drv_new(void)
{
	return platform_driver_register(&sp_otp0_driver);
}
subsys_initcall(sp_otp0_drv_new);

static void __exit sp_otp0_drv_del(void)
{
	platform_driver_unregister(&sp_otp0_driver);
}
module_exit(sp_otp0_drv_del);

MODULE_AUTHOR("Vincent Shih <vincent.shih@sunplus.com>");
MODULE_DESCRIPTION("Sunplus On-Chip OTP (eFuse0) driver");
MODULE_LICENSE("GPL v2");

