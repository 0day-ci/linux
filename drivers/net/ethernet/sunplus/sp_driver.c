// SPDX-License-Identifier: GPL-2.0
/* Copyright Sunplus Technology Co., Ltd.
 *       All rights reserved.
 */

#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/nvmem-consumer.h>
#include <linux/of_net.h>
#include "sp_driver.h"
#include "sp_phy.h"

static const char def_mac_addr[ETHERNET_MAC_ADDR_LEN] = {
	0xfc, 0x4b, 0xbc, 0x00, 0x00, 0x00
};

/*********************************************************************
 *
 * net_device_ops
 *
 **********************************************************************/
static int ethernet_open(struct net_device *ndev)
{
	struct sp_mac *mac = netdev_priv(ndev);

	netdev_dbg(ndev, "Open port = %x\n", mac->lan_port);

	mac->comm->enable |= mac->lan_port;

	hal_mac_start(mac);
	write_sw_int_mask0(mac, read_sw_int_mask0(mac) & ~(MAC_INT_TX | MAC_INT_RX));

	netif_carrier_on(ndev);
	if (netif_carrier_ok(ndev))
		netif_start_queue(ndev);

	return 0;
}

static int ethernet_stop(struct net_device *ndev)
{
	struct sp_mac *mac = netdev_priv(ndev);

	netif_stop_queue(ndev);
	netif_carrier_off(ndev);

	mac->comm->enable &= ~mac->lan_port;

	hal_mac_stop(mac);

	return 0;
}

/* Transmit a packet (called by the kernel) */
static int ethernet_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct sp_mac *mac = netdev_priv(ndev);
	struct sp_common *comm = mac->comm;
	u32 tx_pos;
	u32 cmd1;
	u32 cmd2;
	struct mac_desc *txdesc;
	struct skb_info *skbinfo;
	unsigned long flags;

	if (unlikely(comm->tx_desc_full == 1)) {
		// No TX descriptors left. Wait for tx interrupt.
		netdev_info(ndev, "TX descriptor queue full when xmit!\n");
		return NETDEV_TX_BUSY;
	}

	/* if skb size shorter than 60, fill it with '\0' */
	if (unlikely(skb->len < ETH_ZLEN)) {
		if (skb_tailroom(skb) >= (ETH_ZLEN - skb->len)) {
			memset(__skb_put(skb, ETH_ZLEN - skb->len), '\0',
			       ETH_ZLEN - skb->len);
		} else {
			struct sk_buff *old_skb = skb;

			skb = dev_alloc_skb(ETH_ZLEN + TX_OFFSET);
			if (skb) {
				memset(skb->data + old_skb->len, '\0',
				       ETH_ZLEN - old_skb->len);
				memcpy(skb->data, old_skb->data, old_skb->len);
				skb_put(skb, ETH_ZLEN);	/* add data to an sk_buff */
				dev_kfree_skb_irq(old_skb);
			} else {
				skb = old_skb;
			}
		}
	}

	spin_lock_irqsave(&comm->tx_lock, flags);
	tx_pos = comm->tx_pos;
	txdesc = &comm->tx_desc[tx_pos];
	skbinfo = &comm->tx_temp_skb_info[tx_pos];
	skbinfo->len = skb->len;
	skbinfo->skb = skb;
	skbinfo->mapping = dma_map_single(&comm->pdev->dev, skb->data,
					  skb->len, DMA_TO_DEVICE);
	cmd1 = (OWN_BIT | FS_BIT | LS_BIT | (mac->to_vlan << 12) | (skb->len & LEN_MASK));
	cmd2 = skb->len & LEN_MASK;

	if (tx_pos == (TX_DESC_NUM - 1))
		cmd2 |= EOR_BIT;

	txdesc->addr1 = skbinfo->mapping;
	txdesc->cmd2 = cmd2;
	wmb();	// Set OWN_BIT after other fields of descriptor are effective.
	txdesc->cmd1 = cmd1;

	NEXT_TX(tx_pos);

	if (unlikely(tx_pos == comm->tx_done_pos)) {
		netif_stop_queue(ndev);
		comm->tx_desc_full = 1;
	}
	comm->tx_pos = tx_pos;
	wmb();			// make sure settings are effective.

	/* trigger gmac to transmit */
	hal_tx_trigger(mac);

	spin_unlock_irqrestore(&mac->comm->tx_lock, flags);
	return NETDEV_TX_OK;
}

static void ethernet_set_rx_mode(struct net_device *ndev)
{
	if (ndev) {
		struct sp_mac *mac = netdev_priv(ndev);
		struct sp_common *comm = mac->comm;
		unsigned long flags;

		spin_lock_irqsave(&comm->ioctl_lock, flags);
		hal_rx_mode_set(ndev);
		spin_unlock_irqrestore(&comm->ioctl_lock, flags);
	}
}

static int ethernet_set_mac_address(struct net_device *ndev, void *addr)
{
	struct sockaddr *hwaddr = (struct sockaddr *)addr;
	struct sp_mac *mac = netdev_priv(ndev);

	if (netif_running(ndev))
		return -EBUSY;

	memcpy(ndev->dev_addr, hwaddr->sa_data, ndev->addr_len);

	/* Delete the old Ethernet MAC address */
	netdev_dbg(ndev, "HW Addr = %pM\n", mac->mac_addr);
	if (is_valid_ether_addr(mac->mac_addr))
		hal_mac_addr_del(mac);

	/* Set the Ethernet MAC address */
	memcpy(mac->mac_addr, hwaddr->sa_data, ndev->addr_len);
	hal_mac_addr_set(mac);

	return 0;
}

static int ethernet_do_ioctl(struct net_device *ndev, struct ifreq *ifr, int cmd)
{
	struct sp_mac *mac = netdev_priv(ndev);

	switch (cmd) {
	case SIOCGMIIPHY:
	case SIOCGMIIREG:
	case SIOCSMIIREG:
		return phy_mii_ioctl(mac->phy_dev, ifr, cmd);
	}

	return -EOPNOTSUPP;
}

static void ethernet_tx_timeout(struct net_device *ndev, unsigned int txqueue)
{
}

static struct net_device_stats *ethernet_get_stats(struct net_device *ndev)
{
	struct sp_mac *mac;

	mac = netdev_priv(ndev);
	return &mac->dev_stats;
}

static const struct net_device_ops netdev_ops = {
	.ndo_open = ethernet_open,
	.ndo_stop = ethernet_stop,
	.ndo_start_xmit = ethernet_start_xmit,
	.ndo_set_rx_mode = ethernet_set_rx_mode,
	.ndo_set_mac_address = ethernet_set_mac_address,
	.ndo_do_ioctl = ethernet_do_ioctl,
	.ndo_tx_timeout = ethernet_tx_timeout,
	.ndo_get_stats = ethernet_get_stats,
};

char *sp7021_otp_read_mac(struct device *dev, ssize_t *len, char *name)
{
	char *ret = NULL;
	struct nvmem_cell *cell = nvmem_cell_get(dev, name);

	if (IS_ERR_OR_NULL(cell)) {
		dev_err(dev, "OTP %s read failure: %ld", name, PTR_ERR(cell));
		return NULL;
	}

	ret = nvmem_cell_read(cell, len);
	nvmem_cell_put(cell);
	dev_dbg(dev, "%zd bytes are read from OTP %s.", *len, name);

	return ret;
}

static void check_mac_vendor_id_and_convert(char *mac_addr)
{
	// Byte order of MAC address of some samples are reversed.
	// Check vendor id and convert byte order if it is wrong.
	if ((mac_addr[5] == 0xFC) && (mac_addr[4] == 0x4B) && (mac_addr[3] == 0xBC) &&
	    ((mac_addr[0] != 0xFC) || (mac_addr[1] != 0x4B) || (mac_addr[2] != 0xBC))) {
		char tmp;

		// Swap mac_addr[0] and mac_addr[5]
		tmp = mac_addr[0];
		mac_addr[0] = mac_addr[5];
		mac_addr[5] = tmp;

		// Swap mac_addr[1] and mac_addr[4]
		tmp = mac_addr[1];
		mac_addr[1] = mac_addr[4];
		mac_addr[4] = tmp;

		// Swap mac_addr[2] and mac_addr[3]
		tmp = mac_addr[2];
		mac_addr[2] = mac_addr[3];
		mac_addr[3] = tmp;
	}
}

/*********************************************************************
 *
 * platform_driver
 *
 **********************************************************************/
static u32 init_netdev(struct platform_device *pdev, int eth_no, struct net_device **r_ndev)
{
	struct sp_mac *mac;
	struct net_device *ndev;
	char *m_addr_name = (eth_no == 0) ? "mac_addr0" : "mac_addr1";
	ssize_t otp_l = 0;
	char *otp_v;
	int ret;

	// Allocate the devices, and also allocate sp_mac, we can get it by netdev_priv().
	ndev = alloc_etherdev(sizeof(*mac));
	if (!ndev) {
		*r_ndev = NULL;
		return -ENOMEM;
	}
	SET_NETDEV_DEV(ndev, &pdev->dev);
	ndev->netdev_ops = &netdev_ops;

	mac = netdev_priv(ndev);
	mac->ndev = ndev;
	mac->next_ndev = NULL;

	// Get property 'mac-addr0' or 'mac-addr1' from dts.
	otp_v = sp7021_otp_read_mac(&pdev->dev, &otp_l, m_addr_name);
	if ((otp_l < 6) || IS_ERR_OR_NULL(otp_v)) {
		dev_info(&pdev->dev, "OTP mac %s (len = %zd) is invalid, using default!\n",
			 m_addr_name, otp_l);
		otp_l = 0;
	} else {
		// Check if mac-address is valid or not. If not, copy from default.
		memcpy(mac->mac_addr, otp_v, 6);

		// Byte order of Some samples are reversed. Convert byte order here.
		check_mac_vendor_id_and_convert(mac->mac_addr);

		if (!is_valid_ether_addr(mac->mac_addr)) {
			dev_info(&pdev->dev, "Invalid mac in OTP[%s] = %pM, use default!\n",
				 m_addr_name, mac->mac_addr);
			otp_l = 0;
		}
	}
	if (otp_l != 6) {
		memcpy(mac->mac_addr, def_mac_addr, ETHERNET_MAC_ADDR_LEN);
		mac->mac_addr[5] += eth_no;
	}

	dev_info(&pdev->dev, "HW Addr = %pM\n", mac->mac_addr);

	memcpy(ndev->dev_addr, mac->mac_addr, ETHERNET_MAC_ADDR_LEN);

	ret = register_netdev(ndev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register net device \"%s\"!\n",
			ndev->name);
		free_netdev(ndev);
		*r_ndev = NULL;
		return ret;
	}
	netdev_info(ndev, "Registered net device \"%s\" successfully.\n", ndev->name);

	*r_ndev = ndev;
	return 0;
}

static int soc0_open(struct sp_mac *mac)
{
	struct sp_common *comm = mac->comm;
	u32 ret;

	hal_mac_stop(mac);

	ret = descs_init(comm);
	if (ret) {
		netdev_err(mac->ndev, "Fail to initialize mac descriptors!\n");
		descs_free(comm);
		return ret;
	}

	mac_init(mac);
	return 0;
}

static int soc0_stop(struct sp_mac *mac)
{
	hal_mac_stop(mac);

	descs_free(mac->comm);
	return 0;
}

static int sp_probe(struct platform_device *pdev)
{
	struct sp_common *comm;
	struct resource *rc;
	struct net_device *ndev, *ndev2;
	struct device_node *np;
	struct sp_mac *mac, *mac2;
	int ret;

	if (platform_get_drvdata(pdev))
		return -ENODEV;

	// Allocate memory for 'sp_common' area.
	comm = devm_kzalloc(&pdev->dev, sizeof(*comm), GFP_KERNEL);
	if (!comm)
		return -ENOMEM;
	comm->pdev = pdev;

	spin_lock_init(&comm->rx_lock);
	spin_lock_init(&comm->tx_lock);
	spin_lock_init(&comm->ioctl_lock);

	// Get memory resoruce "emac" from dts.
	rc = platform_get_resource_byname(pdev, IORESOURCE_MEM, "emac");
	if (!rc) {
		dev_err(&pdev->dev, "No MEM resource \'emac\' found!\n");
		return -ENXIO;
	}
	dev_dbg(&pdev->dev, "name = \"%s\", start = %pa\n", rc->name, &rc->start);

	comm->sp_reg_base = devm_ioremap_resource(&pdev->dev, rc);
	if (IS_ERR(comm->sp_reg_base)) {
		dev_err(&pdev->dev, "ioremap failed!\n");
		return -ENOMEM;
	}

	// Get memory resoruce "moon5" from dts.
	rc = platform_get_resource_byname(pdev, IORESOURCE_MEM, "moon5");
	if (!rc) {
		dev_err(&pdev->dev, "No MEM resource \'moon5\' found!\n");
		return -ENXIO;
	}
	dev_dbg(&pdev->dev, "name = \"%s\", start = %pa\n", rc->name, &rc->start);

	// Note that moon5 is shared resource. Don't use devm_ioremap_resource().
	comm->moon5_reg_base = devm_ioremap(&pdev->dev, rc->start, rc->end - rc->start + 1);
	if (IS_ERR(comm->moon5_reg_base)) {
		dev_err(&pdev->dev, "ioremap failed!\n");
		return -ENOMEM;
	}

	// Get irq resource from dts.
	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		return ret;
	comm->irq = ret;

	// Get clock controller.
	comm->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(comm->clk)) {
		dev_err_probe(&pdev->dev, PTR_ERR(comm->clk),
			      "Failed to retrieve clock controller!\n");
		return PTR_ERR(comm->clk);
	}

	// Get reset controller.
	comm->rstc = devm_reset_control_get_exclusive(&pdev->dev, NULL);
	if (IS_ERR(comm->rstc)) {
		dev_err_probe(&pdev->dev, PTR_ERR(comm->rstc),
			      "Failed to retrieve reset controller!\n");
		return PTR_ERR(comm->rstc);
	}

	// Enable clock.
	clk_prepare_enable(comm->clk);
	udelay(1);

	reset_control_assert(comm->rstc);
	udelay(1);
	reset_control_deassert(comm->rstc);
	udelay(1);

	// Initialize the 1st net device.
	ret = init_netdev(pdev, 0, &ndev);
	if (!ndev)
		return ret;

	platform_set_drvdata(pdev, ndev);

	ndev->irq = comm->irq;
	mac = netdev_priv(ndev);
	mac->comm = comm;
	comm->ndev = ndev;

	// Get node of phy 1.
	mac->phy_node = of_parse_phandle(pdev->dev.of_node, "phy-handle1", 0);
	if (!mac->phy_node) {
		netdev_info(ndev, "Cannot get node of phy 1!\n");
		ret = -ENODEV;
		goto out_unregister_dev;
	}

	// Get address of phy from dts.
	if (of_property_read_u32(mac->phy_node, "reg", &mac->phy_addr)) {
		mac->phy_addr = 0;
		netdev_info(ndev, "Cannot get address of phy 1! Set to 0.\n");
	}

	// Get mode of phy from dts.
	if (of_get_phy_mode(mac->phy_node, &mac->phy_mode)) {
		mac->phy_mode = PHY_INTERFACE_MODE_RGMII_ID;
		netdev_info(ndev, "Missing phy-mode of phy 1! Set to \'rgmii-id\'.\n");
	}

	// Request irq.
	ret = devm_request_irq(&pdev->dev, comm->irq, ethernet_interrupt, 0,
			       ndev->name, ndev);
	if (ret) {
		netdev_err(ndev, "Failed to request irq #%d for \"%s\"!\n",
			   ndev->irq, ndev->name);
		goto out_unregister_dev;
	}

	mac->cpu_port = 0x1;	// soc0
	mac->lan_port = 0x1;	// forward to port 0
	mac->to_vlan = 0x1;	// vlan group: 0
	mac->vlan_id = 0x0;	// vlan group: 0

	// Set MAC address
	hal_mac_addr_set(mac);
	hal_rx_mode_set(ndev);
	hal_mac_addr_table_del_all(mac);

	ndev2 = NULL;
	np = of_parse_phandle(pdev->dev.of_node, "phy-handle2", 0);
	if (np) {
		init_netdev(pdev, 1, &ndev2);
		if (ndev2) {
			mac->next_ndev = ndev2; // Point to the second net device.

			ndev2->irq = comm->irq;
			mac2 = netdev_priv(ndev2);
			mac2->comm = comm;
			mac2->phy_node = np;

			if (of_property_read_u32(mac2->phy_node, "reg", &mac2->phy_addr)) {
				mac2->phy_addr = 1;
				netdev_info(ndev2, "Cannot get address of phy 2! Set to 1.\n");
			}

			if (of_get_phy_mode(mac2->phy_node, &mac2->phy_mode)) {
				mac2->phy_mode = PHY_INTERFACE_MODE_RGMII_ID;
				netdev_info(ndev, "Missing phy-mode phy 2! Set to \'rgmii-id\'.\n");
			}

			mac2->cpu_port = 0x1;	// soc0
			mac2->lan_port = 0x2;	// forward to port 1
			mac2->to_vlan = 0x2;	// vlan group: 1
			mac2->vlan_id = 0x1;	// vlan group: 1

			hal_mac_addr_set(mac2);	// Set MAC address for the 2nd net device.
			hal_rx_mode_set(ndev2);
		}
	}

	soc0_open(mac);
	hal_set_rmii_tx_rx_pol(mac);
	hal_phy_addr(mac);

	ret = mdio_init(pdev, ndev);
	if (ret) {
		netdev_err(ndev, "Failed to initialize mdio!\n");
		goto out_unregister_dev;
	}

	ret = sp_phy_probe(ndev);
	if (ret) {
		netdev_err(ndev, "Failed to probe phy!\n");
		goto out_freemdio;
	}

	if (ndev2) {
		ret = sp_phy_probe(ndev2);
		if (ret) {
			netdev_err(ndev2, "Failed to probe phy!\n");
			unregister_netdev(ndev2);
			mac->next_ndev = 0;
		}
	}

	netif_napi_add(ndev, &comm->rx_napi, rx_poll, RX_NAPI_WEIGHT);
	napi_enable(&comm->rx_napi);
	netif_napi_add(ndev, &comm->tx_napi, tx_poll, TX_NAPI_WEIGHT);
	napi_enable(&comm->tx_napi);
	return 0;

out_freemdio:
	if (comm->mii_bus)
		mdio_remove(ndev);

out_unregister_dev:
	unregister_netdev(ndev);
	if (ndev2)
		unregister_netdev(ndev2);

	return ret;
}

static int sp_remove(struct platform_device *pdev)
{
	struct net_device *ndev, *ndev2;
	struct sp_mac *mac;

	ndev = platform_get_drvdata(pdev);
	if (!ndev)
		return 0;

	mac = netdev_priv(ndev);

	// Unregister and free 2nd net device.
	ndev2 = mac->next_ndev;
	if (ndev2) {
		sp_phy_remove(ndev2);
		unregister_netdev(ndev2);
		free_netdev(ndev2);
	}

	mac->comm->enable = 0;
	soc0_stop(mac);

	// Disable and delete napi.
	napi_disable(&mac->comm->rx_napi);
	netif_napi_del(&mac->comm->rx_napi);
	napi_disable(&mac->comm->tx_napi);
	netif_napi_del(&mac->comm->tx_napi);

	sp_phy_remove(ndev);
	mdio_remove(ndev);

	// Unregister and free 1st net device.
	unregister_netdev(ndev);
	free_netdev(ndev);

	clk_disable(mac->comm->clk);

	return 0;
}

static const struct of_device_id sp_of_match[] = {
	{.compatible = "sunplus,sp7021-emac"},
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, sp_of_match);

static struct platform_driver sp_driver = {
	.probe = sp_probe,
	.remove = sp_remove,
	.driver = {
		.name = "sp7021_emac",
		.owner = THIS_MODULE,
		.of_match_table = sp_of_match,
	},
};

module_platform_driver(sp_driver);

MODULE_AUTHOR("Wells Lu <wells.lu@sunplus.com>");
MODULE_DESCRIPTION("Sunplus Dual 10M/100M Ethernet driver");
MODULE_LICENSE("GPL v2");
