/* SPDX-License-Identifier: GPL-2.0+ */

/* Copyright 1997 Dan Malek (dmalek@jlc.net)
 * Copyright 2000 Ericsson Radio Systems AB.
 * Copyright 2001-2005 Greg Ungerer (gerg@snapgear.com)
 * Copyright 2004-2006 Macq Electronique SA.
 * Copyright 2010-2011 Freescale Semiconductor, Inc.
 * Copyright 2021 NXP
 */

/* FEC MII MMFR bits definition */
#define FEC_MMFR_ST             BIT(30)
#define FEC_MMFR_ST_C45         (0)
#define FEC_MMFR_OP_READ        (2 << 28)
#define FEC_MMFR_OP_READ_C45    (3 << 28)
#define FEC_MMFR_OP_WRITE       BIT(28)
#define FEC_MMFR_OP_ADDR_WRITE  (0)
#define FEC_MMFR_PA(v)          (((v) & 0x1f) << 23)
#define FEC_MMFR_RA(v)          (((v) & 0x1f) << 18)
#define FEC_MMFR_TA             (2 << 16)
#define FEC_MMFR_DATA(v)        ((v) & 0xffff)

#define FEC_MDIO_PM_TIMEOUT	100 /* ms */

int fec_reset_phy(struct platform_device *pdev);
void fec_enet_phy_reset_after_clk_enable(struct net_device *ndev);
int fec_enet_mdio_wait(struct fec_enet_private *fep);
int fec_enet_mdio_read(struct mii_bus *bus, int mii_id, int regnum);
int fec_enet_mdio_write(struct mii_bus *bus, int mii_id, int regnum, u16 value);
int fec_enet_mii_init(struct platform_device *pdev);
void fec_enet_mii_remove(struct fec_enet_private *fep);
