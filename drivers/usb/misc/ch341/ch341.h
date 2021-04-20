/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Definitions for CH341 driver
 */

#include <linux/usb.h>
#include <linux/i2c.h>
#include <linux/gpio.h>

#define DEFAULT_TIMEOUT 1000	/* 1s USB requests timeout */

/* I2C - The maximum request size is 128 bytes, for reading and
 * writing. The adapter will get the buffer in packets of up to 32
 * bytes. The I2C stream must start and stop in each 32-byte packet.
 * Reading must also be split, up to 32-byte per packet.
 */
#define PKT_SIZE 32
#define PKT_COUNT 4

struct ch341_device {
	struct usb_device *usb_dev;
	struct usb_interface *iface;
	struct mutex usb_lock;

	int ep_in;
	int ep_out;

	/* I2C */
	struct i2c_adapter adapter;
	bool i2c_init;

	/* I2C request and response state */
	int idx_out;		/* current offset in buf */
	int out_pkt;		/* current packet */
	u8 i2c_buf[PKT_COUNT * PKT_SIZE];

	/* GPIO */
	struct gpio_chip gpio;
	struct mutex gpio_lock;
	bool gpio_init;
	u8 gpio_dir;		/* 1 bit per pin, 0=IN, 1=OUT. */
	u8 gpio_last_read;	/* last GPIO values read */
	u8 gpio_last_written;	/* last GPIO values written */
	u8 gpio_buf[PKT_SIZE];
};

void ch341_i2c_remove(struct ch341_device *dev);
int ch341_i2c_init(struct ch341_device *dev);
void ch341_gpio_remove(struct ch341_device *dev);
int ch341_gpio_init(struct ch341_device *dev);
