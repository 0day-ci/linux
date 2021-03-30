// SPDX-License-Identifier: GPL-2.0+
/*
 * The driver for BMC side of Aspeed SSIF interface
 *
 * Copyright (c) 2021, Ampere Computing LLC
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <linux/i2c.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/iopoll.h>

#include "ssif_bmc.h"

struct aspeed_i2c_bus {
	struct i2c_adapter              adap;
	struct device                   *dev;
	void __iomem                    *base;
	struct reset_control            *rst;
	/* Synchronizes I/O mem access to base. */
	spinlock_t                      lock;
};

#define ASPEED_I2C_INTR_CTRL_REG	0x0c
#define ASPEED_I2CD_INTR_SLAVE_MATCH	BIT(7)
#define ASPEED_I2CD_INTR_RX_DONE	BIT(2)
static void aspeed_i2c_enable_interrupt(struct aspeed_i2c_bus *bus, unsigned long mask)
{
	unsigned long current_mask;

	current_mask = readl(bus->base + ASPEED_I2C_INTR_CTRL_REG);
	writel(current_mask | mask, bus->base + ASPEED_I2C_INTR_CTRL_REG);
}

static void aspeed_i2c_disable_interrupt(struct aspeed_i2c_bus *bus, unsigned long mask)
{
	unsigned long current_mask;

	current_mask = readl(bus->base + ASPEED_I2C_INTR_CTRL_REG);
	writel(current_mask & ~mask, bus->base + ASPEED_I2C_INTR_CTRL_REG);
}

static void aspeed_set_ssif_bmc_status(struct ssif_bmc_ctx *ssif_bmc, unsigned int status)
{
	struct aspeed_i2c_bus *bus;
	unsigned long flags;

	bus = (struct aspeed_i2c_bus *)ssif_bmc->priv;
	if (!bus)
		return;

	spin_lock_irqsave(&bus->lock, flags);

	if (status & SSIF_BMC_BUSY) {
		/* Ignore RX_DONE and SLAVE_MATCH when slave busy processing */
		aspeed_i2c_disable_interrupt(bus, ASPEED_I2CD_INTR_RX_DONE);
		aspeed_i2c_disable_interrupt(bus, ASPEED_I2CD_INTR_SLAVE_MATCH);
	} else if (status & SSIF_BMC_READY) {
		/* Enable RX_DONE and SLAVE_MATCH when slave ready */
		aspeed_i2c_enable_interrupt(bus, ASPEED_I2CD_INTR_RX_DONE);
		aspeed_i2c_enable_interrupt(bus, ASPEED_I2CD_INTR_SLAVE_MATCH);
	}

	spin_unlock_irqrestore(&bus->lock, flags);
}

static int ssif_bmc_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct ssif_bmc_ctx *ssif_bmc;

	ssif_bmc = ssif_bmc_alloc(client, sizeof(struct aspeed_i2c_bus));
	if (IS_ERR(ssif_bmc))
		return PTR_ERR(ssif_bmc);

	ssif_bmc->priv = i2c_get_adapdata(client->adapter);
	ssif_bmc->set_ssif_bmc_status = aspeed_set_ssif_bmc_status;

	return 0;
}

static int ssif_bmc_remove(struct i2c_client *client)
{
	struct ssif_bmc_ctx *ssif_bmc = i2c_get_clientdata(client);

	i2c_slave_unregister(client);
	misc_deregister(&ssif_bmc->miscdev);

	return 0;
}

static const struct of_device_id ssif_bmc_match[] = {
	{ .compatible = "aspeed,ast2500-ssif-bmc" },
	{ },
};

static const struct i2c_device_id ssif_bmc_id[] = {
	{ DEVICE_NAME, 0 },
	{ },
};

MODULE_DEVICE_TABLE(i2c, ssif_bmc_id);

static struct i2c_driver ssif_bmc_driver = {
	.driver		= {
		.name		= DEVICE_NAME,
		.of_match_table = ssif_bmc_match,
	},
	.probe		= ssif_bmc_probe,
	.remove		= ssif_bmc_remove,
	.id_table	= ssif_bmc_id,
};

module_i2c_driver(ssif_bmc_driver);

MODULE_AUTHOR("Chuong Tran <chuong@os.amperecomputing.com>");
MODULE_AUTHOR("Quan Nguyen <quan@os.amperecomputing.com>");
MODULE_DESCRIPTION("Linux device driver of Aspeed BMC IPMI SSIF interface.");
MODULE_LICENSE("GPL v2");
