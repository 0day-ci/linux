// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for Qualcomm QCA808x PHY
 * Author: Luo Jie <luoj@codeaurora.org>
 */

#include <linux/module.h>
#include <linux/etherdevice.h>
#include <linux/phy.h>
#include <linux/bitfield.h>

#define QCA8081_PHY_ID					0x004DD101

/* MII special status */
#define QCA808X_PHY_SPEC_STATUS				0x11
#define QCA808X_STATUS_FULL_DUPLEX			BIT(13)
#define QCA808X_STATUS_LINK_PASS			BIT(10)
#define QCA808X_STATUS_SPEED_MASK			GENMASK(9, 7)
#define QCA808X_STATUS_SPEED_100MBS			1
#define QCA808X_STATUS_SPEED_1000MBS			2
#define QCA808X_STATUS_SPEED_2500MBS			4

/* MII interrupt enable & status */
#define QCA808X_PHY_INTR_MASK				0x12
#define QCA808X_PHY_INTR_STATUS				0x13
#define QCA808X_INTR_ENABLE_FAST_RETRAIN_FAIL		BIT(15)
#define QCA808X_INTR_ENABLE_SPEED_CHANGED		BIT(14)
#define QCA808X_INTR_ENABLE_DUPLEX_CHANGED		BIT(13)
#define QCA808X_INTR_ENABLE_PAGE_RECEIVED		BIT(12)
#define QCA808X_INTR_ENABLE_LINK_FAIL			BIT(11)
#define QCA808X_INTR_ENABLE_LINK_SUCCESS		BIT(10)
#define QCA808X_INTR_ENABLE_POE				BIT(1)
#define QCA808X_INTR_ENABLE_WOL				BIT(0)

/* MII DBG address & data */
#define QCA808X_PHY_DEBUG_ADDR				0x1d
#define QCA808X_PHY_DEBUG_DATA				0x1e

/* Conifg seed */
#define QCA808X_PHY_DEBUG_LOCAL_SEED			9
#define QCA808X_MASTER_SLAVE_SEED_ENABLE		BIT(1)
#define QCA808X_MASTER_SLAVE_SEED_CFG			GENMASK(12, 2)
#define QCA808X_MASTER_SLAVE_SEED_RANGE			0x32

/* ADC threshold */
#define QCA808X_PHY_DEBUG_ADC_THRESHOLD			0x2c80
#define QCA808X_ADC_THRESHOLD_MASK			GENMASK(7, 0)
#define QCA808X_ADC_THRESHOLD_80MV			0
#define QCA808X_ADC_THRESHOLD_100MV			0xf0
#define QCA808X_ADC_THRESHOLD_200MV			0x0f
#define QCA808X_ADC_THRESHOLD_300MV			0xff

/* PMA control */
#define QCA808X_PHY_MMD1_PMA_CONTROL			0x0
#define QCA808X_PMA_CONTROL_SPEED_MASK			(BIT(13) | BIT(6))
#define QCA808X_PMA_CONTROL_2500M			(BIT(13) | BIT(6))
#define QCA808X_PMA_CONTROL_1000M			BIT(6)
#define QCA808X_PMA_CONTROL_100M			BIT(13)
#define QCA808X_PMA_CONTROL_10M				0x0

/* PMA capable */
#define QCA808X_PHY_MMD1_PMA_CAP_REG			0x4
#define QCA808X_STATUS_2500T_FD_CAPS			BIT(13)

/* PMA type */
#define QCA808X_PHY_MMD1_PMA_TYPE			0x7
#define QCA808X_PMA_TYPE_MASK				GENMASK(5, 0)
#define QCA808X_PMA_TYPE_2500M				0x30
#define QCA808X_PMA_TYPE_1000M				0xc
#define QCA808X_PMA_TYPE_100M				0xe
#define QCA808X_PMA_TYPE_10M				0xf

/* CLD control */
#define QCA808X_PHY_MMD3_ADDR_CLD_CTRL7			0x8007
#define QCA808X_8023AZ_AFE_CTRL_MASK			GENMASK(8, 4)
#define QCA808X_8023AZ_AFE_EN				0x90

/* AZ control */
#define QCA808X_PHY_MMD3_AZ_TRAINING_CTRL		0x8008
#define QCA808X_MMD3_AZ_TRAINING_VAL			0x1c32

/* WOL control */
#define QCA808X_PHY_MMD3_WOL_CTRL			0x8012
#define QCA808X_WOL_EN					BIT(5)

#define QCA808X_PHY_MMD3_LOCAL_MAC_ADDR_0_15_OFFSET	0x804c
#define QCA808X_PHY_MMD3_LOCAL_MAC_ADDR_16_31_OFFSET	0x804b
#define QCA808X_PHY_MMD3_LOCAL_MAC_ADDR_32_47_OFFSET	0x804a

/* AN 2.5G */
#define QCA808X_PHY_MMD7_AUTONEGOTIATION_CONTROL	0x20
#define QCA808X_ADVERTISE_2500FULL			BIT(7)
#define QCA808X_FAST_RETRAIN_2500BT			BIT(5)
#define QCA808X_ADV_LOOP_TIMING				BIT(0)

/* Fast retrain related registers */
#define QCA808X_PHY_MMD1_FAST_RETRAIN_STATUS_CTL	0x93
#define QCA808X_FAST_RETRAIN_CTRL			0x1

#define QCA808X_PHY_MMD1_MSE_THRESHOLD_20DB		0x8014
#define QCA808X_MSE_THRESHOLD_20DB_VALUE		0x529

#define QCA808X_PHY_MMD1_MSE_THRESHOLD_17DB		0x800E
#define QCA808X_MSE_THRESHOLD_17DB_VALUE		0x341

#define QCA808X_PHY_MMD1_MSE_THRESHOLD_27DB		0x801E
#define QCA808X_MSE_THRESHOLD_27DB_VALUE		0x419

#define QCA808X_PHY_MMD1_MSE_THRESHOLD_28DB		0x8020
#define QCA808X_MSE_THRESHOLD_28DB_VALUE		0x341

#define QCA808X_PHY_MMD7_TOP_OPTION1			0x901c
#define QCA808X_TOP_OPTION1_DATA			0x0

#define QCA808X_PHY_MMD7_ADDR_EEE_LP_ADVERTISEMENT	0x40
#define QCA808X_EEE_ADV_THP				0x8

#define QCA808X_PHY_MMD3_DEBUG_1			0xa100
#define QCA808X_MMD3_DEBUG_1_VALUE			0x9203
#define QCA808X_PHY_MMD3_DEBUG_2			0xa101
#define QCA808X_MMD3_DEBUG_2_VALUE			0x48ad
#define QCA808X_PHY_MMD3_DEBUG_3			0xa103
#define QCA808X_MMD3_DEBUG_3_VALUE			0x1698
#define QCA808X_PHY_MMD3_DEBUG_4			0xa105
#define QCA808X_MMD3_DEBUG_4_VALUE			0x8001
#define QCA808X_PHY_MMD3_DEBUG_5			0xa106
#define QCA808X_MMD3_DEBUG_5_VALUE			0x1111
#define QCA808X_PHY_MMD3_DEBUG_6			0xa011
#define QCA808X_MMD3_DEBUG_6_VALUE			0x5f85

static int qca808x_debug_reg_read(struct phy_device *phydev, u16 reg)
{
	int ret;

	ret = phy_write(phydev, QCA808X_PHY_DEBUG_ADDR, reg);
	if (ret)
		return ret;

	return phy_read(phydev, QCA808X_PHY_DEBUG_DATA);
}

static int qca808x_debug_reg_modify(struct phy_device *phydev, u16 reg,
				 u16 mask, u16 val)
{
	u16 phy_data;
	int ret;

	ret = qca808x_debug_reg_read(phydev, reg);
	if (ret < 0)
		return ret;

	phy_data = ret & 0xffff;
	phy_data &= ~mask;
	phy_data |= val;

	return phy_write(phydev, QCA808X_PHY_DEBUG_DATA, phy_data);
}

static int qca808x_get_2500caps(struct phy_device *phydev)
{
	int phy_data;

	phy_data = phy_read_mmd(phydev, MDIO_MMD_PMAPMD, QCA808X_PHY_MMD1_PMA_CAP_REG);

	return (phy_data & QCA808X_STATUS_2500T_FD_CAPS) ? 1 : 0;
}

static int qca808x_get_features(struct phy_device *phydev)
{
	linkmode_mod_bit(ETHTOOL_LINK_MODE_2500baseT_Full_BIT, phydev->supported,
			qca808x_get_2500caps(phydev));

	return genphy_read_abilities(phydev);
}

static int qca808x_phy_fast_retrain_cfg(struct phy_device *phydev)
{
	int ret;

	ret = phy_write_mmd(phydev, MDIO_MMD_AN, QCA808X_PHY_MMD7_AUTONEGOTIATION_CONTROL,
			QCA808X_ADVERTISE_2500FULL |
			QCA808X_FAST_RETRAIN_2500BT |
			QCA808X_ADV_LOOP_TIMING);
	if (ret)
		return ret;

	phy_write_mmd(phydev, MDIO_MMD_PMAPMD, QCA808X_PHY_MMD1_FAST_RETRAIN_STATUS_CTL,
			QCA808X_FAST_RETRAIN_CTRL);
	phy_write_mmd(phydev, MDIO_MMD_PMAPMD, QCA808X_PHY_MMD1_MSE_THRESHOLD_20DB,
			QCA808X_MSE_THRESHOLD_20DB_VALUE);
	phy_write_mmd(phydev, MDIO_MMD_PMAPMD, QCA808X_PHY_MMD1_MSE_THRESHOLD_17DB,
			QCA808X_MSE_THRESHOLD_17DB_VALUE);
	phy_write_mmd(phydev, MDIO_MMD_PMAPMD, QCA808X_PHY_MMD1_MSE_THRESHOLD_27DB,
			QCA808X_MSE_THRESHOLD_27DB_VALUE);
	phy_write_mmd(phydev, MDIO_MMD_PMAPMD, QCA808X_PHY_MMD1_MSE_THRESHOLD_28DB,
			QCA808X_MSE_THRESHOLD_28DB_VALUE);
	phy_write_mmd(phydev, MDIO_MMD_AN, QCA808X_PHY_MMD7_ADDR_EEE_LP_ADVERTISEMENT,
			QCA808X_EEE_ADV_THP);
	phy_write_mmd(phydev, MDIO_MMD_AN, QCA808X_PHY_MMD7_TOP_OPTION1,
			QCA808X_TOP_OPTION1_DATA);
	phy_write_mmd(phydev, MDIO_MMD_PCS, QCA808X_PHY_MMD3_DEBUG_1,
			QCA808X_MMD3_DEBUG_1_VALUE);
	phy_write_mmd(phydev, MDIO_MMD_PCS, QCA808X_PHY_MMD3_DEBUG_4,
			QCA808X_MMD3_DEBUG_4_VALUE);
	phy_write_mmd(phydev, MDIO_MMD_PCS, QCA808X_PHY_MMD3_DEBUG_5,
			QCA808X_MMD3_DEBUG_5_VALUE);
	phy_write_mmd(phydev, MDIO_MMD_PCS, QCA808X_PHY_MMD3_DEBUG_3,
			QCA808X_MMD3_DEBUG_3_VALUE);
	phy_write_mmd(phydev, MDIO_MMD_PCS, QCA808X_PHY_MMD3_DEBUG_6,
			QCA808X_MMD3_DEBUG_6_VALUE);
	phy_write_mmd(phydev, MDIO_MMD_PCS, QCA808X_PHY_MMD3_DEBUG_2,
			QCA808X_MMD3_DEBUG_2_VALUE);

	return 0;
}

static int qca808x_phy_ms_random_seed_set(struct phy_device *phydev)
{
	u16 seed_value = (prandom_u32() % QCA808X_MASTER_SLAVE_SEED_RANGE) << 2;

	return qca808x_debug_reg_modify(phydev, QCA808X_PHY_DEBUG_LOCAL_SEED,
			QCA808X_MASTER_SLAVE_SEED_CFG, seed_value);
}

static int qca808x_phy_ms_seed_enable(struct phy_device *phydev, bool enable)
{
	u16 seed_enable = 0;

	if (enable)
		seed_enable = QCA808X_MASTER_SLAVE_SEED_ENABLE;

	return qca808x_debug_reg_modify(phydev, QCA808X_PHY_DEBUG_LOCAL_SEED,
			QCA808X_MASTER_SLAVE_SEED_ENABLE, seed_enable);
}

static int qca808x_config_init(struct phy_device *phydev)
{
	int ret;

	/* Active adc&vga on 802.3az for the link 1000M and 100M */
	ret = phy_modify_mmd(phydev, MDIO_MMD_PCS, QCA808X_PHY_MMD3_ADDR_CLD_CTRL7,
			QCA808X_8023AZ_AFE_CTRL_MASK, QCA808X_8023AZ_AFE_EN);
	if (ret)
		return ret;

	/* Adjust the threshold on 802.3az for the link 1000M */
	ret = phy_write_mmd(phydev, MDIO_MMD_PCS,
			QCA808X_PHY_MMD3_AZ_TRAINING_CTRL, QCA808X_MMD3_AZ_TRAINING_VAL);
	if (ret)
		return ret;

	/* Config the fast retrain for the link 2500M */
	ret = qca808x_phy_fast_retrain_cfg(phydev);
	if (ret)
		return ret;

	/* Configure ramdom seed to make phy linked as slave mode for link 2500M */
	ret = qca808x_phy_ms_random_seed_set(phydev);
	if (ret)
		return ret;

	/* Enable seed */
	ret = qca808x_phy_ms_seed_enable(phydev, true);
	if (ret)
		return ret;

	/* Configure adc threshold as 100mv for the link 10M */
	return qca808x_debug_reg_modify(phydev, QCA808X_PHY_DEBUG_ADC_THRESHOLD,
			QCA808X_ADC_THRESHOLD_MASK, QCA808X_ADC_THRESHOLD_100MV);
}

static int qca808x_ack_interrupt(struct phy_device *phydev)
{
	int ret;

	ret = phy_read(phydev, QCA808X_PHY_INTR_STATUS);

	return (ret < 0) ? ret : 0;
}

static int qca808x_config_intr(struct phy_device *phydev)
{
	int ret;
	int intr_ctl = 0;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		ret = qca808x_ack_interrupt(phydev);
		if (ret)
			return ret;

		intr_ctl = phy_read(phydev, QCA808X_PHY_INTR_MASK);
		if (intr_ctl < 0)
			return intr_ctl;

		intr_ctl |= QCA808X_INTR_ENABLE_FAST_RETRAIN_FAIL |
			QCA808X_INTR_ENABLE_SPEED_CHANGED |
			QCA808X_INTR_ENABLE_DUPLEX_CHANGED |
			QCA808X_INTR_ENABLE_LINK_FAIL |
			QCA808X_INTR_ENABLE_LINK_SUCCESS;
		ret = phy_write(phydev, QCA808X_PHY_INTR_MASK, intr_ctl);
	} else {
		ret = phy_write(phydev, QCA808X_PHY_INTR_MASK, intr_ctl);
		if (ret)
			return ret;

		ret = qca808x_ack_interrupt(phydev);
	}

	return ret;
}

static irqreturn_t qca808x_handle_interrupt(struct phy_device *phydev)
{
	int intr_status, intr_mask;

	intr_status = phy_read(phydev, QCA808X_PHY_INTR_STATUS);
	if (intr_status < 0) {
		phy_error(phydev);
		return IRQ_NONE;
	}

	intr_mask = phy_read(phydev, QCA808X_PHY_INTR_MASK);
	if (intr_mask < 0) {
		phy_error(phydev);
		return IRQ_NONE;
	}

	if (!(intr_status & intr_mask))
		return IRQ_NONE;

	phy_start_machine(phydev);

	return IRQ_HANDLED;
}

static int qca808x_set_wol(struct phy_device *phydev, struct ethtool_wolinfo *wol)
{
	struct net_device *ndev = phydev->attached_dev;
	const u8 *mac;
	int ret;
	unsigned int i, offsets[] = {
		QCA808X_PHY_MMD3_LOCAL_MAC_ADDR_32_47_OFFSET,
		QCA808X_PHY_MMD3_LOCAL_MAC_ADDR_16_31_OFFSET,
		QCA808X_PHY_MMD3_LOCAL_MAC_ADDR_0_15_OFFSET,
	};

	if (!ndev)
		return -ENODEV;

	if (wol->wolopts & WAKE_MAGIC) {
		mac = (const u8 *) ndev->dev_addr;
		if (!is_valid_ether_addr(mac))
			return -EINVAL;

		for (i = 0; i < 3; i++)
			phy_write_mmd(phydev, MDIO_MMD_PCS, offsets[i],
				      mac[(i * 2) + 1] | (mac[(i * 2)] << 8));

		/* clear the pending wol interrupt */
		phy_read(phydev, QCA808X_PHY_INTR_STATUS);

		ret = phy_modify(phydev, QCA808X_PHY_INTR_MASK, 0, QCA808X_INTR_ENABLE_WOL);
		if (ret)
			return ret;

		ret = phy_modify_mmd(phydev, MDIO_MMD_PCS, QCA808X_PHY_MMD3_WOL_CTRL,
				0, QCA808X_WOL_EN);
	} else {
		ret = phy_modify(phydev, QCA808X_PHY_INTR_MASK, QCA808X_INTR_ENABLE_WOL, 0);
		if (ret)
			return ret;

		ret = phy_modify_mmd(phydev, MDIO_MMD_PCS, QCA808X_PHY_MMD3_WOL_CTRL,
				QCA808X_WOL_EN, 0);
	}

	return ret;
}

static void qca808x_get_wol(struct phy_device *phydev, struct ethtool_wolinfo *wol)
{
	int ret;

	wol->supported = WAKE_MAGIC;
	wol->wolopts = 0;

	ret = phy_read_mmd(phydev, MDIO_MMD_PCS, QCA808X_PHY_MMD3_WOL_CTRL);
	if (ret < 0)
		return;

	if (ret & QCA808X_WOL_EN)
		wol->wolopts |= WAKE_MAGIC;
}

static int qca808x_suspend(struct phy_device *phydev)
{
	int ret;

	ret = phy_read_mmd(phydev, MDIO_MMD_PCS, QCA808X_PHY_MMD3_WOL_CTRL);
	if (ret < 0)
		return ret;

	if (ret & QCA808X_WOL_EN)
		return 0;

	return genphy_suspend(phydev);
}

static int qca808x_speed_forced(struct phy_device *phydev)
{
	u16 speed_ctrl, type_ctrl;
	int ret;

	switch (phydev->speed) {
	case SPEED_2500:
		speed_ctrl = QCA808X_PMA_CONTROL_2500M;
		type_ctrl = QCA808X_PMA_TYPE_2500M;
		break;
	case SPEED_1000:
		speed_ctrl = QCA808X_PMA_CONTROL_1000M;
		type_ctrl = QCA808X_PMA_TYPE_1000M;
		break;
	case SPEED_100:
		speed_ctrl = QCA808X_PMA_CONTROL_100M;
		type_ctrl = QCA808X_PMA_TYPE_100M;
		break;
	default:
		speed_ctrl = QCA808X_PMA_CONTROL_10M;
		type_ctrl = QCA808X_PMA_TYPE_10M;
		break;
	}

	ret = phy_modify_mmd(phydev, MDIO_MMD_PMAPMD, QCA808X_PHY_MMD1_PMA_CONTROL,
			QCA808X_PMA_CONTROL_SPEED_MASK, speed_ctrl);
	if (ret)
		return ret;

	return phy_modify_mmd(phydev, MDIO_MMD_PMAPMD, QCA808X_PHY_MMD1_PMA_TYPE,
			QCA808X_PMA_TYPE_MASK, type_ctrl);
}

static int qca808x_config_aneg(struct phy_device *phydev)
{
	int phy_ctrl = 0;
	int ret = 0;

	if (phydev->autoneg != AUTONEG_ENABLE) {
		ret = genphy_setup_forced(phydev);
		if (ret)
			return ret;

		ret = qca808x_speed_forced(phydev);
	} else {
		ret = __genphy_config_aneg(phydev, ret);
		if (ret)
			return ret;

		if (linkmode_test_bit(ETHTOOL_LINK_MODE_2500baseT_Full_BIT, phydev->advertising))
			phy_ctrl = QCA808X_ADVERTISE_2500FULL;

		ret = phy_modify_mmd(phydev, MDIO_MMD_AN, QCA808X_PHY_MMD7_AUTONEGOTIATION_CONTROL,
				QCA808X_ADVERTISE_2500FULL, phy_ctrl);
	}

	return ret;
}

static int qca808x_get_speed(struct phy_device *phydev)
{
	int ret = phy_read(phydev, QCA808X_PHY_SPEC_STATUS);

	if (ret < 0)
		return ret;

	switch (FIELD_GET(QCA808X_STATUS_SPEED_MASK, ret)) {
	case QCA808X_STATUS_SPEED_2500MBS:
		phydev->speed = SPEED_2500;
		break;
	case QCA808X_STATUS_SPEED_1000MBS:
		phydev->speed = SPEED_1000;
		break;
	case QCA808X_STATUS_SPEED_100MBS:
		phydev->speed = SPEED_100;
		break;
	default:
		phydev->speed = SPEED_10;
		break;
	}

	return 0;
}

static int qca808x_read_status(struct phy_device *phydev)
{
	int ret;

	ret = genphy_read_status(phydev);
	if (ret)
		return ret;

	if (!phydev->link) {
		ret = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_AN_10GBT_STAT);
		if (ret < 0)
			return ret;

		linkmode_mod_bit(ETHTOOL_LINK_MODE_2500baseT_Full_BIT, phydev->lp_advertising,
				ret & MDIO_AN_10GBT_STAT_LP2_5G);

		/* generate random seed as a lower value to make PHY linked as SLAVE easily,
		 * excpet for master/slave configuration fault detected.
		 */
		if (qca808x_get_2500caps(phydev) == 1) {
			ret = phy_read(phydev, MII_STAT1000);
			if (ret < 0)
				return ret;

			if (ret & LPA_1000MSFAIL) {
				qca808x_phy_ms_seed_enable(phydev, false);
			} else {
				qca808x_phy_ms_random_seed_set(phydev);
				qca808x_phy_ms_seed_enable(phydev, true);
			}
		}
	}

	return qca808x_get_speed(phydev);
}

static int qca808x_soft_reset(struct phy_device *phydev)
{
	int ret;

	ret = genphy_soft_reset(phydev);
	if (ret < 0)
		return ret;

	if (phydev->autoneg == AUTONEG_DISABLE) {
		ret = qca808x_speed_forced(phydev);
		if (ret)
			return ret;
	}

	return qca808x_phy_ms_seed_enable(phydev, true);
}

static struct phy_driver qca808x_phy_driver[] = {
	{
		PHY_ID_MATCH_EXACT(QCA8081_PHY_ID),
		.name			= "QCA8081 PHY",
		.get_features		= qca808x_get_features,
		.config_init		= qca808x_config_init,
		.read_status		= qca808x_read_status,
		.soft_reset		= qca808x_soft_reset,
		.config_intr		= qca808x_config_intr,
		.handle_interrupt	= qca808x_handle_interrupt,
		.config_aneg		= qca808x_config_aneg,
		.set_wol		= qca808x_set_wol,
		.get_wol		= qca808x_get_wol,
		.suspend		= qca808x_suspend,
		.resume			= genphy_resume,
	},
};
module_phy_driver(qca808x_phy_driver);

static struct mdio_device_id __maybe_unused qca808x_phy_tbl[] = {
	{ PHY_ID_MATCH_EXACT(QCA8081_PHY_ID) },
	{ },
};
MODULE_DEVICE_TABLE(mdio, qca808x_phy_tbl);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. QCA8081 PHY driver");
MODULE_AUTHOR("Luo Jie");
MODULE_LICENSE("GPL");
