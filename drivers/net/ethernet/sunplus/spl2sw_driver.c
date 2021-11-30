// SPDX-License-Identifier: GPL-2.0
/* Copyright Sunplus Technology Co., Ltd.
 *       All rights reserved.
 */

#include <linux/nvmem-consumer.h>
#include <linux/of_net.h>
#include <linux/random.h>
#include <linux/reset.h>
#include <linux/clk.h>

#include "spl2sw_register.h"
#include "spl2sw_driver.h"
#include "spl2sw_phy.h"

/* OUI of Sunplus Technology Co., Ltd. */
static const char spl2sw_def_mac_addr[ETH_ALEN] = {
	0xfc, 0x4b, 0xbc, 0x00, 0x00, 0x00
};

/* net device operations */
static int spl2sw_ethernet_open(struct net_device *ndev)
{
	struct spl2sw_mac *mac = netdev_priv(ndev);
	struct spl2sw_common *comm = mac->comm;
	u32 mask;

	netdev_dbg(ndev, "Open port = %x\n", mac->lan_port);

	comm->enable |= mac->lan_port;

	spl2sw_mac_hw_start(comm);

	/* Enable TX and RX interrupts */
	mask = readl(comm->l2sw_reg_base + L2SW_SW_INT_MASK_0);
	mask &= ~(MAC_INT_TX | MAC_INT_RX);
	writel(mask, comm->l2sw_reg_base + L2SW_SW_INT_MASK_0);

	phy_start(ndev->phydev);

	netif_start_queue(ndev);

	return 0;
}

static int spl2sw_ethernet_stop(struct net_device *ndev)
{
	struct spl2sw_mac *mac = netdev_priv(ndev);
	struct spl2sw_common *comm = mac->comm;

	netif_stop_queue(ndev);

	comm->enable &= ~mac->lan_port;

	phy_stop(ndev->phydev);

	spl2sw_mac_hw_stop(comm);

	return 0;
}

static int spl2sw_ethernet_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct spl2sw_mac *mac = netdev_priv(ndev);
	struct spl2sw_common *comm = mac->comm;
	struct spl2sw_skb_info *skbinfo;
	struct spl2sw_mac_desc *txdesc;
	unsigned long flags;
	u32 tx_pos;
	u32 cmd1;
	u32 cmd2;

	if (unlikely(comm->tx_desc_full == 1)) {
		/* No TX descriptors left. Wait for tx interrupt. */
		netdev_dbg(ndev, "TX descriptor queue full when xmit!\n");
		return NETDEV_TX_BUSY;
	}

	/* if skb size shorter than 60, fill it with '\0' */
	if (unlikely(skb->len < ETH_ZLEN)) {
		if (skb_tailroom(skb) >= (ETH_ZLEN - skb->len)) {
			memset(__skb_put(skb, ETH_ZLEN - skb->len), '\0',
			       ETH_ZLEN - skb->len);
		} else {
			struct sk_buff *old_skb = skb;

			skb = dev_alloc_skb(ETH_ZLEN);
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
	if (dma_mapping_error(&comm->pdev->dev, skbinfo->mapping)) {
		ndev->stats.tx_errors++;
		skbinfo->mapping = 0;
		dev_kfree_skb_irq(skb);
		skbinfo->skb = NULL;
		goto xmit_drop;
	}

	/* Set up a TX descriptor */
	cmd1 = TXD_OWN | TXD_SOP | TXD_EOP | (mac->to_vlan << 12) |
	       (skb->len & TXD_PKT_LEN);
	cmd2 = skb->len & TXD_BUF_LEN1;

	if (tx_pos == (TX_DESC_NUM - 1))
		cmd2 |= TXD_EOR;

	txdesc->addr1 = skbinfo->mapping;
	txdesc->cmd2 = cmd2;
	wmb();	/* Set TXD_OWN after other fields are effective. */
	txdesc->cmd1 = cmd1;

	/* Move tx_pos to next position */
	tx_pos = ((tx_pos + 1) == TX_DESC_NUM) ? 0 : tx_pos + 1;

	if (unlikely(tx_pos == comm->tx_done_pos)) {
		netif_stop_queue(ndev);
		comm->tx_desc_full = 1;
	}
	comm->tx_pos = tx_pos;
	wmb();		/* make sure settings are effective. */

	/* trigger gmac to transmit */
	writel(MAC_TRIG_L_SOC0, comm->l2sw_reg_base + L2SW_CPU_TX_TRIG);

xmit_drop:
	spin_unlock_irqrestore(&comm->tx_lock, flags);
	return NETDEV_TX_OK;
}

static void spl2sw_ethernet_set_rx_mode(struct net_device *ndev)
{
	struct spl2sw_mac *mac = netdev_priv(ndev);

	spl2sw_mac_rx_mode_set(mac);
}

static int spl2sw_ethernet_set_mac_address(struct net_device *ndev, void *addr)
{
	struct spl2sw_mac *mac = netdev_priv(ndev);
	int err;

	err = eth_mac_addr(ndev, addr);
	if (err)
		return err;

	/* Delete the old MAC address */
	netdev_dbg(ndev, "HW Addr = %pM\n", mac->mac_addr);
	if (is_valid_ether_addr(mac->mac_addr))
		spl2sw_mac_addr_del(mac);

	/* Set the MAC address */
	memcpy(mac->mac_addr, ndev->dev_addr, ndev->addr_len);
	spl2sw_mac_addr_add(mac);

	return 0;
}

static void spl2sw_ethernet_tx_timeout(struct net_device *ndev, unsigned int txqueue)
{
	struct spl2sw_mac *mac = netdev_priv(ndev);
	struct spl2sw_common *comm = mac->comm;
	unsigned long flags;
	int i;

	netdev_err(ndev, "TX timed out!\n");
	ndev->stats.tx_errors++;

	spin_lock_irqsave(&comm->tx_lock, flags);

	for (i = 0; i < MAX_NETDEV_NUM; i++)
		if (comm->ndev[i])
			netif_stop_queue(comm->ndev[i]);

	spl2sw_mac_soft_reset(comm);

	/* Accept TX packets again. */
	for (i = 0; i < MAX_NETDEV_NUM; i++)
		if (comm->ndev[i]) {
			netif_trans_update(comm->ndev[i]);
			netif_wake_queue(comm->ndev[i]);
		}

	spin_unlock_irqrestore(&comm->tx_lock, flags);
}

static const struct net_device_ops netdev_ops = {
	.ndo_open = spl2sw_ethernet_open,
	.ndo_stop = spl2sw_ethernet_stop,
	.ndo_start_xmit = spl2sw_ethernet_start_xmit,
	.ndo_set_rx_mode = spl2sw_ethernet_set_rx_mode,
	.ndo_set_mac_address = spl2sw_ethernet_set_mac_address,
	.ndo_do_ioctl = phy_do_ioctl,
	.ndo_tx_timeout = spl2sw_ethernet_tx_timeout,
};

static char *spl2sw_otp_read_mac(struct device *dev, ssize_t *len, char *name)
{
	struct nvmem_cell *cell = nvmem_cell_get(dev, name);
	char *ret;

	if (IS_ERR_OR_NULL(cell)) {
		dev_err(dev, "OTP %s read failure: %ld", name, PTR_ERR(cell));
		return NULL;
	}

	ret = nvmem_cell_read(cell, len);
	nvmem_cell_put(cell);
	dev_dbg(dev, "%zd bytes are read from OTP %s.", *len, name);

	return ret;
}

static void spl2sw_check_mac_vendor_id_and_convert(char *mac_addr)
{
	/* Byte order of MAC address of some samples are reversed.
	 * Check vendor id and convert byte order if it is wrong.
	 */
	if (mac_addr[5] == 0xFC && mac_addr[4] == 0x4B && mac_addr[3] == 0xBC &&
	    (mac_addr[0] != 0xFC || mac_addr[1] != 0x4B || mac_addr[2] != 0xBC)) {
		char tmp;

		/* Swap mac_addr[0] and mac_addr[5] */
		tmp = mac_addr[0];
		mac_addr[0] = mac_addr[5];
		mac_addr[5] = tmp;

		/* Swap mac_addr[1] and mac_addr[4] */
		tmp = mac_addr[1];
		mac_addr[1] = mac_addr[4];
		mac_addr[4] = tmp;

		/* Swap mac_addr[2] and mac_addr[3] */
		tmp = mac_addr[2];
		mac_addr[2] = mac_addr[3];
		mac_addr[3] = tmp;
	}
}

static u32 spl2sw_init_netdev(struct platform_device *pdev, int eth_no,
			      struct net_device **r_ndev)
{
	struct net_device *ndev;
	struct spl2sw_mac *mac;
	char *m_addr_name;
	ssize_t otp_l = 0;
	char *otp_v;
	int ret;

	m_addr_name = (eth_no == 0) ? "mac_addr0" : "mac_addr1";

	/* Allocate the devices, and also allocate spl2sw_mac,
	 * we can get it by netdev_priv().
	 */
	ndev = devm_alloc_etherdev(&pdev->dev, sizeof(*mac));
	if (!ndev) {
		*r_ndev = NULL;
		return -ENOMEM;
	}
	SET_NETDEV_DEV(ndev, &pdev->dev);
	ndev->netdev_ops = &netdev_ops;

	mac = netdev_priv(ndev);
	mac->ndev = ndev;

	/* Get property 'mac-addr0' or 'mac-addr1' from dts. */
	otp_v = spl2sw_otp_read_mac(&pdev->dev, &otp_l, m_addr_name);
	if (otp_l < ETH_ALEN || IS_ERR_OR_NULL(otp_v)) {
		dev_err(&pdev->dev, "OTP mac %s (len = %zd) is invalid, using default!\n",
			m_addr_name, otp_l);
		otp_l = 0;
	} else {
		/* Check if MAC address is valid or not. If not, copy from default. */
		ether_addr_copy(mac->mac_addr, otp_v);

		/* Byte order of Some samples are reversed. Convert byte order here. */
		spl2sw_check_mac_vendor_id_and_convert(mac->mac_addr);

		if (!is_valid_ether_addr(mac->mac_addr)) {
			dev_err(&pdev->dev, "Invalid mac in OTP[%s] = %pM, use default!\n",
				m_addr_name, mac->mac_addr);
			otp_l = 0;
		}
	}
	if (otp_l != 6) {
		/* MAC address is invalid. Generate one using random number. */
		ether_addr_copy(mac->mac_addr, spl2sw_def_mac_addr);
		mac->mac_addr[3] = get_random_int() % 256;
		mac->mac_addr[4] = get_random_int() % 256;
		mac->mac_addr[5] = get_random_int() % 256;
	}

	eth_hw_addr_set(ndev, mac->mac_addr);
	dev_info(&pdev->dev, "HW Addr = %pM\n", mac->mac_addr);

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

static struct device_node *spl2sw_get_eth_child_node(struct device_node *eth_node, int id)
{
	struct device_node *port_np;
	int port_id;

	for_each_child_of_node(eth_node, port_np) {
		/* It is not a 'port' node, continue. */
		if (strcmp(port_np->name, "port"))
			continue;

		if (of_property_read_u32(port_np, "reg", &port_id) < 0)
			continue;

		if (port_id == id)
			return port_np;
	}

	/* Not found! */
	return NULL;
}

static int spl2sw_probe(struct platform_device *pdev)
{
	struct device_node *eth_ports_np;
	struct device_node *port_np;
	struct spl2sw_common *comm;
	struct device_node *phy_np;
	phy_interface_t phy_mode;
	struct net_device *ndev;
	struct spl2sw_mac *mac;
	struct resource *rc;
	int irq, i;
	int ret;

	if (platform_get_drvdata(pdev))
		return -ENODEV;

	/* Allocate memory for 'spl2sw_common' area. */
	comm = devm_kzalloc(&pdev->dev, sizeof(*comm), GFP_KERNEL);
	if (!comm)
		return -ENOMEM;
	comm->pdev = pdev;

	spin_lock_init(&comm->rx_lock);
	spin_lock_init(&comm->tx_lock);
	spin_lock_init(&comm->mdio_lock);

	/* Get memory resoruce "emac" from dts. */
	rc = platform_get_resource_byname(pdev, IORESOURCE_MEM, "emac");
	if (!rc) {
		dev_err(&pdev->dev, "No MEM resource \'emac\' found!\n");
		return -ENXIO;
	}
	dev_dbg(&pdev->dev, "name = \"%s\", start = %pa\n", rc->name, &rc->start);

	comm->l2sw_reg_base = devm_ioremap_resource(&pdev->dev, rc);
	if (IS_ERR(comm->l2sw_reg_base)) {
		dev_err(&pdev->dev, "ioremap failed!\n");
		return -ENOMEM;
	}

	/* Get memory resoruce "moon5" from dts. */
	rc = platform_get_resource_byname(pdev, IORESOURCE_MEM, "moon5");
	if (!rc) {
		dev_err(&pdev->dev, "No MEM resource \'moon5\' found!\n");
		return -ENXIO;
	}
	dev_dbg(&pdev->dev, "name = \"%s\", start = %pa\n", rc->name, &rc->start);

	/* Note that moon5 is shared resource.
	 * Don't use devm_ioremap_resource().
	 */
	comm->moon5_reg_base = devm_ioremap(&pdev->dev, rc->start, rc->end - rc->start + 1);
	if (!comm->moon5_reg_base) {
		dev_err(&pdev->dev, "ioremap failed!\n");
		return -ENOMEM;
	}

	/* Get irq resource from dts. */
	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		return ret;
	irq = ret;

	/* Get clock controller. */
	comm->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(comm->clk)) {
		dev_err_probe(&pdev->dev, PTR_ERR(comm->clk),
			      "Failed to retrieve clock controller!\n");
		return PTR_ERR(comm->clk);
	}

	/* Get reset controller. */
	comm->rstc = devm_reset_control_get_exclusive(&pdev->dev, NULL);
	if (IS_ERR(comm->rstc)) {
		dev_err_probe(&pdev->dev, PTR_ERR(comm->rstc),
			      "Failed to retrieve reset controller!\n");
		return PTR_ERR(comm->rstc);
	}

	/* Enable clock. */
	clk_prepare_enable(comm->clk);
	udelay(1);

	reset_control_assert(comm->rstc);
	udelay(1);
	reset_control_deassert(comm->rstc);
	udelay(1);

	/* Get child node ethernet-ports. */
	eth_ports_np = of_get_child_by_name(pdev->dev.of_node, "ethernet-ports");
	if (!eth_ports_np) {
		dev_err(&pdev->dev, "No ethernet-ports child node found!\n");
		return -ENODEV;
	}

	for (i = 0; i < MAX_NETDEV_NUM; i++) {
		/* Get port@i of node ethernet-ports. */
		port_np = spl2sw_get_eth_child_node(eth_ports_np, i);
		if (!port_np)
			continue;

		/* Get phy-mode. */
		if (of_get_phy_mode(port_np, &phy_mode)) {
			dev_err(&pdev->dev, "Failed to get phy-mode property of port@%d!\n",
				i);
			continue;
		}

		/* Get phy-handle. */
		phy_np = of_parse_phandle(port_np, "phy-handle", 0);
		if (!phy_np) {
			dev_err(&pdev->dev, "Failed to get phy-handle property of port@%d!\n",
				i);
			continue;
		}

		/* Get address of phy. */
		if (of_property_read_u32(phy_np, "reg", &comm->phy_addr[i])) {
			dev_err(&pdev->dev, "Failed to get reg property of phy node!\n");
			continue;
		}

		if (comm->phy_addr[i] >= PHY_MAX_ADDR - 1) {
			dev_err(&pdev->dev, "Invalid phy address (reg = <%d>)!\n",
				comm->phy_addr[i]);
			continue;
		}

		if (!comm->mdio_node) {
			comm->mdio_node = of_get_parent(phy_np);
			if (!comm->mdio_node) {
				dev_err(&pdev->dev, "Failed to get mdio_node!\n");
				return -ENODATA;
			}
		}

		/* Initialize the net device. */
		ret = spl2sw_init_netdev(pdev, i, &ndev);
		if (ret)
			goto out_unregister_dev;

		ndev->irq = irq;
		comm->ndev[i] = ndev;
		mac = netdev_priv(ndev);
		mac->phy_node = phy_np;
		mac->phy_mode = phy_mode;
		mac->comm = comm;

		mac->lan_port = 0x1 << i;	/* forward to port i */
		mac->to_vlan = 0x1 << i;	/* vlan group: i     */
		mac->vlan_id = i;		/* vlan group: i     */

		/* Set MAC address */
		spl2sw_mac_addr_add(mac);
		spl2sw_mac_rx_mode_set(mac);
	}

	/* Find first valid net device. */
	for (i = 0; i < MAX_NETDEV_NUM; i++) {
		if (comm->ndev[i])
			break;
	}
	if (i >= MAX_NETDEV_NUM) {
		dev_err(&pdev->dev, "No valid ethernet port!\n");
		return -ENODEV;
	}

	/* Save first valid net device */
	ndev = comm->ndev[i];
	platform_set_drvdata(pdev, ndev);

	/* Request irq. */
	ret = devm_request_irq(&pdev->dev, irq, spl2sw_ethernet_interrupt,
			       0, ndev->name, ndev);
	if (ret) {
		netdev_err(ndev, "Failed to request irq #%d for \"%s\"!\n",
			   irq, ndev->name);
		goto out_unregister_dev;
	}

	/* Initialize mdio bus */
	ret = spl2sw_mdio_init(comm);
	if (ret) {
		netdev_err(ndev, "Failed to initialize mdio!\n");
		goto out_unregister_dev;
	}

	spl2sw_mac_addr_del_all(comm);

	ret = spl2sw_descs_init(comm);
	if (ret) {
		dev_err(&comm->pdev->dev, "Fail to initialize mac descriptors!\n");
		spl2sw_descs_free(comm);
		goto out_free_mdio;
	}

	spl2sw_mac_init(comm);

	ret = spl2sw_phy_connect(comm);
	if (ret) {
		netdev_err(ndev, "Failed to connect phy!\n");
		goto out_free_mdio;
	}

	netif_napi_add(ndev, &comm->rx_napi, spl2sw_rx_poll, SPL2SW_RX_NAPI_WEIGHT);
	napi_enable(&comm->rx_napi);
	netif_napi_add(ndev, &comm->tx_napi, spl2sw_tx_poll, SPL2SW_TX_NAPI_WEIGHT);
	napi_enable(&comm->tx_napi);
	return 0;

out_free_mdio:
	spl2sw_mdio_remove(comm);

out_unregister_dev:
	for (i = 0; i < MAX_NETDEV_NUM; i++)
		if (comm->ndev[i])
			unregister_netdev(comm->ndev[i]);

	return ret;
}

static int spl2sw_remove(struct platform_device *pdev)
{
	struct spl2sw_common *comm;
	struct net_device *ndev;
	struct spl2sw_mac *mac;
	int i;

	ndev = platform_get_drvdata(pdev);
	if (!ndev)
		return 0;

	mac = netdev_priv(ndev);
	comm = mac->comm;

	spl2sw_phy_remove(comm);

	/* Unregister and free net device. */
	for (i = 0; i < MAX_NETDEV_NUM; i++)
		if (comm->ndev[i])
			unregister_netdev(comm->ndev[i]);

	comm->enable = 0;
	spl2sw_mac_hw_stop(comm);
	spl2sw_descs_free(comm);

	/* Disable and delete napi. */
	napi_disable(&comm->rx_napi);
	netif_napi_del(&comm->rx_napi);
	napi_disable(&comm->tx_napi);
	netif_napi_del(&comm->tx_napi);

	spl2sw_mdio_remove(comm);

	clk_disable(comm->clk);

	return 0;
}

static const struct of_device_id spl2sw_of_match[] = {
	{.compatible = "sunplus,sp7021-emac"},
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, spl2sw_of_match);

static struct platform_driver spl2sw_driver = {
	.probe = spl2sw_probe,
	.remove = spl2sw_remove,
	.driver = {
		.name = "sp7021_emac",
		.owner = THIS_MODULE,
		.of_match_table = spl2sw_of_match,
	},
};

module_platform_driver(spl2sw_driver);

MODULE_AUTHOR("Wells Lu <wellslutw@gmail.com>");
MODULE_DESCRIPTION("Sunplus Dual 10M/100M Ethernet driver");
MODULE_LICENSE("GPL v2");
