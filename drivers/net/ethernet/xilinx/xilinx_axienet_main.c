// SPDX-License-Identifier: GPL-2.0-only
/*
 * Xilinx Axi Ethernet device driver
 *
 * Copyright (c) 2008 Nissin Systems Co., Ltd.,  Yoshio Kashiwagi
 * Copyright (c) 2005-2008 DLA Systems,  David H. Lynch Jr. <dhlii@dlasys.net>
 * Copyright (c) 2008-2009 Secret Lab Technologies Ltd.
 * Copyright (c) 2010 - 2011 Michal Simek <monstr@monstr.eu>
 * Copyright (c) 2010 - 2011 PetaLogix
 * Copyright (c) 2019 SED Systems, a division of Calian Ltd.
 * Copyright (c) 2010 - 2012 Xilinx, Inc. All rights reserved.
 *
 * This is a driver for the Xilinx Axi Ethernet which is used in the Virtex6
 * and Spartan6.
 *
 * TODO:
 *  - Add Axi Fifo support.
 *  - Test and fix basic multicast filtering.
 *  - Add support for extended multicast filtering.
 *  - Test basic VLAN support.
 *  - Add support for extended VLAN support.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/phy.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>

#include "xilinx_axienet.h"

/* Descriptors defines for Tx and Rx DMA */
#define TX_BD_NUM_DEFAULT		64
#define RX_BD_NUM_DEFAULT		128
#define TX_BD_NUM_MAX			4096
#define RX_BD_NUM_MAX			4096
#define DMA_NUM_APP_WORDS		5

/* Must be shorter than length of ethtool_drvinfo.driver field to fit */
#define DRIVER_NAME		"xaxienet"
#define DRIVER_DESCRIPTION	"Xilinx Axi Ethernet driver"
#define DRIVER_VERSION		"1.00a"

#define AXIENET_REGS_N		40

/* Match table for of_platform binding */
static const struct of_device_id axienet_of_match[] = {
	{ .compatible = "xlnx,axi-ethernet-1.00.a", },
	{ .compatible = "xlnx,axi-ethernet-1.01.a", },
	{ .compatible = "xlnx,axi-ethernet-2.01.a", },
	{},
};

MODULE_DEVICE_TABLE(of, axienet_of_match);

/* Option table for setting up Axi Ethernet hardware options */
static struct axienet_option axienet_options[] = {
	/* Turn on jumbo packet support for both Rx and Tx */
	{
		.opt = XAE_OPTION_JUMBO,
		.reg = XAE_TC_OFFSET,
		.m_or = XAE_TC_JUM_MASK,
	}, {
		.opt = XAE_OPTION_JUMBO,
		.reg = XAE_RCW1_OFFSET,
		.m_or = XAE_RCW1_JUM_MASK,
	}, { /* Turn on VLAN packet support for both Rx and Tx */
		.opt = XAE_OPTION_VLAN,
		.reg = XAE_TC_OFFSET,
		.m_or = XAE_TC_VLAN_MASK,
	}, {
		.opt = XAE_OPTION_VLAN,
		.reg = XAE_RCW1_OFFSET,
		.m_or = XAE_RCW1_VLAN_MASK,
	}, { /* Turn on FCS stripping on receive packets */
		.opt = XAE_OPTION_FCS_STRIP,
		.reg = XAE_RCW1_OFFSET,
		.m_or = XAE_RCW1_FCS_MASK,
	}, { /* Turn on FCS insertion on transmit packets */
		.opt = XAE_OPTION_FCS_INSERT,
		.reg = XAE_TC_OFFSET,
		.m_or = XAE_TC_FCS_MASK,
	}, { /* Turn off length/type field checking on receive packets */
		.opt = XAE_OPTION_LENTYPE_ERR,
		.reg = XAE_RCW1_OFFSET,
		.m_or = XAE_RCW1_LT_DIS_MASK,
	}, { /* Turn on Rx flow control */
		.opt = XAE_OPTION_FLOW_CONTROL,
		.reg = XAE_FCC_OFFSET,
		.m_or = XAE_FCC_FCRX_MASK,
	}, { /* Turn on Tx flow control */
		.opt = XAE_OPTION_FLOW_CONTROL,
		.reg = XAE_FCC_OFFSET,
		.m_or = XAE_FCC_FCTX_MASK,
	}, { /* Turn on promiscuous frame filtering */
		.opt = XAE_OPTION_PROMISC,
		.reg = XAE_FMI_OFFSET,
		.m_or = XAE_FMI_PM_MASK,
	}, { /* Enable transmitter */
		.opt = XAE_OPTION_TXEN,
		.reg = XAE_TC_OFFSET,
		.m_or = XAE_TC_TX_MASK,
	}, { /* Enable receiver */
		.opt = XAE_OPTION_RXEN,
		.reg = XAE_RCW1_OFFSET,
		.m_or = XAE_RCW1_RX_MASK,
	},
	{}
};

struct axi_skbuff {
	struct sk_buff *skb;
	struct scatterlist sgl[MAX_SKB_FRAGS + 1];
	dma_addr_t dma_address;
	int sg_len;
	struct dma_async_tx_descriptor *desc;
} __packed;

static void axienet_dma_rx_cb(void *data, const struct dmaengine_result *result);
static int axienet_rx_submit_desc(struct net_device *ndev)
{
	struct axienet_local *lp = netdev_priv(ndev);
	struct dma_async_tx_descriptor *dma_rx_desc = NULL;
	struct axi_skbuff *axi_skb;
	struct sk_buff *skb;
	dma_addr_t addr;
	int ret;

	axi_skb = kmem_cache_alloc(lp->skb_cache, GFP_KERNEL);
	if (!axi_skb)
		return -ENOMEM;

	skb = netdev_alloc_skb(ndev, lp->max_frm_size);
	if (!skb) {
		ret = -ENOMEM;
		goto rx_bd_init_skb;
	}

	sg_init_table(axi_skb->sgl, 1);
	addr = dma_map_single(lp->dev, skb->data, lp->max_frm_size, DMA_FROM_DEVICE);
	sg_dma_address(axi_skb->sgl) = addr;
	sg_dma_len(axi_skb->sgl) = lp->max_frm_size;

	dma_rx_desc = dmaengine_prep_slave_sg(lp->rx_chan, axi_skb->sgl,
					      1, DMA_DEV_TO_MEM,
					      DMA_PREP_INTERRUPT);
	if (!dma_rx_desc) {
		ret = -EINVAL;
		goto rx_bd_init_prep_sg;
	}

	axi_skb->skb = skb;
	axi_skb->dma_address = sg_dma_address(axi_skb->sgl);
	axi_skb->desc = dma_rx_desc;
	dma_rx_desc->callback_param =  axi_skb;
	dma_rx_desc->callback_result = axienet_dma_rx_cb;
	dmaengine_submit(dma_rx_desc);

	return 0;

rx_bd_init_prep_sg:
	dma_unmap_single(lp->dev, addr, lp->max_frm_size, DMA_FROM_DEVICE);
	dev_kfree_skb(skb);
rx_bd_init_skb:
	kmem_cache_free(lp->skb_cache, axi_skb);
	return ret;

};

static void axienet_dma_rx_cb(void *data, const struct dmaengine_result *result)
{
	struct axi_skbuff *axi_skb = data;
	struct sk_buff *skb = axi_skb->skb;
	struct net_device *netdev = skb->dev;
	struct axienet_local *lp = netdev_priv(netdev);
	u32 *app;
	size_t meta_len, meta_max_len, rx_len;

	app  = dmaengine_desc_get_metadata_ptr(axi_skb->desc, &meta_len, &meta_max_len);
	dma_unmap_single(lp->dev, axi_skb->dma_address, lp->max_frm_size,
			 DMA_FROM_DEVICE);
	/* TODO: Derive app word index programmatically */
	rx_len = (app[4] & 0xFFFF);
	skb_put(skb, rx_len);
	skb->protocol = eth_type_trans(skb, netdev);
	skb->ip_summed = CHECKSUM_NONE;

	netif_rx(skb);
	kmem_cache_free(lp->skb_cache, axi_skb);
	netdev->stats.rx_packets++;
	netdev->stats.rx_bytes += rx_len;
	axienet_rx_submit_desc(netdev);
	dma_async_issue_pending(lp->rx_chan);
}

static int axienet_setup_dma_chan(struct net_device *ndev)
{
	struct axienet_local *lp = netdev_priv(ndev);
	int i, ret;

	lp->tx_chan = dma_request_chan(lp->dev, "tx_chan0");
	if (IS_ERR(lp->tx_chan)) {
		ret = PTR_ERR(lp->tx_chan);
		if (ret != -EPROBE_DEFER)
			netdev_err(ndev, "No Ethernet DMA (TX) channel found\n");
		return ret;
	}

	lp->rx_chan = dma_request_chan(lp->dev, "rx_chan0");
	if (IS_ERR(lp->rx_chan)) {
		ret = PTR_ERR(lp->rx_chan);
		if (ret != -EPROBE_DEFER)
			netdev_err(ndev, "No Ethernet DMA (RX) channel found\n");
		goto err_dma_request_rx;
	}

	lp->skb_cache = kmem_cache_create("ethernet", sizeof(struct axi_skbuff),
					  0, 0, NULL);
	if (!lp->skb_cache) {
		ret =  -ENOMEM;
		goto err_kmem;
	}

	/* TODO: Instead of BD_NUM_DEFAULT use runtime support*/
	for (i = 0; i < RX_BD_NUM_DEFAULT; i++)
		axienet_rx_submit_desc(ndev);
	dma_async_issue_pending(lp->rx_chan);

	return 0;

err_kmem:
	dma_release_channel(lp->rx_chan);
err_dma_request_rx:
	dma_release_channel(lp->tx_chan);
	return ret;
}

/**
 * axienet_set_mac_address - Write the MAC address
 * @ndev:	Pointer to the net_device structure
 * @address:	6 byte Address to be written as MAC address
 *
 * This function is called to initialize the MAC address of the Axi Ethernet
 * core. It writes to the UAW0 and UAW1 registers of the core.
 */
static void axienet_set_mac_address(struct net_device *ndev,
				    const void *address)
{
	struct axienet_local *lp = netdev_priv(ndev);

	if (address)
		memcpy(ndev->dev_addr, address, ETH_ALEN);
	if (!is_valid_ether_addr(ndev->dev_addr))
		eth_hw_addr_random(ndev);

	/* Set up unicast MAC address filter set its mac address */
	axienet_iow(lp, XAE_UAW0_OFFSET,
		    (ndev->dev_addr[0]) |
		    (ndev->dev_addr[1] << 8) |
		    (ndev->dev_addr[2] << 16) |
		    (ndev->dev_addr[3] << 24));
	axienet_iow(lp, XAE_UAW1_OFFSET,
		    (((axienet_ior(lp, XAE_UAW1_OFFSET)) &
		      ~XAE_UAW1_UNICASTADDR_MASK) |
		     (ndev->dev_addr[4] |
		     (ndev->dev_addr[5] << 8))));
}

/**
 * netdev_set_mac_address - Write the MAC address (from outside the driver)
 * @ndev:	Pointer to the net_device structure
 * @p:		6 byte Address to be written as MAC address
 *
 * Return: 0 for all conditions. Presently, there is no failure case.
 *
 * This function is called to initialize the MAC address of the Axi Ethernet
 * core. It calls the core specific axienet_set_mac_address. This is the
 * function that goes into net_device_ops structure entry ndo_set_mac_address.
 */
static int netdev_set_mac_address(struct net_device *ndev, void *p)
{
	struct sockaddr *addr = p;
	axienet_set_mac_address(ndev, addr->sa_data);
	return 0;
}

/**
 * axienet_set_multicast_list - Prepare the multicast table
 * @ndev:	Pointer to the net_device structure
 *
 * This function is called to initialize the multicast table during
 * initialization. The Axi Ethernet basic multicast support has a four-entry
 * multicast table which is initialized here. Additionally this function
 * goes into the net_device_ops structure entry ndo_set_multicast_list. This
 * means whenever the multicast table entries need to be updated this
 * function gets called.
 */
static void axienet_set_multicast_list(struct net_device *ndev)
{
	int i;
	u32 reg, af0reg, af1reg;
	struct axienet_local *lp = netdev_priv(ndev);

	if (ndev->flags & (IFF_ALLMULTI | IFF_PROMISC) ||
	    netdev_mc_count(ndev) > XAE_MULTICAST_CAM_TABLE_NUM) {
		/* We must make the kernel realize we had to move into
		 * promiscuous mode. If it was a promiscuous mode request
		 * the flag is already set. If not we set it.
		 */
		ndev->flags |= IFF_PROMISC;
		reg = axienet_ior(lp, XAE_FMI_OFFSET);
		reg |= XAE_FMI_PM_MASK;
		axienet_iow(lp, XAE_FMI_OFFSET, reg);
		dev_info(&ndev->dev, "Promiscuous mode enabled.\n");
	} else if (!netdev_mc_empty(ndev)) {
		struct netdev_hw_addr *ha;

		i = 0;
		netdev_for_each_mc_addr(ha, ndev) {
			if (i >= XAE_MULTICAST_CAM_TABLE_NUM)
				break;

			af0reg = (ha->addr[0]);
			af0reg |= (ha->addr[1] << 8);
			af0reg |= (ha->addr[2] << 16);
			af0reg |= (ha->addr[3] << 24);

			af1reg = (ha->addr[4]);
			af1reg |= (ha->addr[5] << 8);

			reg = axienet_ior(lp, XAE_FMI_OFFSET) & 0xFFFFFF00;
			reg |= i;

			axienet_iow(lp, XAE_FMI_OFFSET, reg);
			axienet_iow(lp, XAE_AF0_OFFSET, af0reg);
			axienet_iow(lp, XAE_AF1_OFFSET, af1reg);
			i++;
		}
	} else {
		reg = axienet_ior(lp, XAE_FMI_OFFSET);
		reg &= ~XAE_FMI_PM_MASK;

		axienet_iow(lp, XAE_FMI_OFFSET, reg);

		for (i = 0; i < XAE_MULTICAST_CAM_TABLE_NUM; i++) {
			reg = axienet_ior(lp, XAE_FMI_OFFSET) & 0xFFFFFF00;
			reg |= i;

			axienet_iow(lp, XAE_FMI_OFFSET, reg);
			axienet_iow(lp, XAE_AF0_OFFSET, 0);
			axienet_iow(lp, XAE_AF1_OFFSET, 0);
		}

		dev_info(&ndev->dev, "Promiscuous mode disabled.\n");
	}
}

/**
 * axienet_setoptions - Set an Axi Ethernet option
 * @ndev:	Pointer to the net_device structure
 * @options:	Option to be enabled/disabled
 *
 * The Axi Ethernet core has multiple features which can be selectively turned
 * on or off. The typical options could be jumbo frame option, basic VLAN
 * option, promiscuous mode option etc. This function is used to set or clear
 * these options in the Axi Ethernet hardware. This is done through
 * axienet_option structure .
 */
static void axienet_setoptions(struct net_device *ndev, u32 options)
{
	int reg;
	struct axienet_local *lp = netdev_priv(ndev);
	struct axienet_option *tp = &axienet_options[0];

	while (tp->opt) {
		reg = ((axienet_ior(lp, tp->reg)) & ~(tp->m_or));
		if (options & tp->opt)
			reg |= tp->m_or;
		axienet_iow(lp, tp->reg, reg);
		tp++;
	}

	lp->options |= options;
}

/**
 * axienet_device_reset - Reset and initialize the Axi Ethernet hardware.
 * @ndev:	Pointer to the net_device structure
 *
 * This function is called to reset and initialize the Axi Ethernet core. This
 * is typically called during initialization. It does a reset of the Axi DMA
 * Rx/Tx channels and initializes the Axi DMA BDs. Since Axi DMA reset lines
 * areconnected to Axi Ethernet reset lines, this in turn resets the Axi
 * Ethernet core. No separate hardware reset is done for the Axi Ethernet
 * core.
 * Returns 0 on success or a negative error number otherwise.
 */
static int axienet_device_reset(struct net_device *ndev)
{
	u32 axienet_status;
	struct axienet_local *lp = netdev_priv(ndev);

	/* TODO: Request DMA RESET */

	lp->max_frm_size = XAE_MAX_VLAN_FRAME_SIZE;
	lp->options |= XAE_OPTION_VLAN;
	lp->options &= (~XAE_OPTION_JUMBO);

	if ((ndev->mtu > XAE_MTU) &&
		(ndev->mtu <= XAE_JUMBO_MTU)) {
		lp->max_frm_size = ndev->mtu + VLAN_ETH_HLEN +
					XAE_TRL_SIZE;

		if (lp->max_frm_size <= lp->rxmem)
			lp->options |= XAE_OPTION_JUMBO;
	}

	/* TODO: BD initialization */
	axienet_status = axienet_ior(lp, XAE_RCW1_OFFSET);
	axienet_status &= ~XAE_RCW1_RX_MASK;
	axienet_iow(lp, XAE_RCW1_OFFSET, axienet_status);

	axienet_status = axienet_ior(lp, XAE_IP_OFFSET);
	if (axienet_status & XAE_INT_RXRJECT_MASK)
		axienet_iow(lp, XAE_IS_OFFSET, XAE_INT_RXRJECT_MASK);
	axienet_iow(lp, XAE_IE_OFFSET, lp->eth_irq > 0 ?
		    XAE_INT_RECV_ERROR_MASK : 0);

	axienet_iow(lp, XAE_FCC_OFFSET, XAE_FCC_FCRX_MASK);

	/* Sync default options with HW but leave receiver and
	 * transmitter disabled.
	 */
	axienet_setoptions(ndev, lp->options &
			   ~(XAE_OPTION_TXEN | XAE_OPTION_RXEN));
	axienet_set_mac_address(ndev, NULL);
	axienet_set_multicast_list(ndev);
	axienet_setoptions(ndev, lp->options);

	netif_trans_update(ndev);

	return 0;
}

/**
 * axienet_dma_tx_cb - DMA engine callback for TX channel.
 * @data:       Pointer to the net_device structure
 * @result:     error reporting through dmaengine_result.
 * This function is called by dmaengine driver for TX channel to notify
 * that the transmit is done.
 */

static void axienet_dma_tx_cb(void *data, const struct dmaengine_result *result)
{
	struct axi_skbuff *axi_skb = data;
	struct sk_buff *skb = axi_skb->skb;

	struct net_device *netdev = skb->dev;
	struct axienet_local *lp = netdev_priv(netdev);

	dma_unmap_sg(lp->dev, axi_skb->sgl, axi_skb->sg_len, DMA_MEM_TO_DEV);
	dev_kfree_skb_any(skb);
	kmem_cache_free(lp->skb_cache, axi_skb);
	netdev->stats.tx_packets++;

	if (netif_queue_stopped(netdev))
		netif_wake_queue(netdev);
}

/**
 * axienet_start_xmit - Starts the transmission.
 * @skb:	sk_buff pointer that contains data to be Txed.
 * @ndev:	Pointer to net_device structure.
 *
 * Return: NETDEV_TX_OK, on success
 *	    NETDEV_TX_BUSY, if any of the descriptors are not free
 *
 * This function is invoked from upper layers to initiate transmission. The
 * function uses the next available free BDs and populates their fields to
 * start the transmission. Additionally if checksum offloading is supported,
 * it populates AXI Stream Control fields with appropriate values.
 */
static netdev_tx_t
axienet_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	u32 csum_start_off;
	u32 csum_index_off;
	int sg_len;
	struct axienet_local *lp = netdev_priv(ndev);
	struct dma_async_tx_descriptor *dma_tx_desc = NULL;
	struct axi_skbuff *axi_skb;
	u32 app[DMA_NUM_APP_WORDS] = {0};
	int ret;

	sg_len = skb_shinfo(skb)->nr_frags + 1;
	axi_skb = kmem_cache_zalloc(lp->skb_cache, GFP_KERNEL);
	if (!axi_skb)
		return NETDEV_TX_BUSY;

	sg_init_table(axi_skb->sgl, sg_len);
	ret = skb_to_sgvec(skb, axi_skb->sgl, 0, skb->len);
	if (unlikely(ret < 0))
		goto xmit_error_skb_sgvec;

	dma_map_sg(lp->dev, axi_skb->sgl, sg_len, DMA_TO_DEVICE);

	/*Fill up app fields for checksum */
	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		if (lp->features & XAE_FEATURE_FULL_TX_CSUM) {
			/* Tx Full Checksum Offload Enabled */
			app[0] |= 2;
		} else if (lp->features & XAE_FEATURE_PARTIAL_RX_CSUM) {
			csum_start_off = skb_transport_offset(skb);
			csum_index_off = csum_start_off + skb->csum_offset;
			/* Tx Partial Checksum Offload Enabled */
			app[0] |= 1;
			app[1] = (csum_start_off << 16) | csum_index_off;
		}
	} else if (skb->ip_summed == CHECKSUM_UNNECESSARY) {
		app[0] |= 2; /* Tx Full Checksum Offload Enabled */
	}

	dma_tx_desc = lp->tx_chan->device->device_prep_slave_sg(lp->tx_chan, axi_skb->sgl,
								sg_len, DMA_MEM_TO_DEV,
								DMA_PREP_INTERRUPT, (void *)app);

	if (!dma_tx_desc)
		goto xmit_error_prep;

	axi_skb->skb = skb;
	axi_skb->sg_len = sg_len;
	dma_tx_desc->callback_param =  axi_skb;
	dma_tx_desc->callback_result = axienet_dma_tx_cb;
	dmaengine_submit(dma_tx_desc);
	dma_async_issue_pending(lp->tx_chan);
	ndev->stats.tx_bytes += skb->len;

	return NETDEV_TX_OK;

xmit_error_prep:
	dma_unmap_sg(lp->dev, axi_skb->sgl, sg_len, DMA_TO_DEVICE);
xmit_error_skb_sgvec:
	kmem_cache_free(lp->skb_cache, axi_skb);
	return NETDEV_TX_BUSY;
}

/**
 * axienet_eth_irq - Ethernet core Isr.
 * @irq:	irq number
 * @_ndev:	net_device pointer
 *
 * Return: IRQ_HANDLED if device generated a core interrupt, IRQ_NONE otherwise.
 *
 * Handle miscellaneous conditions indicated by Ethernet core IRQ.
 */
static irqreturn_t axienet_eth_irq(int irq, void *_ndev)
{
	struct net_device *ndev = _ndev;
	struct axienet_local *lp = netdev_priv(ndev);
	unsigned int pending;

	pending = axienet_ior(lp, XAE_IP_OFFSET);
	if (!pending)
		return IRQ_NONE;

	if (pending & XAE_INT_RXFIFOOVR_MASK)
		ndev->stats.rx_missed_errors++;

	if (pending & XAE_INT_RXRJECT_MASK)
		ndev->stats.rx_frame_errors++;

	axienet_iow(lp, XAE_IS_OFFSET, pending);
	return IRQ_HANDLED;
}

/**
 * axienet_open - Driver open routine.
 * @ndev:	Pointer to net_device structure
 *
 * Return: 0, on success.
 *	    non-zero error value on failure
 *
 * This is the driver open routine. It calls phylink_start to start the
 * PHY device.
 * It also allocates interrupt service routines, enables the interrupt lines
 * and ISR handling. Axi Ethernet core is reset through Axi DMA core. Buffer
 * descriptors are initialized.
 */
static int axienet_open(struct net_device *ndev)
{
	int ret;
	struct axienet_local *lp = netdev_priv(ndev);

	dev_dbg(&ndev->dev, "axienet_open()\n");

	/* When we do an Axi Ethernet reset, it resets the complete core
	 * including the MDIO. MDIO must be disabled before resetting.
	 * Hold MDIO bus lock to avoid MDIO accesses during the reset.
	 */
	mutex_lock(&lp->mii_bus->mdio_lock);
	ret = axienet_device_reset(ndev);
	mutex_unlock(&lp->mii_bus->mdio_lock);

	ret = phylink_of_phy_connect(lp->phylink, lp->dev->of_node, 0);
	if (ret) {
		dev_err(lp->dev, "phylink_of_phy_connect() failed: %d\n", ret);
		return ret;
	}

	phylink_start(lp->phylink);

	/* Enable interrupts for Axi Ethernet core (if defined) */
	if (lp->eth_irq > 0) {
		ret = request_irq(lp->eth_irq, axienet_eth_irq, IRQF_SHARED,
				  ndev->name, ndev);
		if (ret)
			goto err_eth_irq;
	}

	 /* Setup dma channel */
	ret = axienet_setup_dma_chan(ndev);
	if (ret < 0)
		goto err_dma_setup;

	return 0;

err_dma_setup:
	free_irq(lp->eth_irq, ndev);
err_eth_irq:
	phylink_stop(lp->phylink);
	phylink_disconnect_phy(lp->phylink);
	dev_err(lp->dev, "request_irq() failed\n");
	return ret;
}

/**
 * axienet_stop - Driver stop routine.
 * @ndev:	Pointer to net_device structure
 *
 * Return: 0, on success.
 *
 * This is the driver stop routine. It calls phylink_disconnect to stop the PHY
 * device. It also removes the interrupt handlers and disables the interrupts.
 * The Axi DMA Tx/Rx BDs are released.
 */
static int axienet_stop(struct net_device *ndev)
{
	struct axienet_local *lp = netdev_priv(ndev);

	dev_dbg(&ndev->dev, "axienet_close()\n");

	phylink_stop(lp->phylink);
	phylink_disconnect_phy(lp->phylink);

	axienet_setoptions(ndev, lp->options &
			   ~(XAE_OPTION_TXEN | XAE_OPTION_RXEN));

	axienet_iow(lp, XAE_IE_OFFSET, 0);

	dma_release_channel(lp->rx_chan);
	dma_release_channel(lp->tx_chan);

	if (lp->eth_irq > 0)
		free_irq(lp->eth_irq, ndev);

	return 0;
}

/**
 * axienet_change_mtu - Driver change mtu routine.
 * @ndev:	Pointer to net_device structure
 * @new_mtu:	New mtu value to be applied
 *
 * Return: Always returns 0 (success).
 *
 * This is the change mtu driver routine. It checks if the Axi Ethernet
 * hardware supports jumbo frames before changing the mtu. This can be
 * called only when the device is not up.
 */
static int axienet_change_mtu(struct net_device *ndev, int new_mtu)
{
	struct axienet_local *lp = netdev_priv(ndev);

	if (netif_running(ndev))
		return -EBUSY;

	if ((new_mtu + VLAN_ETH_HLEN +
		XAE_TRL_SIZE) > lp->rxmem)
		return -EINVAL;

	ndev->mtu = new_mtu;

	return 0;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
/**
 * axienet_poll_controller - Axi Ethernet poll mechanism.
 * @ndev:	Pointer to net_device structure
 *
 * This implements Rx/Tx ISR poll mechanisms. The interrupts are disabled prior
 * to polling the ISRs and are enabled back after the polling is done.
 */
static void axienet_poll_controller(struct net_device *ndev)
{
	struct axienet_local *lp = netdev_priv(ndev);
	/* TODO: Placeholder to implement poll mechanism */
}
#endif

static int axienet_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct axienet_local *lp = netdev_priv(dev);

	if (!netif_running(dev))
		return -EINVAL;

	return phylink_mii_ioctl(lp->phylink, rq, cmd);
}

static const struct net_device_ops axienet_netdev_ops = {
	.ndo_open = axienet_open,
	.ndo_stop = axienet_stop,
	.ndo_start_xmit = axienet_start_xmit,
	.ndo_change_mtu	= axienet_change_mtu,
	.ndo_set_mac_address = netdev_set_mac_address,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_do_ioctl = axienet_ioctl,
	.ndo_set_rx_mode = axienet_set_multicast_list,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller = axienet_poll_controller,
#endif
};

/**
 * axienet_ethtools_get_drvinfo - Get various Axi Ethernet driver information.
 * @ndev:	Pointer to net_device structure
 * @ed:		Pointer to ethtool_drvinfo structure
 *
 * This implements ethtool command for getting the driver information.
 * Issue "ethtool -i ethX" under linux prompt to execute this function.
 */
static void axienet_ethtools_get_drvinfo(struct net_device *ndev,
					 struct ethtool_drvinfo *ed)
{
	strlcpy(ed->driver, DRIVER_NAME, sizeof(ed->driver));
	strlcpy(ed->version, DRIVER_VERSION, sizeof(ed->version));
}

/**
 * axienet_ethtools_get_regs_len - Get the total regs length present in the
 *				   AxiEthernet core.
 * @ndev:	Pointer to net_device structure
 *
 * This implements ethtool command for getting the total register length
 * information.
 *
 * Return: the total regs length
 */
static int axienet_ethtools_get_regs_len(struct net_device *ndev)
{
	return sizeof(u32) * AXIENET_REGS_N;
}

/**
 * axienet_ethtools_get_regs - Dump the contents of all registers present
 *			       in AxiEthernet core.
 * @ndev:	Pointer to net_device structure
 * @regs:	Pointer to ethtool_regs structure
 * @ret:	Void pointer used to return the contents of the registers.
 *
 * This implements ethtool command for getting the Axi Ethernet register dump.
 * Issue "ethtool -d ethX" to execute this function.
 */
static void axienet_ethtools_get_regs(struct net_device *ndev,
				      struct ethtool_regs *regs, void *ret)
{
	u32 *data = (u32 *) ret;
	size_t len = sizeof(u32) * AXIENET_REGS_N;
	struct axienet_local *lp = netdev_priv(ndev);

	regs->version = 0;
	regs->len = len;

	memset(data, 0, len);
	data[0] = axienet_ior(lp, XAE_RAF_OFFSET);
	data[1] = axienet_ior(lp, XAE_TPF_OFFSET);
	data[2] = axienet_ior(lp, XAE_IFGP_OFFSET);
	data[3] = axienet_ior(lp, XAE_IS_OFFSET);
	data[4] = axienet_ior(lp, XAE_IP_OFFSET);
	data[5] = axienet_ior(lp, XAE_IE_OFFSET);
	data[6] = axienet_ior(lp, XAE_TTAG_OFFSET);
	data[7] = axienet_ior(lp, XAE_RTAG_OFFSET);
	data[8] = axienet_ior(lp, XAE_UAWL_OFFSET);
	data[9] = axienet_ior(lp, XAE_UAWU_OFFSET);
	data[10] = axienet_ior(lp, XAE_TPID0_OFFSET);
	data[11] = axienet_ior(lp, XAE_TPID1_OFFSET);
	data[12] = axienet_ior(lp, XAE_PPST_OFFSET);
	data[13] = axienet_ior(lp, XAE_RCW0_OFFSET);
	data[14] = axienet_ior(lp, XAE_RCW1_OFFSET);
	data[15] = axienet_ior(lp, XAE_TC_OFFSET);
	data[16] = axienet_ior(lp, XAE_FCC_OFFSET);
	data[17] = axienet_ior(lp, XAE_EMMC_OFFSET);
	data[18] = axienet_ior(lp, XAE_PHYC_OFFSET);
	data[19] = axienet_ior(lp, XAE_MDIO_MC_OFFSET);
	data[20] = axienet_ior(lp, XAE_MDIO_MCR_OFFSET);
	data[21] = axienet_ior(lp, XAE_MDIO_MWD_OFFSET);
	data[22] = axienet_ior(lp, XAE_MDIO_MRD_OFFSET);
	data[27] = axienet_ior(lp, XAE_UAW0_OFFSET);
	data[28] = axienet_ior(lp, XAE_UAW1_OFFSET);
	data[29] = axienet_ior(lp, XAE_FMI_OFFSET);
	data[30] = axienet_ior(lp, XAE_AF0_OFFSET);
	data[31] = axienet_ior(lp, XAE_AF1_OFFSET);

	/*TODO : explore how to dump DMA registers here?*/
}

static void axienet_ethtools_get_ringparam(struct net_device *ndev,
					   struct ethtool_ringparam *ering)
{
	struct axienet_local *lp = netdev_priv(ndev);

	ering->rx_max_pending = RX_BD_NUM_MAX;
	ering->rx_mini_max_pending = 0;
	ering->rx_jumbo_max_pending = 0;
	ering->tx_max_pending = TX_BD_NUM_MAX;
	ering->rx_pending = lp->rx_bd_num;
	ering->rx_mini_pending = 0;
	ering->rx_jumbo_pending = 0;
	ering->tx_pending = lp->tx_bd_num;
}

static int axienet_ethtools_set_ringparam(struct net_device *ndev,
					  struct ethtool_ringparam *ering)
{
	struct axienet_local *lp = netdev_priv(ndev);

	if (ering->rx_pending > RX_BD_NUM_MAX ||
	    ering->rx_mini_pending ||
	    ering->rx_jumbo_pending ||
	    ering->rx_pending > TX_BD_NUM_MAX)
		return -EINVAL;

	if (netif_running(ndev))
		return -EBUSY;

	lp->rx_bd_num = ering->rx_pending;
	lp->tx_bd_num = ering->tx_pending;
	return 0;
}

/**
 * axienet_ethtools_get_pauseparam - Get the pause parameter setting for
 *				     Tx and Rx paths.
 * @ndev:	Pointer to net_device structure
 * @epauseparm:	Pointer to ethtool_pauseparam structure.
 *
 * This implements ethtool command for getting axi ethernet pause frame
 * setting. Issue "ethtool -a ethX" to execute this function.
 */
static void
axienet_ethtools_get_pauseparam(struct net_device *ndev,
				struct ethtool_pauseparam *epauseparm)
{
	struct axienet_local *lp = netdev_priv(ndev);

	phylink_ethtool_get_pauseparam(lp->phylink, epauseparm);
}

/**
 * axienet_ethtools_set_pauseparam - Set device pause parameter(flow control)
 *				     settings.
 * @ndev:	Pointer to net_device structure
 * @epauseparm:Pointer to ethtool_pauseparam structure
 *
 * This implements ethtool command for enabling flow control on Rx and Tx
 * paths. Issue "ethtool -A ethX tx on|off" under linux prompt to execute this
 * function.
 *
 * Return: 0 on success, -EFAULT if device is running
 */
static int
axienet_ethtools_set_pauseparam(struct net_device *ndev,
				struct ethtool_pauseparam *epauseparm)
{
	struct axienet_local *lp = netdev_priv(ndev);

	return phylink_ethtool_set_pauseparam(lp->phylink, epauseparm);
}

/**
 * axienet_ethtools_get_coalesce - Get DMA interrupt coalescing count.
 * @ndev:	Pointer to net_device structure
 * @ecoalesce:	Pointer to ethtool_coalesce structure
 *
 * This implements ethtool command for getting the DMA interrupt coalescing
 * count on Tx and Rx paths. Issue "ethtool -c ethX" under linux prompt to
 * execute this function.
 *
 * Return: 0 always
 */
static int axienet_ethtools_get_coalesce(struct net_device *ndev,
					 struct ethtool_coalesce *ecoalesce)
{
	/* TODO: Request and populate DMA engine TX and RX coalesc params */

	return 0;
}

/**
 * axienet_ethtools_set_coalesce - Set DMA interrupt coalescing count.
 * @ndev:	Pointer to net_device structure
 * @ecoalesce:	Pointer to ethtool_coalesce structure
 *
 * This implements ethtool command for setting the DMA interrupt coalescing
 * count on Tx and Rx paths. Issue "ethtool -C ethX rx-frames 5" under linux
 * prompt to execute this function.
 *
 * Return: 0, on success, Non-zero error value on failure.
 */
static int axienet_ethtools_set_coalesce(struct net_device *ndev,
					 struct ethtool_coalesce *ecoalesce)
{
	struct axienet_local *lp = netdev_priv(ndev);

	if (netif_running(ndev)) {
		netdev_err(ndev,
			   "Please stop netif before applying configuration\n");
		return -EFAULT;
	}

	if (ecoalesce->rx_max_coalesced_frames)
		lp->coalesce_count_rx = ecoalesce->rx_max_coalesced_frames;
	if (ecoalesce->tx_max_coalesced_frames)
		lp->coalesce_count_tx = ecoalesce->tx_max_coalesced_frames;

	return 0;
}

static int
axienet_ethtools_get_link_ksettings(struct net_device *ndev,
				    struct ethtool_link_ksettings *cmd)
{
	struct axienet_local *lp = netdev_priv(ndev);

	return phylink_ethtool_ksettings_get(lp->phylink, cmd);
}

static int
axienet_ethtools_set_link_ksettings(struct net_device *ndev,
				    const struct ethtool_link_ksettings *cmd)
{
	struct axienet_local *lp = netdev_priv(ndev);

	return phylink_ethtool_ksettings_set(lp->phylink, cmd);
}

static int axienet_ethtools_nway_reset(struct net_device *dev)
{
	struct axienet_local *lp = netdev_priv(dev);

	return phylink_ethtool_nway_reset(lp->phylink);
}

static const struct ethtool_ops axienet_ethtool_ops = {
	.supported_coalesce_params = ETHTOOL_COALESCE_MAX_FRAMES,
	.get_drvinfo    = axienet_ethtools_get_drvinfo,
	.get_regs_len   = axienet_ethtools_get_regs_len,
	.get_regs       = axienet_ethtools_get_regs,
	.get_link       = ethtool_op_get_link,
	.get_ringparam	= axienet_ethtools_get_ringparam,
	.set_ringparam	= axienet_ethtools_set_ringparam,
	.get_pauseparam = axienet_ethtools_get_pauseparam,
	.set_pauseparam = axienet_ethtools_set_pauseparam,
	.get_coalesce   = axienet_ethtools_get_coalesce,
	.set_coalesce   = axienet_ethtools_set_coalesce,
	.get_link_ksettings = axienet_ethtools_get_link_ksettings,
	.set_link_ksettings = axienet_ethtools_set_link_ksettings,
	.nway_reset	= axienet_ethtools_nway_reset,
};

static void axienet_validate(struct phylink_config *config,
			     unsigned long *supported,
			     struct phylink_link_state *state)
{
	struct net_device *ndev = to_net_dev(config->dev);
	struct axienet_local *lp = netdev_priv(ndev);
	__ETHTOOL_DECLARE_LINK_MODE_MASK(mask) = { 0, };

	/* Only support the mode we are configured for */
	switch (state->interface) {
	case PHY_INTERFACE_MODE_NA:
		break;
	case PHY_INTERFACE_MODE_1000BASEX:
	case PHY_INTERFACE_MODE_SGMII:
		if (lp->switch_x_sgmii)
			break;
		fallthrough;
	default:
		if (state->interface != lp->phy_mode) {
			netdev_warn(ndev, "Cannot use PHY mode %s, supported: %s\n",
				    phy_modes(state->interface),
				    phy_modes(lp->phy_mode));
			bitmap_zero(supported, __ETHTOOL_LINK_MODE_MASK_NBITS);
			return;
		}
	}

	phylink_set(mask, Autoneg);
	phylink_set_port_modes(mask);

	phylink_set(mask, Asym_Pause);
	phylink_set(mask, Pause);

	switch (state->interface) {
	case PHY_INTERFACE_MODE_NA:
	case PHY_INTERFACE_MODE_1000BASEX:
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_GMII:
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		phylink_set(mask, 1000baseX_Full);
		phylink_set(mask, 1000baseT_Full);
		if (state->interface == PHY_INTERFACE_MODE_1000BASEX)
			break;
		fallthrough;
	case PHY_INTERFACE_MODE_MII:
		phylink_set(mask, 100baseT_Full);
		phylink_set(mask, 10baseT_Full);
	default:
		break;
	}

	bitmap_and(supported, supported, mask,
		   __ETHTOOL_LINK_MODE_MASK_NBITS);
	bitmap_and(state->advertising, state->advertising, mask,
		   __ETHTOOL_LINK_MODE_MASK_NBITS);
}

static void axienet_mac_pcs_get_state(struct phylink_config *config,
				      struct phylink_link_state *state)
{
	struct net_device *ndev = to_net_dev(config->dev);
	struct axienet_local *lp = netdev_priv(ndev);

	switch (state->interface) {
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_1000BASEX:
		phylink_mii_c22_pcs_get_state(lp->pcs_phy, state);
		break;
	default:
		break;
	}
}

static void axienet_mac_an_restart(struct phylink_config *config)
{
	struct net_device *ndev = to_net_dev(config->dev);
	struct axienet_local *lp = netdev_priv(ndev);

	phylink_mii_c22_pcs_an_restart(lp->pcs_phy);
}

static int axienet_mac_prepare(struct phylink_config *config, unsigned int mode,
			       phy_interface_t iface)
{
	struct net_device *ndev = to_net_dev(config->dev);
	struct axienet_local *lp = netdev_priv(ndev);
	int ret;

	switch (iface) {
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_1000BASEX:
		if (!lp->switch_x_sgmii)
			return 0;

		ret = mdiobus_write(lp->pcs_phy->bus,
				    lp->pcs_phy->addr,
				    XLNX_MII_STD_SELECT_REG,
				    iface == PHY_INTERFACE_MODE_SGMII ?
					XLNX_MII_STD_SELECT_SGMII : 0);
		if (ret < 0)
			netdev_warn(ndev, "Failed to switch PHY interface: %d\n",
				    ret);
		return ret;
	default:
		return 0;
	}
}

static void axienet_mac_config(struct phylink_config *config, unsigned int mode,
			       const struct phylink_link_state *state)
{
	struct net_device *ndev = to_net_dev(config->dev);
	struct axienet_local *lp = netdev_priv(ndev);
	int ret;

	switch (state->interface) {
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_1000BASEX:
		ret = phylink_mii_c22_pcs_config(lp->pcs_phy, mode,
						 state->interface,
						 state->advertising);
		if (ret < 0)
			netdev_warn(ndev, "Failed to configure PCS: %d\n",
				    ret);
		break;

	default:
		break;
	}
}

static void axienet_mac_link_down(struct phylink_config *config,
				  unsigned int mode,
				  phy_interface_t interface)
{
	/* nothing meaningful to do */
}

static void axienet_mac_link_up(struct phylink_config *config,
				struct phy_device *phy,
				unsigned int mode, phy_interface_t interface,
				int speed, int duplex,
				bool tx_pause, bool rx_pause)
{
	struct net_device *ndev = to_net_dev(config->dev);
	struct axienet_local *lp = netdev_priv(ndev);
	u32 emmc_reg, fcc_reg;

	emmc_reg = axienet_ior(lp, XAE_EMMC_OFFSET);
	emmc_reg &= ~XAE_EMMC_LINKSPEED_MASK;

	switch (speed) {
	case SPEED_1000:
		emmc_reg |= XAE_EMMC_LINKSPD_1000;
		break;
	case SPEED_100:
		emmc_reg |= XAE_EMMC_LINKSPD_100;
		break;
	case SPEED_10:
		emmc_reg |= XAE_EMMC_LINKSPD_10;
		break;
	default:
		dev_err(&ndev->dev,
			"Speed other than 10, 100 or 1Gbps is not supported\n");
		break;
	}

	axienet_iow(lp, XAE_EMMC_OFFSET, emmc_reg);

	fcc_reg = axienet_ior(lp, XAE_FCC_OFFSET);
	if (tx_pause)
		fcc_reg |= XAE_FCC_FCTX_MASK;
	else
		fcc_reg &= ~XAE_FCC_FCTX_MASK;
	if (rx_pause)
		fcc_reg |= XAE_FCC_FCRX_MASK;
	else
		fcc_reg &= ~XAE_FCC_FCRX_MASK;
	axienet_iow(lp, XAE_FCC_OFFSET, fcc_reg);
}

static const struct phylink_mac_ops axienet_phylink_ops = {
	.validate = axienet_validate,
	.mac_pcs_get_state = axienet_mac_pcs_get_state,
	.mac_an_restart = axienet_mac_an_restart,
	.mac_prepare = axienet_mac_prepare,
	.mac_config = axienet_mac_config,
	.mac_link_down = axienet_mac_link_down,
	.mac_link_up = axienet_mac_link_up,
};

/**
 * axienet_probe - Axi Ethernet probe function.
 * @pdev:	Pointer to platform device structure.
 *
 * Return: 0, on success
 *	    Non-zero error value on failure.
 *
 * This is the probe routine for Axi Ethernet driver. This is called before
 * any other driver routines are invoked. It allocates and sets up the Ethernet
 * device. Parses through device tree and populates fields of
 * axienet_local. It registers the Ethernet device.
 */
static int axienet_probe(struct platform_device *pdev)
{
	int ret;
	struct axienet_local *lp;
	struct net_device *ndev;
	const void *mac_addr;
	struct resource *ethres;
	u32 value;

	ndev = alloc_etherdev(sizeof(*lp));
	if (!ndev)
		return -ENOMEM;

	platform_set_drvdata(pdev, ndev);

	SET_NETDEV_DEV(ndev, &pdev->dev);
	ndev->flags &= ~IFF_MULTICAST;  /* clear multicast */
	ndev->features = NETIF_F_SG;
	ndev->netdev_ops = &axienet_netdev_ops;
	ndev->ethtool_ops = &axienet_ethtool_ops;

	/* MTU range: 64 - 9000 */
	ndev->min_mtu = 64;
	ndev->max_mtu = XAE_JUMBO_MTU;

	lp = netdev_priv(ndev);
	lp->ndev = ndev;
	lp->dev = &pdev->dev;
	lp->options = XAE_OPTION_DEFAULTS;
	lp->rx_bd_num = RX_BD_NUM_DEFAULT;
	lp->tx_bd_num = TX_BD_NUM_DEFAULT;

	lp->clk = devm_clk_get_optional(&pdev->dev, NULL);
	if (IS_ERR(lp->clk)) {
		ret = PTR_ERR(lp->clk);
		goto free_netdev;
	}
	ret = clk_prepare_enable(lp->clk);
	if (ret) {
		dev_err(&pdev->dev, "Unable to enable clock: %d\n", ret);
		goto free_netdev;
	}

	/* Map device registers */
	ethres = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	lp->regs = devm_ioremap_resource(&pdev->dev, ethres);
	if (IS_ERR(lp->regs)) {
		dev_err(&pdev->dev, "could not map Axi Ethernet regs.\n");
		ret = PTR_ERR(lp->regs);
		goto free_netdev;
	}
	lp->regs_start = ethres->start;

	/* Setup checksum offload, but default to off if not specified */
	lp->features = 0;

	ret = of_property_read_u32(pdev->dev.of_node, "xlnx,txcsum", &value);
	if (!ret) {
		switch (value) {
		case 1:
			lp->csum_offload_on_tx_path =
				XAE_FEATURE_PARTIAL_TX_CSUM;
			lp->features |= XAE_FEATURE_PARTIAL_TX_CSUM;
			/* Can checksum TCP/UDP over IPv4. */
			ndev->features |= NETIF_F_IP_CSUM;
			break;
		case 2:
			lp->csum_offload_on_tx_path =
				XAE_FEATURE_FULL_TX_CSUM;
			lp->features |= XAE_FEATURE_FULL_TX_CSUM;
			/* Can checksum TCP/UDP over IPv4. */
			ndev->features |= NETIF_F_IP_CSUM;
			break;
		default:
			lp->csum_offload_on_tx_path = XAE_NO_CSUM_OFFLOAD;
		}
	}
	ret = of_property_read_u32(pdev->dev.of_node, "xlnx,rxcsum", &value);
	if (!ret) {
		switch (value) {
		case 1:
			lp->csum_offload_on_rx_path =
				XAE_FEATURE_PARTIAL_RX_CSUM;
			lp->features |= XAE_FEATURE_PARTIAL_RX_CSUM;
			break;
		case 2:
			lp->csum_offload_on_rx_path =
				XAE_FEATURE_FULL_RX_CSUM;
			lp->features |= XAE_FEATURE_FULL_RX_CSUM;
			break;
		default:
			lp->csum_offload_on_rx_path = XAE_NO_CSUM_OFFLOAD;
		}
	}
	/* For supporting jumbo frames, the Axi Ethernet hardware must have
	 * a larger Rx/Tx Memory. Typically, the size must be large so that
	 * we can enable jumbo option and start supporting jumbo frames.
	 * Here we check for memory allocated for Rx/Tx in the hardware from
	 * the device-tree and accordingly set flags.
	 */
	of_property_read_u32(pdev->dev.of_node, "xlnx,rxmem", &lp->rxmem);

	lp->switch_x_sgmii = of_property_read_bool(pdev->dev.of_node,
						   "xlnx,switch-x-sgmii");

	/* Start with the proprietary, and broken phy_type */
	ret = of_property_read_u32(pdev->dev.of_node, "xlnx,phy-type", &value);
	if (!ret) {
		netdev_warn(ndev, "Please upgrade your device tree binary blob to use phy-mode");
		switch (value) {
		case XAE_PHY_TYPE_MII:
			lp->phy_mode = PHY_INTERFACE_MODE_MII;
			break;
		case XAE_PHY_TYPE_GMII:
			lp->phy_mode = PHY_INTERFACE_MODE_GMII;
			break;
		case XAE_PHY_TYPE_RGMII_2_0:
			lp->phy_mode = PHY_INTERFACE_MODE_RGMII_ID;
			break;
		case XAE_PHY_TYPE_SGMII:
			lp->phy_mode = PHY_INTERFACE_MODE_SGMII;
			break;
		case XAE_PHY_TYPE_1000BASE_X:
			lp->phy_mode = PHY_INTERFACE_MODE_1000BASEX;
			break;
		default:
			ret = -EINVAL;
			goto free_netdev;
		}
	} else {
		ret = of_get_phy_mode(pdev->dev.of_node, &lp->phy_mode);
		if (ret)
			goto free_netdev;
	}
	if (lp->switch_x_sgmii && lp->phy_mode != PHY_INTERFACE_MODE_SGMII &&
	    lp->phy_mode != PHY_INTERFACE_MODE_1000BASEX) {
		dev_err(&pdev->dev, "xlnx,switch-x-sgmii only supported with SGMII or 1000BaseX\n");
		ret = -EINVAL;
		goto free_netdev;
	}

	/* Check for Ethernet core IRQ (optional) */
	if (lp->eth_irq <= 0)
		dev_info(&pdev->dev, "Ethernet core IRQ not defined\n");

	/* Retrieve the MAC address */
	mac_addr = of_get_mac_address(pdev->dev.of_node);
	if (IS_ERR(mac_addr)) {
		dev_warn(&pdev->dev, "could not find MAC address property: %ld\n",
			 PTR_ERR(mac_addr));
		mac_addr = NULL;
	}
	axienet_set_mac_address(ndev, mac_addr);

	lp->coalesce_count_rx = XAXIDMA_DFT_RX_THRESHOLD;
	lp->coalesce_count_tx = XAXIDMA_DFT_TX_THRESHOLD;

	lp->phy_node = of_parse_phandle(pdev->dev.of_node, "phy-handle", 0);
	if (lp->phy_node) {
		ret = axienet_mdio_setup(lp);
		if (ret)
			dev_warn(&pdev->dev,
				 "error registering MDIO bus: %d\n", ret);
	}
	if (lp->phy_mode == PHY_INTERFACE_MODE_SGMII ||
	    lp->phy_mode == PHY_INTERFACE_MODE_1000BASEX) {
		if (!lp->phy_node) {
			dev_err(&pdev->dev, "phy-handle required for 1000BaseX/SGMII\n");
			ret = -EINVAL;
			goto free_netdev;
		}
		lp->pcs_phy = of_mdio_find_device(lp->phy_node);
		if (!lp->pcs_phy) {
			ret = -EPROBE_DEFER;
			goto free_netdev;
		}
		lp->phylink_config.pcs_poll = true;
	}

	lp->phylink_config.dev = &ndev->dev;
	lp->phylink_config.type = PHYLINK_NETDEV;

	lp->phylink = phylink_create(&lp->phylink_config, pdev->dev.fwnode,
				     lp->phy_mode,
				     &axienet_phylink_ops);
	if (IS_ERR(lp->phylink)) {
		ret = PTR_ERR(lp->phylink);
		dev_err(&pdev->dev, "phylink_create error (%i)\n", ret);
		goto free_netdev;
	}

	ret = register_netdev(lp->ndev);
	if (ret) {
		dev_err(lp->dev, "register_netdev() error (%i)\n", ret);
		goto free_netdev;
	}

	return 0;

free_netdev:
	free_netdev(ndev);

	return ret;
}

static int axienet_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct axienet_local *lp = netdev_priv(ndev);

	unregister_netdev(ndev);

	if (lp->phylink)
		phylink_destroy(lp->phylink);

	if (lp->pcs_phy)
		put_device(&lp->pcs_phy->dev);

	axienet_mdio_teardown(lp);

	clk_disable_unprepare(lp->clk);

	of_node_put(lp->phy_node);
	lp->phy_node = NULL;

	free_netdev(ndev);

	return 0;
}

static void axienet_shutdown(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);

	rtnl_lock();
	netif_device_detach(ndev);

	if (netif_running(ndev))
		dev_close(ndev);

	rtnl_unlock();
}

static struct platform_driver axienet_driver = {
	.probe = axienet_probe,
	.remove = axienet_remove,
	.shutdown = axienet_shutdown,
	.driver = {
		 .name = "xilinx_axienet",
		 .of_match_table = axienet_of_match,
	},
};

module_platform_driver(axienet_driver);

MODULE_DESCRIPTION("Xilinx Axi Ethernet driver");
MODULE_AUTHOR("Xilinx");
MODULE_LICENSE("GPL");
