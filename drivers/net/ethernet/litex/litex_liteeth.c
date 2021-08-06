// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * LiteX Liteeth Ethernet
 *
 * Copyright 2017 Joel Stanley <joel@jms.id.au>
 *
 */

#include <linux/etherdevice.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/of_address.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/iopoll.h>

#define LITEETH_WRITER_SLOT       0x00
#define LITEETH_WRITER_LENGTH     0x04
#define LITEETH_WRITER_ERRORS     0x08
#define LITEETH_WRITER_EV_STATUS  0x0C
#define LITEETH_WRITER_EV_PENDING 0x10
#define LITEETH_WRITER_EV_ENABLE  0x14
#define LITEETH_READER_START      0x18
#define LITEETH_READER_READY      0x1C
#define LITEETH_READER_LEVEL      0x20
#define LITEETH_READER_SLOT       0x24
#define LITEETH_READER_LENGTH     0x28
#define LITEETH_READER_EV_STATUS  0x2C
#define LITEETH_READER_EV_PENDING 0x30
#define LITEETH_READER_EV_ENABLE  0x34
#define LITEETH_PREAMBLE_CRC      0x38
#define LITEETH_PREAMBLE_ERRORS   0x3C
#define LITEETH_CRC_ERRORS        0x40

#define LITEETH_PHY_CRG_RESET     0x00
#define LITEETH_MDIO_W            0x04
#define LITEETH_MDIO_R            0x0C

#define DRV_NAME	"liteeth"

#define LITEETH_BUFFER_SIZE		0x800
#define MAX_PKT_SIZE			LITEETH_BUFFER_SIZE

struct liteeth {
	void __iomem *base;
	void __iomem *mdio_base;
	struct net_device *netdev;
	struct device *dev;
	struct mii_bus *mii_bus;

	/* Link management */
	int cur_duplex;
	int cur_speed;

	/* Tx */
	int tx_slot;
	int num_tx_slots;
	void __iomem *tx_base;

	/* Rx */
	int rx_slot;
	int num_rx_slots;
	void __iomem *rx_base;
};


static int liteeth_rx(struct net_device *netdev)
{
	struct liteeth *priv = netdev_priv(netdev);
	struct sk_buff *skb;
	unsigned char *data;
	u8 rx_slot;
	int len;

	rx_slot = readb(priv->base + LITEETH_WRITER_SLOT);
	len = readl(priv->base + LITEETH_WRITER_LENGTH);

	skb = netdev_alloc_skb(netdev, len + NET_IP_ALIGN);
	if (!skb) {
		netdev_err(netdev, "couldn't get memory");
		netdev->stats.rx_dropped++;
		return NET_RX_DROP;
	}

	/* Ensure alignemnt of the ip header within the skb */
	skb_reserve(skb, NET_IP_ALIGN);
	if (len == 0 || len > 2048)
		return NET_RX_DROP;
	data = skb_put(skb, len);
	memcpy_fromio(data, priv->rx_base + rx_slot * LITEETH_BUFFER_SIZE, len);
	skb->protocol = eth_type_trans(skb, netdev);

	netdev->stats.rx_packets++;
	netdev->stats.rx_bytes += len;

	return netif_rx(skb);
}

static irqreturn_t liteeth_interrupt(int irq, void *dev_id)
{
	struct net_device *netdev = dev_id;
	struct liteeth *priv = netdev_priv(netdev);
	u8 reg;

	reg = readb(priv->base + LITEETH_READER_EV_PENDING);
	if (reg) {
		netdev->stats.tx_packets++;
		writeb(reg, priv->base + LITEETH_READER_EV_PENDING);
	}

	reg = readb(priv->base + LITEETH_WRITER_EV_PENDING);
	if (reg) {
		liteeth_rx(netdev);
		writeb(reg, priv->base + LITEETH_WRITER_EV_PENDING);
	}

	return IRQ_HANDLED;
}

static int liteeth_open(struct net_device *netdev)
{
	struct liteeth *priv = netdev_priv(netdev);
	int err;

	/* Clear pending events */
	writeb(1, priv->base + LITEETH_WRITER_EV_PENDING);
	writeb(1, priv->base + LITEETH_READER_EV_PENDING);

	err = request_irq(netdev->irq, liteeth_interrupt, 0, netdev->name, netdev);
	if (err) {
		netdev_err(netdev, "failed to request irq %d\n", netdev->irq);
		return err;
	}

	/* Enable IRQs */
	writeb(1, priv->base + LITEETH_WRITER_EV_ENABLE);
	writeb(1, priv->base + LITEETH_READER_EV_ENABLE);

	/* TODO: Remove these once we have working mdio support */
	priv->cur_duplex = DUPLEX_FULL;
	priv->cur_speed = SPEED_100;
	netif_carrier_on(netdev);

	netif_start_queue(netdev);

	return 0;
}

static int liteeth_stop(struct net_device *netdev)
{
	struct liteeth *priv = netdev_priv(netdev);

	netif_stop_queue(netdev);

	writeb(0, priv->base + LITEETH_WRITER_EV_ENABLE);
	writeb(0, priv->base + LITEETH_READER_EV_ENABLE);

	free_irq(netdev->irq, netdev);

	return 0;
}

static int liteeth_start_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct liteeth *priv = netdev_priv(netdev);
	void __iomem *txbuffer;
	int ret;
	u8 val;

	/* Reject oversize packets */
	if (unlikely(skb->len > MAX_PKT_SIZE)) {
		if (net_ratelimit())
			netdev_dbg(netdev, "tx packet too big\n");
		goto drop;
	}

	txbuffer = priv->tx_base + priv->tx_slot * LITEETH_BUFFER_SIZE;
	memcpy_toio(txbuffer, skb->data, skb->len);
	writeb(priv->tx_slot, priv->base + LITEETH_READER_SLOT);
	writew(skb->len, priv->base + LITEETH_READER_LENGTH);

	ret = readl_poll_timeout_atomic(priv->base + LITEETH_READER_READY, val, val, 5, 1000);
	if (ret == -ETIMEDOUT) {
		netdev_err(netdev, "LITEETH_READER_READY timed out\n");
		goto drop;
	}

	writeb(1, priv->base + LITEETH_READER_START);

	netdev->stats.tx_bytes += skb->len;

	priv->tx_slot = (priv->tx_slot + 1) % priv->num_tx_slots;
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
drop:
	/* Drop the packet */
	dev_kfree_skb_any(skb);
	netdev->stats.tx_dropped++;

	return NETDEV_TX_OK;
}

static const struct net_device_ops liteeth_netdev_ops = {
	.ndo_open		= liteeth_open,
	.ndo_stop		= liteeth_stop,
	.ndo_start_xmit         = liteeth_start_xmit,
};

static void liteeth_reset_hw(struct liteeth *priv)
{
	/* Reset, twice */
	writeb(0, priv->base + LITEETH_PHY_CRG_RESET);
	udelay(10);
	writeb(1, priv->base + LITEETH_PHY_CRG_RESET);
	udelay(10);
	writeb(0, priv->base + LITEETH_PHY_CRG_RESET);
	udelay(10);
}

static int liteeth_probe(struct platform_device *pdev)
{
	struct net_device *netdev;
	void __iomem *buf_base;
	struct resource *res;
	struct liteeth *priv;
	int irq, err;

	netdev = alloc_etherdev(sizeof(*priv));
	if (!netdev)
		return -ENOMEM;

	priv = netdev_priv(netdev);
	priv->netdev = netdev;
	priv->dev = &pdev->dev;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "Failed to get IRQ\n");
		goto err;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->base)) {
		err = PTR_ERR(priv->base);
		goto err;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	priv->mdio_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->mdio_base)) {
		err = PTR_ERR(priv->mdio_base);
		goto err;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	buf_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(buf_base)) {
		err = PTR_ERR(buf_base);
		goto err;
	}

	err = of_property_read_u32(pdev->dev.of_node, "rx-fifo-depth",
			&priv->num_rx_slots);
	if (err) {
		dev_err(&pdev->dev, "unable to get rx-fifo-depth\n");
		goto err;
	}

	err = of_property_read_u32(pdev->dev.of_node, "tx-fifo-depth",
			&priv->num_tx_slots);
	if (err) {
		dev_err(&pdev->dev, "unable to get tx-fifo-depth\n");
		goto err;
	}

	/* Rx slots */
	priv->rx_base = buf_base;
	priv->rx_slot = 0;

	/* Tx slots come after Rx slots */
	priv->tx_base = buf_base + priv->num_rx_slots * LITEETH_BUFFER_SIZE;
	priv->tx_slot = 0;

	err = of_get_mac_address(pdev->dev.of_node, netdev->dev_addr);
	if (err)
		eth_hw_addr_random(netdev);

	SET_NETDEV_DEV(netdev, &pdev->dev);
	platform_set_drvdata(pdev, netdev);

	netdev->netdev_ops = &liteeth_netdev_ops;
	netdev->irq = irq;

	liteeth_reset_hw(priv);

	err = register_netdev(netdev);
	if (err) {
		dev_err(&pdev->dev, "Failed to register netdev\n");
		goto err;
	}

	netdev_info(netdev, "irq %d, mapped at %px\n", netdev->irq, priv->base);

	return 0;
err:
	free_netdev(netdev);
	return err;
}

static int liteeth_remove(struct platform_device *pdev)
{
	struct net_device *netdev = platform_get_drvdata(pdev);

	unregister_netdev(netdev);
	free_netdev(netdev);

	return 0;
}

static const struct of_device_id liteeth_of_match[] = {
	{ .compatible = "litex,liteeth" },
	{ }
};
MODULE_DEVICE_TABLE(of, liteeth_of_match);

static struct platform_driver liteeth_driver = {
	.probe = liteeth_probe,
	.remove = liteeth_remove,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = liteeth_of_match,
	},
};
module_platform_driver(liteeth_driver);

MODULE_AUTHOR("Joel Stanley <joel@jms.id.au>");
MODULE_LICENSE("GPL");
