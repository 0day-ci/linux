// SPDX-License-Identifier: GPL-2.0
/* Copyright 2021 NXP */
#include <linux/gpio/consumer.h>
#include <linux/of_device.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/spi/spi.h>
#include "sja1105.h"

static void sja1105_hw_reset(struct gpio_desc *gpio, unsigned int pulse_len,
			     unsigned int startup_delay)
{
	gpiod_set_value_cansleep(gpio, 1);
	/* Wait for minimum reset pulse length */
	msleep(pulse_len);
	gpiod_set_value_cansleep(gpio, 0);
	/* Wait until chip is ready after reset */
	msleep(startup_delay);
}

static int sja1105_parse_rgmii_delays(struct sja1105_private *priv)
{
	struct dsa_switch *ds = priv->ds;
	int port;

	for (port = 0; port < ds->num_ports; port++) {
		if (!priv->fixed_link[port])
			continue;

		if (priv->phy_mode[port] == PHY_INTERFACE_MODE_RGMII_RXID ||
		    priv->phy_mode[port] == PHY_INTERFACE_MODE_RGMII_ID)
			priv->rgmii_rx_delay[port] = true;

		if (priv->phy_mode[port] == PHY_INTERFACE_MODE_RGMII_TXID ||
		    priv->phy_mode[port] == PHY_INTERFACE_MODE_RGMII_ID)
			priv->rgmii_tx_delay[port] = true;

		if ((priv->rgmii_rx_delay[port] || priv->rgmii_tx_delay[port]) &&
		    !priv->info->setup_rgmii_delay)
			return -EINVAL;
	}
	return 0;
}

static int sja1105_parse_ports_node(struct sja1105_private *priv,
				    struct device_node *ports_node)
{
	struct device *dev = &priv->spidev->dev;
	struct device_node *child;

	for_each_available_child_of_node(ports_node, child) {
		struct device_node *phy_node;
		phy_interface_t phy_mode;
		u32 index;
		int err;

		/* Get switch port number from DT */
		if (of_property_read_u32(child, "reg", &index) < 0) {
			dev_err(dev, "Port number not defined in device tree "
				"(property \"reg\")\n");
			of_node_put(child);
			return -ENODEV;
		}

		/* Get PHY mode from DT */
		err = of_get_phy_mode(child, &phy_mode);
		if (err) {
			dev_err(dev, "Failed to read phy-mode or "
				"phy-interface-type property for port %d\n",
				index);
			of_node_put(child);
			return -ENODEV;
		}

		phy_node = of_parse_phandle(child, "phy-handle", 0);
		if (!phy_node) {
			if (!of_phy_is_fixed_link(child)) {
				dev_err(dev, "phy-handle or fixed-link "
					"properties missing!\n");
				of_node_put(child);
				return -ENODEV;
			}
			/* phy-handle is missing, but fixed-link isn't.
			 * So it's a fixed link. Default to PHY role.
			 */
			priv->fixed_link[index] = true;
		} else {
			of_node_put(phy_node);
		}

		priv->phy_mode[index] = phy_mode;
	}

	return 0;
}

static int sja1105_parse_dt(struct sja1105_private *priv)
{
	struct device *dev = &priv->spidev->dev;
	struct device_node *switch_node = dev->of_node;
	struct device_node *ports_node;
	int rc;

	ports_node = of_get_child_by_name(switch_node, "ports");
	if (!ports_node)
		ports_node = of_get_child_by_name(switch_node, "ethernet-ports");
	if (!ports_node) {
		dev_err(dev, "Incorrect bindings: absent \"ports\" node\n");
		return -ENODEV;
	}

	rc = sja1105_parse_ports_node(priv, ports_node);
	of_node_put(ports_node);

	return rc;
}

static const struct of_device_id sja1105_dt_ids[];

static int sja1105_check_device_id(struct sja1105_private *priv)
{
	const struct sja1105_regs *regs = priv->info->regs;
	u8 prod_id[SJA1105_SIZE_DEVICE_ID] = {0};
	struct device *dev = &priv->spidev->dev;
	const struct of_device_id *match;
	u32 device_id;
	u64 part_no;
	int rc;

	rc = sja1105_xfer_u32(priv, SPI_READ, regs->device_id, &device_id,
			      NULL);
	if (rc < 0)
		return rc;

	rc = sja1105_xfer_buf(priv, SPI_READ, regs->prod_id, prod_id,
			      SJA1105_SIZE_DEVICE_ID);
	if (rc < 0)
		return rc;

	sja1105_unpack(prod_id, &part_no, 19, 4, SJA1105_SIZE_DEVICE_ID);

	for (match = sja1105_dt_ids; match->compatible[0]; match++) {
		const struct sja1105_info *info = match->data;

		/* Is what's been probed in our match table at all? */
		if (info->device_id != device_id || info->part_no != part_no)
			continue;

		/* But is it what's in the device tree? */
		if (priv->info->device_id != device_id ||
		    priv->info->part_no != part_no) {
			dev_warn(dev, "Device tree specifies chip %s but found %s, please fix it!\n",
				 priv->info->name, info->name);
			/* It isn't. No problem, pick that up. */
			priv->info = info;
		}

		return 0;
	}

	dev_err(dev, "Unexpected {device ID, part number}: 0x%x 0x%llx\n",
		device_id, part_no);

	return -ENODEV;
}

static int sja1105_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct sja1105_private *priv;
	size_t max_xfer, max_msg;
	struct dsa_switch *ds;
	int rc;

	if (!dev->of_node) {
		dev_err(dev, "No DTS bindings for SJA1105 driver\n");
		return -EINVAL;
	}

	priv = devm_kzalloc(dev, sizeof(struct sja1105_private), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	/* Configure the optional reset pin and bring up switch */
	priv->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(priv->reset_gpio))
		dev_dbg(dev, "reset-gpios not defined, ignoring\n");
	else
		sja1105_hw_reset(priv->reset_gpio, 1, 1);

	/* Populate our driver private structure (priv) based on
	 * the device tree node that was probed (spi)
	 */
	priv->spidev = spi;
	spi_set_drvdata(spi, priv);

	/* Configure the SPI bus */
	spi->bits_per_word = 8;
	rc = spi_setup(spi);
	if (rc < 0) {
		dev_err(dev, "Could not init SPI\n");
		return rc;
	}

	/* In sja1105_xfer, we send spi_messages composed of two spi_transfers:
	 * a small one for the message header and another one for the current
	 * chunk of the packed buffer.
	 * Check that the restrictions imposed by the SPI controller are
	 * respected: the chunk buffer is smaller than the max transfer size,
	 * and the total length of the chunk plus its message header is smaller
	 * than the max message size.
	 * We do that during probe time since the maximum transfer size is a
	 * runtime invariant.
	 */
	max_xfer = spi_max_transfer_size(spi);
	max_msg = spi_max_message_size(spi);

	/* We need to send at least one 64-bit word of SPI payload per message
	 * in order to be able to make useful progress.
	 */
	if (max_msg < SJA1105_SIZE_SPI_MSG_HEADER + 8) {
		dev_err(dev, "SPI master cannot send large enough buffers, aborting\n");
		return -EINVAL;
	}

	priv->max_xfer_len = SJA1105_SIZE_SPI_MSG_MAXLEN;
	if (priv->max_xfer_len > max_xfer)
		priv->max_xfer_len = max_xfer;
	if (priv->max_xfer_len > max_msg - SJA1105_SIZE_SPI_MSG_HEADER)
		priv->max_xfer_len = max_msg - SJA1105_SIZE_SPI_MSG_HEADER;

	priv->info = of_device_get_match_data(dev);

	/* Detect hardware device */
	rc = sja1105_check_device_id(priv);
	if (rc < 0) {
		dev_err(dev, "Device ID check failed: %d\n", rc);
		return rc;
	}

	dev_info(dev, "Probed switch chip: %s\n", priv->info->name);

	ds = devm_kzalloc(dev, sizeof(*ds), GFP_KERNEL);
	if (!ds)
		return -ENOMEM;

	ds->dev = dev;
	ds->num_ports = priv->info->num_ports;
	ds->ops = &sja1105_switch_ops;
	ds->priv = priv;
	priv->ds = ds;

	mutex_init(&priv->ptp_data.lock);
	mutex_init(&priv->mgmt_lock);

	rc = sja1105_parse_dt(priv);
	if (rc < 0) {
		dev_err(ds->dev, "Failed to parse DT: %d\n", rc);
		return rc;
	}

	/* Error out early if internal delays are required through DT
	 * and we can't apply them.
	 */
	rc = sja1105_parse_rgmii_delays(priv);
	if (rc < 0) {
		dev_err(ds->dev, "RGMII delay not supported\n");
		return rc;
	}

	if (IS_ENABLED(CONFIG_NET_SCH_CBS)) {
		priv->cbs = devm_kcalloc(dev, priv->info->num_cbs_shapers,
					 sizeof(struct sja1105_cbs_entry),
					 GFP_KERNEL);
		if (!priv->cbs)
			return -ENOMEM;
	}

	return dsa_register_switch(priv->ds);
}

static int sja1105_remove(struct spi_device *spi)
{
	struct sja1105_private *priv = spi_get_drvdata(spi);
	struct dsa_switch *ds = priv->ds;

	dsa_unregister_switch(ds);

	return 0;
}

static const struct of_device_id sja1105_dt_ids[] = {
	{ .compatible = "nxp,sja1105e", .data = &sja1105e_info },
	{ .compatible = "nxp,sja1105t", .data = &sja1105t_info },
	{ .compatible = "nxp,sja1105p", .data = &sja1105p_info },
	{ .compatible = "nxp,sja1105q", .data = &sja1105q_info },
	{ .compatible = "nxp,sja1105r", .data = &sja1105r_info },
	{ .compatible = "nxp,sja1105s", .data = &sja1105s_info },
	{ .compatible = "nxp,sja1110a", .data = &sja1110a_info },
	{ .compatible = "nxp,sja1110b", .data = &sja1110b_info },
	{ .compatible = "nxp,sja1110c", .data = &sja1110c_info },
	{ .compatible = "nxp,sja1110d", .data = &sja1110d_info },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, sja1105_dt_ids);

static struct spi_driver sja1105_driver = {
	.driver = {
		.name  = "sja1105",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(sja1105_dt_ids),
	},
	.probe  = sja1105_probe,
	.remove = sja1105_remove,
};

module_spi_driver(sja1105_driver);

MODULE_AUTHOR("Vladimir Oltean <olteanv@gmail.com>");
MODULE_AUTHOR("Georg Waibel <georg.waibel@sensor-technik.de>");
MODULE_DESCRIPTION("SJA1105 Driver");
MODULE_LICENSE("GPL v2");
