/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Raspberry Pi Sense HAT core driver
 * http://raspberrypi.org
 *
 * Copyright (C) 2015 Raspberry Pi
 * Copyright (C) 2021 Charles Mirabile, Mwesigwa Guma, Joel Savitz
 *
 * Original Author: Serge Schneider
 * Revised for upstream Linux by: Charles Mirabile, Mwesigwa Guma, Joel Savitz
 */

#ifndef __LINUX_MFD_SENSEHAT_H_
#define __LINUX_MFD_SENSEHAT_H_
#include <linux/miscdevice.h>

//8x8 display with 3 color channels
typedef u8 sensehat_fb_t[8][3][8];

#define SENSEHAT_DISPLAY		0x00
#define SENSEHAT_WAI			0xF0
#define SENSEHAT_VER			0xF1
#define SENSEHAT_KEYS			0xF2
#define SENSEHAT_EE_WP			0xF3

#define SENSEHAT_ID			's'

#define SENSEDISP_IOC_MAGIC 0xF1

#define SENSEDISP_IOGET_GAMMA _IO(SENSEDISP_IOC_MAGIC, 0)
#define SENSEDISP_IOSET_GAMMA _IO(SENSEDISP_IOC_MAGIC, 1)
#define SENSEDISP_IORESET_GAMMA _IO(SENSEDISP_IOC_MAGIC, 2)

struct sensehat {
	struct device *dev;
	struct i2c_client *i2c_client;
	struct regmap *regmap;

	/* Client devices */
	struct sensehat_joystick {
		struct platform_device *pdev;
		struct input_dev *keys_dev;
		struct gpio_desc *keys_desc;
		int keys_irq;
	} joystick;

	struct sensehat_display {
		struct platform_device *pdev;
		struct miscdevice mdev;
		struct mutex rw_mtx;
		u8 gamma[32];
		struct {
			u16 b:5, u:1, g:5, r:5;
		} vmem[8][8];
	} display;
};

enum gamma_preset {
	GAMMA_DEFAULT = 0,
	GAMMA_LOWLIGHT,
	GAMMA_PRESET_COUNT,
};

#endif
