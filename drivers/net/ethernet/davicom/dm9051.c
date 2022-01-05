// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021 Davicom Semiconductor,Inc.
 * Davicom DM9051 SPI Fast Ethernet Linux driver
 */

#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/mii.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/phy.h>
#include <linux/regmap.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/spi/spi.h>

#include "dm9051.h"

/* spi master low level code */
static int hw_dm9051_spi_write(void *context, u8 reg, const u8 *data, size_t count)
{
	struct board_info *db = context;
	u8 cmd = DM_SPI_WR | reg;
	int ret;

	db->spi_transf[0].tx_buf = &cmd;
	db->spi_transf[0].rx_buf = NULL;
	db->spi_transf[0].len = 1;
	db->spi_transf[1].tx_buf = data;
	db->spi_transf[1].rx_buf = NULL;
	db->spi_transf[1].len = count;
	ret = spi_sync(db->spidev, &db->spi_msg);
	if (ret < 0)
		dev_err(&db->spidev->dev, "spi burst cmd 0x%02x, ret=%d\n", cmd, ret);
	return ret;
}

static int hw_dm9051_spi_read(void *context, u8 reg, u8 *data, size_t count)
{
	struct board_info *db = context;
	u8 cmd = DM_SPI_RD | reg;

	return spi_write_then_read(db->spidev, &cmd, sizeof(cmd), data, count);
}

static int regmap_dm9051_reg_write(void *context, const void *data, size_t len)
{
	u8 *dout = (u8 *)data;
	u8 reg = dout[0];

	++dout;
	--len;
	return hw_dm9051_spi_write(context, reg, dout, len);
}

static int regmap_dm9051_reg_read(void *context, const void *reg_buf, size_t reg_size,
				  void *val, size_t val_size)
{
	u8 reg = *(const u8 *)reg_buf;

	if (reg_size != 1) {
		pr_err("%s: reg=%02x reg_size=%zu\n", __func__, reg, reg_size);
		return -EINVAL;
	}
	return hw_dm9051_spi_read(context, reg, val, val_size);
}

static int regmap_dm9051_reg_update_bits(void *context, unsigned int reg,
					 unsigned int mask,
					 unsigned int val)
{
	return 0;
}

static struct regmap_bus regmap_spi = {
	.write = regmap_dm9051_reg_write,
	.read = regmap_dm9051_reg_read,
	.reg_update_bits = regmap_dm9051_reg_update_bits,
};

static bool dm9051_regmap_readable(struct device *dev, unsigned int reg)
{
	return true;
}

static bool dm9051_regmap_writeable(struct device *dev, unsigned int reg)
{
	return true;
}

static bool dm9051_regmap_volatile(struct device *dev, unsigned int reg)
{
	return true; /* optional false */
}

static bool dm9051_regmap_precious(struct device *dev, unsigned int reg)
{
	return true; /* optional false */
}

static void regmap_lock_mutex(void *context)
{
	struct board_info *db = context;

	mutex_lock(&db->regmap_mutex);
}

static void regmap_unlock_mutex(void *context)
{
	struct board_info *db = context;

	mutex_unlock(&db->regmap_mutex);
}

static struct regmap_config regconfig = {
	.name = "reg",
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xff,
	.reg_stride = 1,
	.cache_type = REGCACHE_RBTREE,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
	.readable_reg = dm9051_regmap_readable,
	.writeable_reg = dm9051_regmap_writeable,
	.volatile_reg = dm9051_regmap_volatile,
	.precious_reg = dm9051_regmap_precious,
	.lock = regmap_lock_mutex,
	.unlock = regmap_unlock_mutex,
};

static int dm9051_map_poll(struct board_info *db)
{
	unsigned int mval;
	int ret;

	ret = read_poll_timeout(regmap_read, ret, !ret || !(mval & EPCR_ERRE),
				100, 10000, true, db->regmap, DM9051_EPCR, &mval);
	if (ret)
		netdev_err(db->ndev, "timeout in processing for phy/eeprom accessing\n");
	return ret;
}

static int regmap_dm9051_phy_reg_write(void *context, unsigned int reg, unsigned int val)
{
	struct board_info *db = context;
	int ret;

	regmap_write(db->regmap, DM9051_EPAR, DM9051_PHY | reg);
	regmap_write(db->regmap, DM9051_EPDRL, val & 0xff);
	regmap_write(db->regmap, DM9051_EPDRH, (val >> 8) && 0xff);
	regmap_write(db->regmap, DM9051_EPCR, EPCR_EPOS | EPCR_ERPRW);
	ret = dm9051_map_poll(db);
	regmap_write(db->regmap, DM9051_EPCR, 0x0);

	if (reg == MII_BMCR && !(val & 0x0800))
		mdelay(1); /* need for if activate phyxcer */

	return ret;
}

static int regmap_dm9051_phy_reg_read(void *context, unsigned int reg, unsigned int *val)
{
	struct board_info *db = context;
	unsigned int eph, epl;
	int ret;

	regmap_write(db->regmap, DM9051_EPAR, DM9051_PHY | reg);
	regmap_write(db->regmap, DM9051_EPCR, EPCR_ERPRR | EPCR_EPOS);
	ret = dm9051_map_poll(db);
	regmap_write(db->regmap, DM9051_EPCR, 0x0);
	regmap_read(db->regmap, DM9051_EPDRH, &eph);
	regmap_read(db->regmap, DM9051_EPDRL, &epl);
	*val = (eph << 8) | epl;
	return ret;
}

static struct regmap_bus phymap_mdio = {
	.reg_write = regmap_dm9051_phy_reg_write,
	.reg_read = regmap_dm9051_phy_reg_read,
};

static bool dm9051_phymap_readable(struct device *dev, unsigned int reg)
{
	return true;
}

static bool dm9051_phymap_writeable(struct device *dev, unsigned int reg)
{
	if (reg == MII_BMSR || reg == MII_PHYSID1 || reg == MII_PHYSID2)
		return false;
	return true;
}

static bool dm9051_phymap_volatile(struct device *dev, unsigned int reg)
{
	return true; /* optional false */
}

static struct regmap_config phyconfig = {
	.name = "phy",
	.reg_bits = 8,
	.val_bits = 16,
	.max_register = 0x1f,
	.cache_type = REGCACHE_RBTREE,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
	.readable_reg = dm9051_phymap_readable,
	.writeable_reg = dm9051_phymap_writeable,
	.volatile_reg = dm9051_phymap_volatile,
};

static int devm_regmap_init_dm9051(struct device *dev, struct board_info *db)
{
	mutex_init(&db->regmap_mutex);

	regconfig.lock_arg = db;

	db->regmap = devm_regmap_init(dev, &regmap_spi, db, &regconfig);
	if (IS_ERR(db->regmap))
		return PTR_ERR(db->regmap);
	db->phymap = devm_regmap_init(dev, &phymap_mdio, db, &phyconfig);
	if (IS_ERR(db->phymap))
		return PTR_ERR(db->phymap);
	return 0;
}

static u8 dm9051_map_read(struct board_info *db, u8 reg)
{
	struct net_device *ndev = db->ndev;
	unsigned int val = 0;
	int ret;

	ret = regmap_read(db->regmap, reg, &val); /* read only one byte */
	if (unlikely(ret))
		netif_err(db, drv, ndev, "%s: error %d reading reg %02x\n",
			  __func__, ret, reg);
	return val;
}

static void dm9051_map_write(struct board_info *db, u8 reg, u16 val)
{
	struct net_device *ndev = db->ndev;
	int ret = regmap_write(db->regmap, reg, val);

	if (unlikely(ret))
		netif_err(db, drv, ndev, "%s: error %d writing reg %02x=%04x\n",
			  __func__, ret, reg, val);
}

static int dm9051_direct_read(struct board_info *db, u8 reg, u8 *prxb)
{
	return hw_dm9051_spi_read(db, reg, prxb, 1);
}

static int dm9051_direct_write(struct board_info *db, u8 reg, u16 val)
{
	u8 txb = val;

	return hw_dm9051_spi_write(db, reg, &txb, 1);
}

static int dm9051_direct_inblk(struct board_info *db, u8 reg, u8 *data, size_t count)
{
	return hw_dm9051_spi_read(db, reg, data, count);
}

static int dm9051_direct_outblk(struct board_info *db, u8 reg, const u8 *data,
				size_t count)
{
	return hw_dm9051_spi_write(db, reg, data, count);
}

static int dm9051_dumpblk(struct board_info *db, unsigned int len)
{
	int ret;
	u8 rxb[1];

	while (len--) {
		ret = hw_dm9051_spi_read(db, DM_SPI_MRCMD, rxb, 1);
		if (ret < 0)
			return ret;
	}
	return ret;
}

static unsigned int dm9051_map_phyread(struct board_info *db, unsigned int reg)
{
	struct net_device *ndev = db->ndev;
	unsigned int val = 0;
	int ret;

	ret = regmap_read(db->phymap, reg, &val);
	if (unlikely(ret))
		netif_err(db, drv, ndev, "%s: error %d reading %02x\n",
			  __func__, ret, reg);
	return val;
}

static void dm9051_map_phywrite(struct board_info *db, unsigned int reg, unsigned int val)
{
	struct net_device *ndev = db->ndev;
	int ret;

	ret = regmap_write(db->phymap, reg, val);
	if (unlikely(ret))
		netif_err(db, drv, ndev, "%s: error %d writing reg %02x=%04x\n",
			  __func__, ret, reg, val);
}

static int dm9051_direct_poll(struct board_info *db)
{
	u8 check_val;
	int ret;

	ret = read_poll_timeout(dm9051_direct_read, ret, ret < 0 || !(check_val & EPCR_ERRE),
				100, 10000, true, db, DM9051_EPCR, &check_val);
	if (ret)
		netdev_err(db->ndev, "timeout in processing for phy accessing\n");
	return ret;
}

static int dm9051_direct_phyread(struct board_info *db, int reg, int *pvalue)
{
	u8 eph, epl;
	int ret;

	ret = dm9051_direct_write(db, DM9051_EPAR, DM9051_PHY | reg);
	if (ret < 0)
		return ret;
	ret = dm9051_direct_write(db, DM9051_EPCR, EPCR_ERPRR | EPCR_EPOS);
	if (ret < 0)
		return ret;

	ret = dm9051_direct_poll(db);
	if (ret)
		return ret;

	ret = dm9051_direct_write(db, DM9051_EPCR, 0x0);
	if (ret < 0)
		return ret;

	ret = dm9051_direct_read(db, DM9051_EPDRH, &eph);
	if (ret < 0)
		return ret;
	ret = dm9051_direct_read(db, DM9051_EPDRL, &epl);
	if (ret < 0)
		return ret;

	*pvalue = (eph << 8) | epl;
	return ret;
}

static int dm9051_direct_phywrite(struct board_info *db, int reg, int value)
{
	int ret;

	ret = dm9051_direct_write(db, DM9051_EPAR, DM9051_PHY | reg);
	if (ret < 0)
		return ret;
	ret = dm9051_direct_write(db, DM9051_EPDRL, value);
	if (ret < 0)
		return ret;
	ret = dm9051_direct_write(db, DM9051_EPDRH, value >> 8);
	if (ret < 0)
		return ret;
	ret = dm9051_direct_write(db, DM9051_EPCR, EPCR_EPOS | EPCR_ERPRW);
	if (ret < 0)
		return ret;

	ret = dm9051_direct_poll(db);
	if (ret)
		return ret;

	ret = dm9051_direct_write(db, DM9051_EPCR, 0x0);
	if (ret < 0)
		return ret;

	if (reg == MII_BMCR && !(value & 0x0800))
		mdelay(1); /* need for if activate phyxcer */

	return ret;
}

static int dm9051_read_eeprom(struct board_info *db, int offset, u8 *to)
{
	unsigned int mval;
	int ret;

	regmap_write(db->regmap, DM9051_EPAR, offset);
	regmap_write(db->regmap, DM9051_EPCR, EPCR_ERPRR);
	ret = dm9051_map_poll(db);
	regmap_write(db->regmap, DM9051_EPCR, 0x0);
	regmap_read(db->regmap, DM9051_EPDRL, &mval);
	to[0] = mval;
	regmap_read(db->regmap, DM9051_EPDRH, &mval);
	to[1] = mval;
	return ret;
}

static int dm9051_write_eeprom(struct board_info *db, int offset, u8 *data)
{
	int ret;

	regmap_write(db->regmap, DM9051_EPAR, offset);
	regmap_write(db->regmap, DM9051_EPDRH, data[1]);
	regmap_write(db->regmap, DM9051_EPDRL, data[0]);
	regmap_write(db->regmap, DM9051_EPCR, EPCR_WEP | EPCR_ERPRW);
	ret = dm9051_map_poll(db);
	regmap_write(db->regmap, DM9051_EPCR, 0);
	return ret;
}

static int dm9051_mdio_read(struct mii_bus *mdiobus, int phy_id, int reg)
{
	struct board_info *db = mdiobus->priv;
	int val, ret;

	if (phy_id == DM9051_PHY_ID) {
		mutex_lock(&db->addr_lock);
		ret = dm9051_direct_phyread(db, reg, &val);
		mutex_unlock(&db->addr_lock);
		if (ret)
			return ret;
		return val;
	}
	return 0xffff;
}

static int dm9051_mdio_write(struct mii_bus *mdiobus, int phy_id, int reg, u16 val)
{
	struct board_info *db = mdiobus->priv;
	int ret;

	if (phy_id == DM9051_PHY_ID) {
		mutex_lock(&db->addr_lock);
		ret = dm9051_direct_phywrite(db, reg, val);
		mutex_unlock(&db->addr_lock);
		if (ret)
			return ret;
		return 0;
	}
	return -ENODEV;
}

static unsigned int dm9051_chipid(struct board_info *db)
{
	struct device *dev = &db->spidev->dev;
	unsigned int wpidh, wpidl;
	u16 id = 0;

	regmap_read(db->regmap, DM9051_PIDH, &wpidh);
	regmap_read(db->regmap, DM9051_PIDL, &wpidl);

	id = (wpidh << 8) | wpidl;
	if (id == DM9051_ID) {
		dev_info(dev, "chip %04x found\n", id);
		return id;
	}
	dev_info(dev, "chipid error as %04x !\n", id);
	return id;
}

static int dm9051_direct_reset_code(struct board_info *db)
{
	int ret;

	mdelay(2); /* need before NCR_RST */
	ret = dm9051_direct_write(db, DM9051_NCR, NCR_RST); /* NCR reset */
	if (ret < 0)
		return ret;
	mdelay(1);
	ret = dm9051_direct_write(db, DM9051_MBNDRY, MBNDRY_BYTE); /* MemBound */
	if (ret < 0)
		return ret;
	mdelay(1);

	ret = dm9051_direct_write(db, DM9051_PPCR, PPCR_PAUSE_COUNT); /* Pause Count */
	if (ret < 0)
		return ret;
	ret = dm9051_direct_write(db, DM9051_LMCR, db->lcr_all); /* LEDMode1 */
	if (ret < 0)
		return ret;
	ret = dm9051_direct_write(db, DM9051_INTCR, INTCR_POL_LOW); /* INTCR */
	if (ret < 0)
		return ret;

	db->bc.DO_FIFO_RST_counter++;
	return ret;
}

static void dm9051_update_fcr(struct board_info *db, u8 fcr)
{
	u8 nfcr;

	mutex_lock(&db->addr_lock);
	nfcr = dm9051_map_read(db, DM9051_FCR);
	if (nfcr != fcr)
		dm9051_map_write(db, DM9051_FCR, fcr);
	mutex_unlock(&db->addr_lock);
}

static void dm9051_handle_link_change(struct net_device *ndev)
{
	struct board_info *db = to_dm9051_board(ndev);
	int lcl_adv, rmt_adv;
	u8 fcr = 0;

	phy_print_status(ndev->phydev);

	/* only write pause settings to mac. since mac and phy are integrated
	 * together, such as link state, speed and duplex are sync already
	 */
	if (ndev->phydev->link) {
		if (db->eth_pause.autoneg == AUTONEG_ENABLE) {
			lcl_adv = linkmode_adv_to_mii_adv_t(db->phydev->advertising);
			rmt_adv = linkmode_adv_to_mii_adv_t(db->phydev->lp_advertising);

			if (lcl_adv & rmt_adv & ADVERTISE_PAUSE_CAP) {
				db->eth_pause.rx_pause = true;
				db->eth_pause.tx_pause = true;
			}
		}

		/* while both rx_pause & tx_pause
		 * fcr will result idenical as FCR_RXTX_ENABLE
		 */
		if (db->eth_pause.rx_pause)
			fcr |= FCR_BKPM | FCR_FLCE;
		if (db->eth_pause.tx_pause)
			fcr |= FCR_TXPEN;

		dm9051_update_fcr(db, fcr);
	}
}

static int dm9051_phy_connect(struct board_info *db)
{
	char phy_id[MII_BUS_ID_SIZE + 3];

	snprintf(phy_id, MII_BUS_ID_SIZE + 3, PHY_ID_FMT,
		 db->mdiobus->id, DM9051_PHY_ID);
	db->phydev = phy_connect(db->ndev, phy_id, dm9051_handle_link_change,
				 PHY_INTERFACE_MODE_MII);

	if (IS_ERR(db->phydev))
		return PTR_ERR(db->phydev);
	return 0;
}

/* mac address is major from EEPROM
 */
static int dm9051_init_mac_addr(struct net_device *ndev, struct board_info *db)
{
	u8 addr[ETH_ALEN];
	int i;

	for (i = 0; i < ETH_ALEN; i++)
		addr[i] = dm9051_map_read(db, DM9051_PAR + i);

	if (is_valid_ether_addr(addr)) {
		eth_hw_addr_set(ndev, addr);
		return 0;
	}

	eth_hw_addr_random(ndev);
	dev_dbg(&db->spidev->dev, "Use random MAC address\n");
	return 0;
}

/* ethtool-ops
 */
static void
dm9051_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	strscpy(info->driver, DRVNAME_9051, sizeof(info->driver));
}

static void dm9051_set_msglevel(struct net_device *ndev, u32 value)
{
	struct board_info *db = to_dm9051_board(ndev);

	db->msg_enable = value;
}

static u32 dm9051_get_msglevel(struct net_device *ndev)
{
	struct board_info *db = to_dm9051_board(ndev);

	return db->msg_enable;
}

static int dm9051_get_eeprom_len(struct net_device *dev)
{
	return 128;
}

static int dm9051_get_eeprom(struct net_device *ndev,
			     struct ethtool_eeprom *ee, u8 *data)
{
	struct board_info *db = to_dm9051_board(ndev);
	int offset = ee->offset;
	int len = ee->len;
	int i, ret;

	if ((len & 1) != 0 || (offset & 1) != 0)
		return -EINVAL;

	ee->magic = DM_EEPROM_MAGIC;

	mutex_lock(&db->addr_lock);
	for (i = 0; i < len; i += 2) {
		ret = dm9051_read_eeprom(db, (offset + i) / 2, data + i);
		if (ret)
			break;
	}
	mutex_unlock(&db->addr_lock);
	return ret;
}

static int dm9051_set_eeprom(struct net_device *ndev,
			     struct ethtool_eeprom *ee, u8 *data)
{
	struct board_info *db = to_dm9051_board(ndev);
	int offset = ee->offset;
	int len = ee->len;
	int i, ret;

	if ((len | offset) & 1)
		return -EINVAL;

	if (ee->magic != DM_EEPROM_MAGIC)
		return -EINVAL;

	mutex_lock(&db->addr_lock);
	for (i = 0; i < len; i += 2) {
		ret = dm9051_write_eeprom(db, (offset + i) / 2, data + i);
		if (ret)
			break;
	}
	mutex_unlock(&db->addr_lock);
	return ret;
}

static void dm9051_get_pauseparam(struct net_device *ndev,
				  struct ethtool_pauseparam *pause)
{
	struct board_info *db = to_dm9051_board(ndev);

	*pause = db->eth_pause;
}

static int dm9051_set_pauseparam(struct net_device *ndev,
				 struct ethtool_pauseparam *pause)
{
	struct board_info *db = to_dm9051_board(ndev);
	u8 fcr = 0;
	int ret = 0;

	db->eth_pause = *pause;

	if (pause->autoneg)
		db->phydev->autoneg = AUTONEG_ENABLE;
	else
		db->phydev->autoneg = AUTONEG_DISABLE;

	if (pause->rx_pause)
		fcr |= FCR_BKPM | FCR_FLCE;
	if (pause->tx_pause)
		fcr |= FCR_TXPEN;

	dm9051_update_fcr(db, fcr);
	return ret;
}

static const struct ethtool_ops dm9051_ethtool_ops = {
	.get_drvinfo = dm9051_get_drvinfo,
	.get_link_ksettings = phy_ethtool_get_link_ksettings,
	.set_link_ksettings = phy_ethtool_set_link_ksettings,
	.get_msglevel = dm9051_get_msglevel,
	.set_msglevel = dm9051_set_msglevel,
	.nway_reset = phy_ethtool_nway_reset,
	.get_link = ethtool_op_get_link,
	.get_eeprom_len = dm9051_get_eeprom_len,
	.get_eeprom = dm9051_get_eeprom,
	.set_eeprom = dm9051_set_eeprom,
	.get_pauseparam = dm9051_get_pauseparam,
	.set_pauseparam = dm9051_set_pauseparam,
};

static void dm9051_operation_clear(struct board_info *db)
{
	db->bc.status_err_counter = 0;
	db->bc.large_err_counter = 0;
	db->bc.DO_FIFO_RST_counter = 0;
}

/* reset while rx error found
 */
static int dm9051_fifo_reset(struct board_info *db)
{
	struct net_device *ndev = db->ndev;
	int rxlen = le16_to_cpu(db->eth_rxhdr.rxlen);
	u8 fcr = 0;
	int ret;

	netdev_dbg(ndev, "rxhdr-byte (%02x)\n",
		   db->eth_rxhdr.rxpktready);
	netdev_dbg(ndev, "check rxstatus-eror (%02x)\n",
		   db->eth_rxhdr.rxstatus);
	netdev_dbg(ndev, "check rxlen large-eror (%d > %d)\n",
		   rxlen, DM9051_PKT_MAX);

	ret = dm9051_direct_reset_code(db);
	if (ret < 0)
		return ret;

	if (db->eth_pause.rx_pause)
		fcr |= FCR_BKPM | FCR_FLCE;
	if (db->eth_pause.tx_pause)
		fcr |= FCR_TXPEN;

	ret = dm9051_direct_write(db, DM9051_FCR, fcr);
	if (ret < 0)
		return ret;
	ret = dm9051_direct_write(db, DM9051_IMR, db->imr_all); /* rxp to 0xc00 */
	if (ret < 0)
		return ret;
	ret = dm9051_direct_write(db, DM9051_RCR, db->rcr_all); /* enable rx all */
	if (ret < 0)
		return ret;

	netdev_dbg(ndev, " rxstatus_Er & rxlen_Er %d, RST_c %d\n",
		   db->bc.status_err_counter + db->bc.large_err_counter,
		   db->bc.DO_FIFO_RST_counter);
	return ret;
}

/* read packets from the fifo memory
 * return value,
 *  > 0 - read packet number, caller can repeat the rx operation
 *    0 - no error, caller need stop further rx operation
 *  -EBUSY - read data error, caller escape from rx operation
 */
static int dm9051_loop_rx(struct board_info *db)
{
	u8 rxbyte;
	int ret, rxlen;
	struct sk_buff *skb;
	u8 *rdptr;
	int scanrr = 0;

	do {
		ret = dm9051_direct_read(db, DM_SPI_MRCMDX, &rxbyte);
		if (ret < 0) {
			netdev_err(db->ndev, "read MRCMDX fail\n");
			return ret;
		}
		ret = dm9051_direct_read(db, DM_SPI_MRCMDX, &rxbyte);
		if (ret < 0) {
			netdev_err(db->ndev, "read MRCMDX fail\n");
			return ret;
		}

		if (rxbyte != DM9051_PKT_RDY)
			break; /* exhaust-empty */

		ret = dm9051_direct_inblk(db, DM_SPI_RD | DM_SPI_MRCMD,
					  (u8 *)&db->eth_rxhdr, DM_RXHDR_SIZE);
		if (ret < 0)
			return ret;

		ret = dm9051_direct_write(db, DM9051_ISR, 0xff); /* to stop mrcmd */
		if (ret < 0)
			return ret;

		rxlen = le16_to_cpu(db->eth_rxhdr.rxlen);
		if (db->eth_rxhdr.rxstatus & 0xbf || rxlen > DM9051_PKT_MAX) {
			if (db->eth_rxhdr.rxstatus & 0xbf)
				db->bc.status_err_counter++;
			else
				db->bc.large_err_counter++;
			ret = dm9051_fifo_reset(db);
			if (ret < 0)
				return ret;
			return 0;
		}

		skb = dev_alloc_skb(rxlen);
		if (!skb) {
			ret = dm9051_dumpblk(db, rxlen);
			if (ret < 0)
				return ret;
			return scanrr;
		}

		rdptr = (u8 *)skb_put(skb, rxlen - 4);
		ret = dm9051_direct_inblk(db, DM_SPI_RD | DM_SPI_MRCMD, rdptr, rxlen);
		if (ret < 0) {
			dev_kfree_skb(skb);
			return ret;
		}

		ret = dm9051_direct_write(db, DM9051_ISR, 0xff); /* to stop mrcmd */
		if (ret < 0)
			return ret;

		skb->protocol = eth_type_trans(skb, db->ndev);
		if (db->ndev->features & NETIF_F_RXCSUM)
			skb_checksum_none_assert(skb);
		netif_rx_ni(skb);
		db->ndev->stats.rx_bytes += rxlen;
		db->ndev->stats.rx_packets++;
		scanrr++;
	} while (!ret);
	return scanrr;
}

static int dm9051_xmit_poll(struct board_info *db)
{
	int ret;
	u8 check_val;

	/* shorter by waiting tx-end rather than tx-req */
	ret = read_poll_timeout(dm9051_direct_read, ret,
				ret < 0 || check_val & (NSR_TX2END | NSR_TX1END),
				1, 20, false, db, DM9051_NSR, &check_val);
	if (ret)
		netdev_err(db->ndev, "timeout transmit packet\n");
	return ret;
}

/* transmit a packet,
 * return value,
 *   0 - succeed
 *  -ETIMEDOUT - timeout error
 */
static int dm9051_single_tx(struct board_info *db, u8 *buff, unsigned int len)
{
	int ret;

	ret = dm9051_xmit_poll(db);
	if (ret)
		return -ETIMEDOUT;

	ret = dm9051_direct_outblk(db, DM_SPI_MWCMD, buff, len);
	if (ret < 0)
		return ret;

	ret = dm9051_direct_write(db, DM9051_TXPLL, len);
	if (ret < 0)
		return ret;
	ret = dm9051_direct_write(db, DM9051_TXPLH, len >> 8);
	if (ret < 0)
		return ret;
	ret = dm9051_direct_write(db, DM9051_TCR, TCR_TXREQ);
	if (ret < 0)
		return ret;
	return 0;
}

static int dm9051_loop_tx(struct board_info *db)
{
	struct net_device *ndev = db->ndev;
	int ntx = 0;
	int ret;

	while (!skb_queue_empty(&db->txq)) {
		struct sk_buff *skb;

		skb = skb_dequeue(&db->txq);
		if (skb) {
			ntx++;
			ret = dm9051_single_tx(db, skb->data, skb->len);
			dev_kfree_skb(skb);
			if (ret < 0)
				return 0;
			ndev->stats.tx_bytes += skb->len;
			ndev->stats.tx_packets++;
		}

		if (netif_queue_stopped(ndev) &&
		    (skb_queue_len(&db->txq) < DM9051_TX_QUE_LO_WATER))
			netif_wake_queue(ndev);
	}
	return ntx;
}

/* end with enable the interrupt mask
 */
static irqreturn_t dm9051_rx_threaded_irq(int irq, void *pw)
{
	struct board_info *db = pw;
	int result, resul_tx;

	mutex_lock(&db->spi_lock); /* mutex essential */
	mutex_lock(&db->addr_lock);
	if (netif_carrier_ok(db->ndev)) {
		result = dm9051_direct_write(db, DM9051_IMR, IMR_PAR); /* disable imr */
		if (result < 0)
			goto spi_err;

		do {
			result = dm9051_loop_rx(db); /* threaded irq rx */
			if (result < 0)
				goto spi_err;
			resul_tx = dm9051_loop_tx(db); /* more tx better performance */
			if (resul_tx < 0)
				goto spi_err;
		} while (result > 0);

		result = dm9051_direct_write(db, DM9051_IMR, db->imr_all); /* enable imr */
		if (result < 0)
			goto spi_err;
	}
spi_err:
	mutex_unlock(&db->addr_lock);
	mutex_unlock(&db->spi_lock); /* mutex essential */
	return IRQ_HANDLED;
}

static void dm9051_tx_delay(struct kthread_work *ws)
{
	struct board_info *db = container_of(ws, struct board_info, kw_tx);
	int result;

	mutex_lock(&db->spi_lock); /* mutex essential */
	mutex_lock(&db->addr_lock);
	result = dm9051_loop_tx(db);
	if (result < 0)
		netdev_err(db->ndev, "transmit packet error\n");
	mutex_unlock(&db->addr_lock);
	mutex_unlock(&db->spi_lock); /* mutex essential */
}

static void dm9051_rxctl_delay(struct kthread_work *ws)
{
	struct board_info *db = container_of(ws, struct board_info, kw_rxctrl);
	struct net_device *ndev = db->ndev;
	int i, oft, result;

	mutex_lock(&db->addr_lock);
	for (i = 0, oft = DM9051_PAR; i < ETH_ALEN; i++, oft++) {
		result = dm9051_direct_write(db, oft, ndev->dev_addr[i]);
		if (result < 0)
			goto spi_err;
	}

	for (i = 0, oft = DM9051_MAR; i < 4; i++) {
		result = dm9051_direct_write(db, oft++, db->hash_table[i]);
		if (result < 0)
			goto spi_err;
		result = dm9051_direct_write(db, oft++, db->hash_table[i] >> 8);
		if (result < 0)
			goto spi_err;
	}

	dm9051_direct_write(db, DM9051_RCR, db->rcr_all); /* rx enable */
spi_err:
	mutex_unlock(&db->addr_lock);
}

static void dm9051_map_phyup(struct board_info *db)
{
	unsigned int val;

	val = dm9051_map_phyread(db, MII_BMCR);
	dm9051_map_phywrite(db, MII_BMCR, val & ~0x0800); /* BMCR Power-up PHY */

	dm9051_map_write(db, DM9051_GPR, 0); /* GPR Power-up PHY */
	mdelay(1); /* need for activate phyxcer */
}

static void dm9051_map_phydown(struct board_info *db)
{
	dm9051_map_write(db, DM9051_GPR, 0x01); /* Power-Down PHY */
}

static void dm9051_map_reset_code(struct board_info *db)
{
	mdelay(2); /* need before NCR_RST */
	dm9051_map_write(db, DM9051_NCR, NCR_RST);
	mdelay(1);
	dm9051_map_write(db, DM9051_MBNDRY, MBNDRY_BYTE); /* MemBound */
	mdelay(1);

	dm9051_map_write(db, DM9051_PPCR, PPCR_PAUSE_COUNT); /* Pause Pkt Count */
	dm9051_map_write(db, DM9051_LMCR, db->lcr_all); /* LEDMode1 */
	dm9051_map_write(db, DM9051_INTCR, INTCR_POL_LOW); /* INTCR */
	db->bc.DO_FIFO_RST_counter++;
}

static void dm9051_all_start(struct board_info *db)
{
	mutex_lock(&db->addr_lock); /* Note: must */
	dm9051_map_phyup(db);
	dm9051_map_reset_code(db);
	dm9051_map_write(db, DM9051_IMR, db->imr_all); /* phyup, rxp to 0xc00 */
	mutex_unlock(&db->addr_lock);
}

static void dm9051_all_stop(struct board_info *db)
{
	mutex_lock(&db->addr_lock);
	dm9051_map_phydown(db);
	dm9051_map_write(db, DM9051_RCR, RCR_RX_DISABLE); /* rx disable */
	mutex_unlock(&db->addr_lock);
}

/* Open network device
 * Called when the network device is marked active, such as a user executing
 * 'ifconfig up' on the device
 */
static int dm9051_open(struct net_device *ndev)
{
	struct board_info *db = to_dm9051_board(ndev);
	struct spi_device *spi = db->spidev;
	int ret;

	netif_wake_queue(ndev);

	ndev->irq = spi->irq; /* by dts */
	ret = request_threaded_irq(spi->irq, NULL, dm9051_rx_threaded_irq,
				   IRQF_TRIGGER_LOW | IRQF_ONESHOT,
				   ndev->name, db);
	if (ret < 0) {
		netdev_err(ndev, "failed to get irq\n");
		return ret;
	}

	db->imr_all = IMR_PAR | IMR_PRM;
	db->rcr_all = RCR_DIS_LONG | RCR_DIS_CRC | RCR_RXEN;
	db->lcr_all = LMCR_MODE1;

	/* init pause param FlowCtrl */
	db->eth_pause.rx_pause = true;
	db->eth_pause.tx_pause = true;
	db->eth_pause.autoneg = AUTONEG_ENABLE;

	/* We may have start with auto negotiation */
	db->phydev->autoneg = AUTONEG_ENABLE;
	db->phydev->speed = 0;
	db->phydev->duplex = 0;

	dm9051_all_start(db);

	phy_support_sym_pause(db->phydev); /* Enable support of sym pause */
	phy_start(db->phydev); /* it enclose with mutex_lock/mutex_unlock */
	return 0;
}

/* Close network device
 * Called to close down a network device which has been active. Cancel any
 * work, shutdown the RX and TX process and then place the chip into a low
 * power state while it is not being used
 */
static int dm9051_stop(struct net_device *ndev)
{
	struct board_info *db = to_dm9051_board(ndev);

	free_irq(db->spidev->irq, db);
	netif_stop_queue(ndev);

	phy_stop(db->phydev);
	dm9051_all_stop(db);
	return 0;
}

/* event: play a schedule starter in condition
 */
static netdev_tx_t dm9051_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct board_info *db = to_dm9051_board(ndev);

	skb_queue_tail(&db->txq, skb); /* add to skb */
	if (skb_queue_len(&db->txq) > DM9051_TX_QUE_HI_WATER)
		netif_stop_queue(ndev); /* enforce limit queue size */
	kthread_queue_work(&db->kw, &db->kw_tx);
	return NETDEV_TX_OK;
}

/* event: play with a schedule starter
 */
static void dm9051_set_multicast_list_schedule(struct net_device *ndev)
{
	struct board_info *db = to_dm9051_board(ndev);
	u8 rcr = RCR_DIS_LONG | RCR_DIS_CRC | RCR_RXEN;
	struct netdev_hw_addr *ha;
	u32 hash_val;

	/* rx control */
	if (ndev->flags & IFF_PROMISC) {
		rcr |= RCR_PRMSC;
		netdev_dbg(ndev, "set_multicast rcr |= RCR_PRMSC, rcr= %02x\n", rcr);
	}

	if (ndev->flags & IFF_ALLMULTI) {
		rcr |= RCR_ALL;
		netdev_dbg(ndev, "set_multicast rcr |= RCR_ALLMULTI, rcr= %02x\n", rcr);
	}

	db->rcr_all = rcr;

	/* broadcast address */
	db->hash_table[0] = 0;
	db->hash_table[1] = 0;
	db->hash_table[2] = 0;
	db->hash_table[3] = 0x8000;

	/* the multicast address in Hash Table : 64 bits */
	netdev_for_each_mc_addr(ha, ndev) {
		hash_val = ether_crc_le(ETH_ALEN, ha->addr) & 0x3f;
		db->hash_table[hash_val / 16] |= (u16)1 << (hash_val % 16);
	}

	kthread_queue_work(&db->kw, &db->kw_rxctrl);
}

/* event: write into the mac registers and eeprom directly
 */
static int dm9051_set_mac_address(struct net_device *ndev, void *p)
{
	struct board_info *db = to_dm9051_board(ndev);
	int i, oft, ret;

	ret = eth_mac_addr(ndev, p);
	if (ret < 0)
		return ret;

	/* write to chip */
	mutex_lock(&db->addr_lock);
	for (i = 0, oft = DM9051_PAR; i < ETH_ALEN; i++, oft++)
		dm9051_map_write(db, oft, ndev->dev_addr[i]);
	mutex_unlock(&db->addr_lock);
	return ret;
}

/* netdev-ops
 */
static const struct of_device_id dm9051_match_table[] = {
	{ .compatible = "davicom,dm9051", },
	{}
};

static const struct spi_device_id dm9051_id_table[] = {
	{ "dm9051", 0 },
	{}
};

static
const struct net_device_ops dm9051_netdev_ops = {
	.ndo_open = dm9051_open,
	.ndo_stop = dm9051_stop,
	.ndo_start_xmit = dm9051_start_xmit,
	.ndo_set_rx_mode = dm9051_set_multicast_list_schedule,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_set_mac_address = dm9051_set_mac_address,
};

/* probe subs
 */
static void dm9051_netdev(struct net_device *ndev)
{
	ndev->mtu = 1500;
	ndev->if_port = IF_PORT_100BASET;
	ndev->netdev_ops = &dm9051_netdev_ops;
	ndev->ethtool_ops = &dm9051_ethtool_ops;
}

static void dm9051_spimsg_addtail(struct board_info *db)
{
	memset(&db->spi_transf, 0, sizeof(struct spi_transfer) * 2);
	spi_message_init(&db->spi_msg);
	spi_message_add_tail(&db->spi_transf[0], &db->spi_msg);
	spi_message_add_tail(&db->spi_transf[1], &db->spi_msg);
}

static int dm9051_register_mdiobus(struct board_info *db)
{
	struct spi_device *spi = db->spidev;
	int ret;

	db->mdiobus = devm_mdiobus_alloc(&spi->dev);
	if (!db->mdiobus)
		return -ENOMEM;

	db->mdiobus->priv = db;
	db->mdiobus->read = dm9051_mdio_read;
	db->mdiobus->write = dm9051_mdio_write;
	db->mdiobus->name = "dm9051-mdiobus";
	db->mdiobus->phy_mask = (u32)~BIT(1);
	db->mdiobus->parent = &spi->dev;
	snprintf(db->mdiobus->id, MII_BUS_ID_SIZE,
		 "dm9051-%s.%u", dev_name(&spi->dev), spi->chip_select);

	ret = devm_mdiobus_register(&spi->dev, db->mdiobus);
	if (ret < 0) {
		dev_err(&spi->dev, "Could not register MDIO bus\n");
		return ret;
	}
	return 0;
}

static int dm9051_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct net_device *ndev;
	struct board_info *db;
	unsigned int id;
	int ret = 0;

	ndev = devm_alloc_etherdev(dev, sizeof(struct board_info));
	if (!ndev)
		return -ENOMEM;

	SET_NETDEV_DEV(ndev, dev);
	dev_set_drvdata(dev, ndev);
	db = netdev_priv(ndev);
	db->msg_enable = 0;
	db->spidev = spi;
	db->ndev = ndev;
	dm9051_netdev(ndev);

	dm9051_spimsg_addtail(db);

	mutex_init(&db->spi_lock);
	mutex_init(&db->addr_lock);

	kthread_init_worker(&db->kw); /* kthread */
	kthread_init_work(&db->kw_rxctrl, dm9051_rxctl_delay);
	kthread_init_work(&db->kw_tx, dm9051_tx_delay);

	db->kwr_task_kw = kthread_run(kthread_worker_fn, &db->kw, "dm9051");
	if (IS_ERR(db->kwr_task_kw))
		return PTR_ERR(db->kwr_task_kw);

	ret = devm_regmap_init_dm9051(dev, db);
	if (ret)
		goto err_stopthread;

	id = dm9051_chipid(db);
	if (id != DM9051_ID) {
		ret = -ENODEV;
		goto err_stopthread;
	}

	ret = dm9051_init_mac_addr(ndev, db);
	if (ret < 0)
		goto err_stopthread;

	ret = dm9051_register_mdiobus(db); /* init mdiobus */
	if (ret)
		goto err_stopthread;

	ret = dm9051_phy_connect(db); /* phy connect as poll mode */
	if (ret)
		goto err_stopthread;

	dm9051_operation_clear(db);
	ret = devm_register_netdev(dev, ndev);
	if (ret) {
		dev_err(dev, "failed to register network device\n");
		goto err_netdev;
	}

	skb_queue_head_init(&db->txq);

	return 0;

err_netdev:
	phy_disconnect(db->phydev);
err_stopthread:
	kthread_stop(db->kwr_task_kw);
	return ret;
}

static int dm9051_drv_remove(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct net_device *ndev = dev_get_drvdata(dev);
	struct board_info *db = to_dm9051_board(ndev);

	phy_disconnect(db->phydev);
	kthread_stop(db->kwr_task_kw);
	return 0;
}

static struct spi_driver dm9051_driver = {
	.driver = {
		.name = DRVNAME_9051,
		.of_match_table = dm9051_match_table,
	},
	.probe = dm9051_probe,
	.remove = dm9051_drv_remove,
	.id_table = dm9051_id_table,
};
module_spi_driver(dm9051_driver);

MODULE_AUTHOR("Joseph CHANG <joseph_chang@davicom.com.tw>");
MODULE_DESCRIPTION("Davicom DM9051 network SPI driver");
MODULE_LICENSE("GPL");
