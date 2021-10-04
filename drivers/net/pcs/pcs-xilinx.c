// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2021 Sean Anderson <sean.anderson@seco.com>
 *
 * This is the driver for the Xilinx 1G/2.5G Ethernet PCS/PMA or SGMII LogiCORE
 * IP. A typical setup will look something like
 *
 * MAC <--GMII--> PCS+PMA <--internal/TBI--> PMD (SERDES) <--SGMII/1000BASE-X
 *
 * The link to the PMD is not modeled by this driver, except for refclk. It is
 * assumed that the SERDES needs no configuration. It is also possible to go
 * from SGMII to GMII (PHY mode), but this is not supported.
 *
 * This driver was written with reference to PG047:
 * https://www.xilinx.com/support/documentation/ip_documentation/gig_ethernet_pcs_pma/v16_2/pg047-gig-eth-pcs-pma.pdf
 */

#include <linux/clk.h>
#include <linux/mdio.h>
#include <linux/of.h>
#include <linux/phylink.h>
#include <linux/reset.h>

/* Vendor-specific MDIO registers */
#define XILINX_PCS_ANICR 16 /* Auto-Negotiation Interrupt Control Register */
#define XILINX_PCS_SSR   17 /* Standard Selection Register */

#define XILINX_PCS_ANICR_IE BIT(0) /* Interrupt Enable */
#define XILINX_PCS_ANICR_IS BIT(1) /* Interrupt Status */

#define XILINX_PCS_SSR_SGMII BIT(0) /* Select SGMII standard */

/**
 * enum xilinx_pcs_standard - Support for interface standards
 * @XILINX_PCS_STD_SGMII: SGMII for 10/100/1000BASE-T
 * @XILINX_PCS_STD_1000BASEX: 1000BASE-X PMD Support Interface
 * @XILINX_PCS_STD_BOTH: Support for both SGMII and 1000BASE-X
 * @XILINX_PCS_STD_2500BASEX: 2500BASE-X PMD Support Interface
 * @XILINX_PCS_STD_2500SGMII: 2.5G SGMII for 2.5GBASE-T
 */
enum xilinx_pcs_standard {
	XILINX_PCS_STD_SGMII,
	XILINX_PCS_STD_1000BASEX,
	XILINX_PCS_STD_BOTH,
	XILINX_PCS_STD_2500BASEX,
	XILINX_PCS_STD_2500SGMII,
};

/**
 * struct xilinx_pcs - Private data for Xilinx PCS devices
 * @pcs: The phylink PCS
 * @mdiodev: The mdiodevice used to access the PCS
 * @refclk: The reference clock for the PMD
 * @reset: The reset controller for the PCS
 * @standard: The supported interface standard
 */
struct xilinx_pcs {
	struct phylink_pcs pcs;
	struct mdio_device *mdiodev;
	struct clk *refclk;
	struct reset_control *reset;
	enum xilinx_pcs_standard standard;
};

static inline struct xilinx_pcs *pcs_to_xilinx(struct phylink_pcs *pcs)
{
	return container_of(pcs, struct xilinx_pcs, pcs);
}

static void xilinx_pcs_get_state(struct phylink_pcs *pcs,
				 struct phylink_link_state *state)
{
	struct xilinx_pcs *xp = pcs_to_xilinx(pcs);

	switch (xp->standard) {
	case XILINX_PCS_STD_SGMII:
		state->interface = PHY_INTERFACE_MODE_SGMII;
		break;
	case XILINX_PCS_STD_1000BASEX:
		state->interface = PHY_INTERFACE_MODE_1000BASEX;
		break;
	case XILINX_PCS_STD_BOTH: {
		int ssr = mdiodev_read(xp->mdiodev, XILINX_PCS_SSR);

		if (ssr < 0) {
			dev_err(pcs->dev, "could not read SSR (err=%d)\n", ssr);
			return;
		}

		if (ssr & XILINX_PCS_SSR_SGMII)
			state->interface = PHY_INTERFACE_MODE_SGMII;
		else
			state->interface = PHY_INTERFACE_MODE_1000BASEX;
		break;
	}
	case XILINX_PCS_STD_2500BASEX:
		state->interface = PHY_INTERFACE_MODE_2500BASEX;
		break;
	default:
		return;
	}

	phylink_mii_c22_pcs_get_state(xp->mdiodev, state);
}

static int xilinx_pcs_config(struct phylink_pcs *pcs, unsigned int mode,
			     phy_interface_t interface,
			     const unsigned long *advertising,
			     bool permit_pause_to_mac)
{
	int ret;
	bool changed = false;
	struct xilinx_pcs *xp = pcs_to_xilinx(pcs);

	switch (xp->standard) {
	case XILINX_PCS_STD_SGMII:
		if (interface != PHY_INTERFACE_MODE_SGMII)
			return -EOPNOTSUPP;
		break;
	case XILINX_PCS_STD_1000BASEX:
		if (interface != PHY_INTERFACE_MODE_1000BASEX)
			return -EOPNOTSUPP;
		break;
	case XILINX_PCS_STD_BOTH: {
		u16 ssr;

		if (interface == PHY_INTERFACE_MODE_SGMII)
			ssr = XILINX_PCS_SSR_SGMII;
		else if (interface == PHY_INTERFACE_MODE_1000BASEX)
			ssr = 0;
		else
			return -EOPNOTSUPP;

		ret = mdiodev_read(xp->mdiodev, XILINX_PCS_SSR);
		if (ret < 0)
			return ret;

		if (ret == ssr)
			break;

		changed = true;
		ret = mdiodev_write(xp->mdiodev, XILINX_PCS_SSR, ssr);
		if (ret)
			return ret;
		break;
	}
	case XILINX_PCS_STD_2500BASEX:
		if (interface != PHY_INTERFACE_MODE_2500BASEX)
			return -EOPNOTSUPP;
		break;
	default:
		return -EOPNOTSUPP;
	}

	ret = phylink_mii_c22_pcs_config(xp->mdiodev, mode, interface,
					 advertising);
	if (ret)
		return ret;
	return changed;
}

static void xilinx_pcs_an_restart(struct phylink_pcs *pcs)
{
	struct xilinx_pcs *xp = pcs_to_xilinx(pcs);

	phylink_mii_c22_pcs_an_restart(xp->mdiodev);
}

static void xilinx_pcs_link_up(struct phylink_pcs *pcs, unsigned int mode,
			       phy_interface_t interface, int speed, int duplex)
{
	int bmcr;
	struct xilinx_pcs *xp = pcs_to_xilinx(pcs);

	if (phylink_autoneg_inband(mode))
		return;

	bmcr = mdiodev_read(xp->mdiodev, MII_BMCR);
	if (bmcr < 0) {
		dev_err(pcs->dev, "could not read BMCR (err=%d)\n", bmcr);
		return;
	}

	bmcr &= ~BMCR_FULLDPLX;
	if (duplex == DUPLEX_FULL)
		bmcr |= BMCR_FULLDPLX;
	else if (duplex != DUPLEX_HALF)
		dev_err(pcs->dev, "unknown duplex %d\n", duplex);

	bmcr &= ~(BMCR_SPEED1000 | BMCR_SPEED100);
	switch (speed) {
	case SPEED_2500:
	case SPEED_1000:
		bmcr |= BMCR_SPEED1000;
		break;
	case SPEED_100:
		bmcr |= BMCR_SPEED100;
		break;
	case SPEED_10:
		bmcr |= BMCR_SPEED10;
		break;
	default:
		dev_err(pcs->dev, "invalid speed %d\n", speed);
	}

	bmcr = mdiodev_write(xp->mdiodev, MII_BMCR, bmcr);
	if (bmcr < 0)
		dev_err(pcs->dev, "could not write BMCR (err=%d)\n", bmcr);
}

static const struct phylink_pcs_ops xilinx_pcs_ops = {
	.pcs_get_state = xilinx_pcs_get_state,
	.pcs_config = xilinx_pcs_config,
	.pcs_an_restart = xilinx_pcs_an_restart,
	.pcs_link_up = xilinx_pcs_link_up,
};

static int xilinx_pcs_probe(struct mdio_device *mdiodev)
{
	const char *standard;
	int ret;
	struct xilinx_pcs *xp;
	struct device *dev = &mdiodev->dev;
	struct device_node *np = dev->of_node;
	u32 phy_id;

	xp = devm_kzalloc(dev, sizeof(*xp), GFP_KERNEL);
	if (!xp)
		return -ENOMEM;
	xp->mdiodev = mdiodev;
	dev_set_drvdata(dev, xp);

	ret = of_property_read_string(np, "standard", &standard);
	if (ret)
		return dev_err_probe(dev, ret, "could not read standard\n");

	if (!strncmp(standard, "1000base-x", 10))
		xp->standard = XILINX_PCS_STD_1000BASEX;
	else if (!strncmp(standard, "sgmii/1000base-x", 16))
		xp->standard = XILINX_PCS_STD_BOTH;
	else if (!strncmp(standard, "sgmii", 5))
		xp->standard = XILINX_PCS_STD_SGMII;
	else if (!strncmp(standard, "2500base-x", 10))
		xp->standard = XILINX_PCS_STD_2500BASEX;
	/* TODO: 2.5G SGMII support */
	else
		return dev_err_probe(dev, -EINVAL,
				     "unknown/unsupported standard %s\n",
				     standard);

	xp->refclk = devm_clk_get(dev, "refclk");
	if (IS_ERR(xp->refclk))
		return dev_err_probe(dev, PTR_ERR(xp->refclk),
				     "could not get reference clock\n");

	xp->reset = devm_reset_control_get_exclusive(dev, "pcs");
	if (IS_ERR(xp->reset))
		return dev_err_probe(dev, PTR_ERR(xp->reset),
				     "could not get reset\n");

	ret = reset_control_assert(xp->reset);
	if (ret)
		return dev_err_probe(dev, ret, "could not enter reset\n");

	ret = clk_prepare_enable(xp->refclk);
	if (ret)
		return dev_err_probe(dev, ret,
				     "could not enable reference clock\n");

	ret = reset_control_deassert(xp->reset);
	if (ret) {
		dev_err_probe(dev, ret, "could not exit reset\n");
		goto err_unprepare;
	}

	/* Sanity check */
	ret = get_phy_c22_id(mdiodev->bus, mdiodev->addr, &phy_id);
	if (ret) {
		dev_err_probe(dev, ret, "could not read id\n");
		goto err_unprepare;
	}
	if ((phy_id & 0xfffffff0) != 0x01740c00)
		dev_warn(dev, "unknown phy id %x\n", phy_id);

	xp->pcs.dev = dev;
	xp->pcs.ops = &xilinx_pcs_ops;
	xp->pcs.poll = true;
	ret = phylink_register_pcs(&xp->pcs);
	if (ret)
		return dev_err_probe(dev, ret, "could not register PCS\n");
	dev_info(dev, "probed (standard=%s)\n", standard);
	return 0;

err_unprepare:
	clk_disable_unprepare(xp->refclk);
	return ret;
}

static void xilinx_pcs_remove(struct mdio_device *mdiodev)
{
	struct xilinx_pcs *xp = dev_get_drvdata(&mdiodev->dev);

	phylink_unregister_pcs(&xp->pcs);
	reset_control_assert(xp->reset);
	clk_disable_unprepare(xp->refclk);
}

static const struct of_device_id xilinx_pcs_of_match[] = {
	{ .compatible = "xlnx,pcs-16.2", },
	{},
};
MODULE_DEVICE_TABLE(of, xilinx_timer_of_match);

static struct mdio_driver xilinx_pcs_driver = {
	.probe = xilinx_pcs_probe,
	.remove = xilinx_pcs_remove,
	.mdiodrv.driver = {
		.name = "xilinx-pcs",
		.of_match_table = of_match_ptr(xilinx_pcs_of_match),
	},
};
mdio_module_driver(xilinx_pcs_driver);

MODULE_ALIAS("platform:xilinx-pcs");
MODULE_DESCRIPTION("Xilinx PCS driver");
MODULE_LICENSE("GPL v2");
