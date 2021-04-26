// SPDX-License-Identifier: GPL-2.0+
/*
 * Multi-function driver for the IDT ClockMatrix(TM) and 82P33xxx families of
 * timing and synchronization devices.
 *
 * Copyright (C) 2019 Integrated Device Technology, Inc., a Renesas Company.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/regmap.h>
#include <linux/of.h>
#include <linux/mfd/core.h>
#include <linux/mfd/rsmu.h>
#include "rsmu_private.h"

/*
 * 16-bit register address: the lower 7 bits of the register address come
 * from the offset addr byte and the upper 9 bits come from the page register.
 */
#define	RSMU_CM_PAGE_ADDR		0x7C
#define	RSMU_SABRE_PAGE_ADDR		0x7F
#define	RSMU_HIGHER_ADDR_MASK		0xFF80
#define	RSMU_HIGHER_ADDR_SHIFT		7
#define	RSMU_LOWER_ADDR_MASK		0x7F

/* Current mfd device index */
static atomic_t rsmu_ndevs = ATOMIC_INIT(0);

/* Platform data */
static struct rsmu_pdata rsmu_pdata[RSMU_MAX_MFD_DEV];

/* clockmatrix phc devices */
static struct mfd_cell rsmu_cm_pdev[RSMU_MAX_MFD_DEV] = {
	[0] = {
		.name = "idtcm-ptp0",
		.of_compatible	= "renesas,idtcm-ptp0",
	},
	[1] = {
		.name = "idtcm-ptp1",
		.of_compatible	= "renesas,idtcm-ptp1",
	},
	[2] = {
		.name = "idtcm-ptp2",
		.of_compatible	= "renesas,idtcm-ptp2",
	},
	[3] = {
		.name = "idtcm-ptp3",
		.of_compatible	= "renesas,idtcm-ptp3",
	},
};

/* sabre phc devices */
static struct mfd_cell rsmu_sabre_pdev[RSMU_MAX_MFD_DEV] = {
	[0] = {
		.name = "idt82p33-ptp0",
		.of_compatible	= "renesas,idt82p33-ptp0",
	},
	[1] = {
		.name = "idt82p33-ptp1",
		.of_compatible	= "renesas,idt82p33-ptp1",
	},
	[2] = {
		.name = "idt82p33-ptp2",
		.of_compatible	= "renesas,idt82p33-ptp2",
	},
	[3] = {
		.name = "idt82p33-ptp3",
		.of_compatible	= "renesas,idt82p33-ptp3",
	},
};

/* rsmu character devices */
static struct mfd_cell rsmu_cdev[RSMU_MAX_MFD_DEV] = {
	[0] = {
		.name = "rsmu-cdev0",
		.of_compatible	= "renesas,rsmu-cdev0",
	},
	[1] = {
		.name = "rsmu-cdev1",
		.of_compatible	= "renesas,rsmu-cdev1",
	},
	[2] = {
		.name = "rsmu-cdev2",
		.of_compatible	= "renesas,rsmu-cdev2",
	},
	[3] = {
		.name = "rsmu-cdev3",
		.of_compatible	= "renesas,rsmu-cdev3",
	},
};

static int rsmu_read_device(struct rsmu_dev *rsmu, u8 reg, u8 *buf, u16 bytes)
{
	struct spi_device *client = rsmu->client;
	struct spi_transfer xfer = {0};
	struct spi_message msg;
	u8 cmd[256] = {0};
	u8 rsp[256] = {0};
	int ret;

	cmd[0] = reg | 0x80;
	xfer.rx_buf = rsp;
	xfer.len = bytes + 1;
	xfer.tx_buf = cmd;
	xfer.bits_per_word = client->bits_per_word;
	xfer.speed_hz = client->max_speed_hz;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);

	ret = spi_sync(client, &msg);
	if (ret >= 0)
		memcpy(buf, &rsp[1], xfer.len-1);

	return ret;
}

static int rsmu_write_device(struct rsmu_dev *rsmu, u8 reg, u8 *buf, u16 bytes)
{
	struct spi_device *client = rsmu->client;
	struct spi_transfer xfer = {0};
	struct spi_message msg;
	u8 cmd[256] = {0};
	int ret = -1;

	cmd[0] = reg;
	memcpy(&cmd[1], buf, bytes);

	xfer.len = bytes + 1;
	xfer.tx_buf = cmd;
	xfer.bits_per_word = client->bits_per_word;
	xfer.speed_hz = client->max_speed_hz;
	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);

	ret = spi_sync(client, &msg);
	return ret;
}

static int rsmu_write_page_register(struct rsmu_dev *rsmu, u16 reg)
{
	u8 buf[2];
	u16 bytes;
	u16 page;
	u8 preg;
	int err;

	switch (rsmu->type) {
	case RSMU_CM:
		preg = RSMU_CM_PAGE_ADDR;
		page = reg & RSMU_HIGHER_ADDR_MASK;
		buf[0] = (u8)(page & 0xff);
		buf[1] = (u8)((page >> 8) & 0xff);
		bytes = 2;
		break;
	case RSMU_SABRE:
		preg = RSMU_SABRE_PAGE_ADDR;
		page = reg >> RSMU_HIGHER_ADDR_SHIFT;
		buf[0] = (u8)(page & 0xff);
		bytes = 1;
		break;
	default:
		return -EINVAL;
	}

	if (rsmu->page == page)
		return 0;

	err = rsmu_write_device(rsmu, preg, buf, bytes);

	if (err) {
		rsmu->page = 0xffff;
		dev_err(rsmu->dev,
			"failed to set page offset 0x%x\n", page);
	} else {
		rsmu->page = page;
	}

	return err;
}

int rsmu_read(struct device *dev, u16 reg, u8 *buf, u16 size)
{
	struct rsmu_dev *rsmu = dev_get_drvdata(dev);
	u8 addr = (u8)(reg & RSMU_LOWER_ADDR_MASK);
	int err;

	err = rsmu_write_page_register(rsmu, reg);
	if (err)
		return err;

	err = rsmu_read_device(rsmu, addr, buf, size);
	if (err)
		dev_err(rsmu->dev,
			"failed to read offset address 0x%x\n", addr);

	return err;
}
EXPORT_SYMBOL_GPL(rsmu_read);

int rsmu_write(struct device *dev, u16 reg, u8 *buf, u16 size)
{
	struct rsmu_dev *rsmu = dev_get_drvdata(dev);
	u8 addr = (u8)(reg & RSMU_LOWER_ADDR_MASK);
	int err;

	err = rsmu_write_page_register(rsmu, reg);
	if (err)
		return err;

	err = rsmu_write_device(rsmu, addr, buf, size);
	if (err)
		dev_err(rsmu->dev,
			"failed to write offset address 0x%x\n", addr);

	return err;
}
EXPORT_SYMBOL_GPL(rsmu_write);

static int rsmu_mfd_init(struct rsmu_dev *rsmu, struct mfd_cell *mfd,
			 struct rsmu_pdata *pdata)
{
	int ret;

	mfd->platform_data = pdata;
	mfd->pdata_size = sizeof(struct rsmu_pdata);

	ret = mfd_add_devices(rsmu->dev, -1, mfd, 1, NULL, 0, NULL);
	if (ret < 0) {
		dev_err(rsmu->dev, "mfd_add_devices failed with %s\n",
			mfd->name);
		return ret;
	}

	return ret;
}

static int rsmu_dev_init(struct rsmu_dev *rsmu)
{
	struct rsmu_pdata *pdata;
	struct mfd_cell *pmfd;
	struct mfd_cell *cmfd;
	int ret;

	/* Initialize device index */
	rsmu->index = atomic_read(&rsmu_ndevs);
	if (rsmu->index >= RSMU_MAX_MFD_DEV)
		return -ENODEV;

	/* Initialize platform data */
	pdata = &rsmu_pdata[rsmu->index];
	pdata->lock = &rsmu->lock;
	pdata->type = rsmu->type;
	pdata->index = rsmu->index;

	/* Initialize MFD devices */
	cmfd = &rsmu_cdev[rsmu->index];
	if (rsmu->type == RSMU_CM)
		pmfd = &rsmu_cm_pdev[rsmu->index];
	else if (rsmu->type == RSMU_SABRE)
		pmfd = &rsmu_sabre_pdev[rsmu->index];
	else
		return -EINVAL;

	ret = rsmu_mfd_init(rsmu, pmfd, pdata);
	if (ret)
		return ret;

	ret = rsmu_mfd_init(rsmu, cmfd, pdata);
	if (ret)
		return ret;

	atomic_inc(&rsmu_ndevs);
	return 0;
}

static int rsmu_dt_init(struct rsmu_dev *rsmu)
{
	struct device_node *np = rsmu->dev->of_node;

	rsmu->type = RSMU_NONE;
	if (of_device_is_compatible(np, "idt,8a34000")) {
		rsmu->type = RSMU_CM;
	} else if (of_device_is_compatible(np, "idt,82p33810")) {
		rsmu->type = RSMU_SABRE;
	} else {
		dev_err(rsmu->dev, "unknown RSMU device\n");
		return -EINVAL;
	}

	return 0;
}

static int rsmu_probe(struct spi_device *client)
{
	struct rsmu_dev *rsmu;
	int ret;

	rsmu = devm_kzalloc(&client->dev, sizeof(struct rsmu_dev), GFP_KERNEL);
	if (rsmu == NULL)
		return -ENOMEM;

	spi_set_drvdata(client, rsmu);
	mutex_init(&rsmu->lock);
	rsmu->dev = &client->dev;
	rsmu->client = client;

	ret = rsmu_dt_init(rsmu);
	if (ret)
		return ret;

	mutex_lock(&rsmu->lock);
	ret = rsmu_dev_init(rsmu);
	mutex_unlock(&rsmu->lock);

	if (ret == 0)
		dev_info(rsmu->dev, "Probe %s successful\n",
			 rsmu_name(rsmu->type));

	return ret;
}

static int rsmu_remove(struct spi_device *client)
{
	struct rsmu_dev *rsmu = spi_get_drvdata(client);

	mfd_remove_devices(&client->dev);
	mutex_destroy(&rsmu->lock);
	atomic_dec(&rsmu_ndevs);

	return 0;
}

static const struct spi_device_id rsmu_id[] = {
	{ "8a34000", 0 },
	{ "82p33810", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, rsmu_id);

static const struct of_device_id rsmu_of_match[] = {
	{.compatible = "idt,8a34000", },
	{.compatible = "idt,82p33810", },
	{},
};
MODULE_DEVICE_TABLE(of, rsmu_of_match);

static struct spi_driver rsmu_driver = {
	.driver = {
		   .name = "rsmu-spi",
		   .of_match_table = of_match_ptr(rsmu_of_match),
	},
	.probe = rsmu_probe,
	.remove	= rsmu_remove,
	.id_table = rsmu_id,
};

static int __init rsmu_init(void)
{
	return spi_register_driver(&rsmu_driver);
}
/* init early so consumer devices can complete system boot */
subsys_initcall(rsmu_init);

static void __exit rsmu_exit(void)
{
	spi_unregister_driver(&rsmu_driver);
}
module_exit(rsmu_exit);

MODULE_DESCRIPTION("Renesas SMU SPI multi-function driver");
MODULE_LICENSE("GPL");
