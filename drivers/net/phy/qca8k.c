// SPDX-License-Identifier: GPL-2.0+
#include <linux/kernel.h>
#include <linux/mii.h>
#include <linux/phy.h>
#include <linux/module.h>
#include <linux/bitfield.h>
#include <linux/etherdevice.h>
#include <linux/ethtool_netlink.h>

#define QCA8K_DEVFLAGS_REVISION_MASK		GENMASK(2, 0)

#define QCA8K_PHY_ID_MASK			0xffffffff
#define QCA8K_PHY_ID_QCA8327			0x004dd034
#define QCA8K_PHY_ID_QCA8337			0x004dd036

#define MDIO_AZ_DEBUG				0x800d

#define MDIO_DBG_ANALOG_TEST			0x0
#define MDIO_DBG_SYSTEM_CONTROL_MODE		0x5
#define MDIO_DBG_CONTROL_FEATURE_CONF		0x3d

/* QCA specific MII registers */
#define MII_ATH_DBG_ADDR			0x1d
#define MII_ATH_DBG_DATA			0x1e

/* QCA specific MII registers access function */
static void qca8k_phy_dbg_write(struct mii_bus *bus, int phy_addr, u16 dbg_addr, u16 dbg_data)
{
	mutex_lock_nested(&bus->mdio_lock, MDIO_MUTEX_NESTED);
	bus->write(bus, phy_addr, MII_ATH_DBG_ADDR, dbg_addr);
	bus->write(bus, phy_addr, MII_ATH_DBG_DATA, dbg_data);
	mutex_unlock(&bus->mdio_lock);
}

enum stat_access_type {
	PHY,
	MMD
};

struct qca8k_hw_stat {
	const char *string;
	u8 reg;
	u32 mask;
	enum stat_access_type access_type;
};

static struct qca8k_hw_stat qca8k_hw_stats[] = {
	{ "phy_idle_errors", 0xa, GENMASK(7, 0), PHY},
	{ "phy_receive_errors", 0x15, GENMASK(15, 0), PHY},
	{ "eee_wake_errors", 0x16, GENMASK(15, 0), MMD},
};

struct qca8k_phy_priv {
	u8 switch_revision;
	u64 stats[ARRAY_SIZE(qca8k_hw_stats)];
};

static int qca8k_get_sset_count(struct phy_device *phydev)
{
	return ARRAY_SIZE(qca8k_hw_stats);
}

static void qca8k_get_strings(struct phy_device *phydev, u8 *data)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(qca8k_hw_stats); i++) {
		strscpy(data + i * ETH_GSTRING_LEN,
			qca8k_hw_stats[i].string, ETH_GSTRING_LEN);
	}
}

static u64 qca8k_get_stat(struct phy_device *phydev, int i)
{
	struct qca8k_hw_stat stat = qca8k_hw_stats[i];
	struct qca8k_phy_priv *priv = phydev->priv;
	int val;
	u64 ret;

	if (stat.access_type == MMD)
		val = phy_read_mmd(phydev, MDIO_MMD_PCS, stat.reg);
	else
		val = phy_read(phydev, stat.reg);

	if (val < 0) {
		ret = U64_MAX;
	} else {
		val = val & stat.mask;
		priv->stats[i] += val;
		ret = priv->stats[i];
	}

	return ret;
}

static void qca8k_get_stats(struct phy_device *phydev,
			    struct ethtool_stats *stats, u64 *data)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(qca8k_hw_stats); i++)
		data[i] = qca8k_get_stat(phydev, i);
}

static int qca8k_config_init(struct phy_device *phydev)
{
	struct qca8k_phy_priv *priv = phydev->priv;
	struct mii_bus *bus = phydev->mdio.bus;
	int phy_addr = phydev->mdio.addr;

	priv->switch_revision = phydev->dev_flags & QCA8K_DEVFLAGS_REVISION_MASK;

	switch (priv->switch_revision) {
	case 1:
		/* For 100M waveform */
		qca8k_phy_dbg_write(bus, phy_addr, MDIO_DBG_ANALOG_TEST, 0x02ea);
		/* Turn on Gigabit clock */
		qca8k_phy_dbg_write(bus, phy_addr, MDIO_DBG_CONTROL_FEATURE_CONF, 0x68a0);
		break;

	case 2:
		phy_write_mmd(phydev, MDIO_MMD_AN, MDIO_AN_EEE_ADV, 0x0);
		fallthrough;
	case 4:
		phy_write_mmd(phydev, MDIO_MMD_PCS, MDIO_AZ_DEBUG, 0x803f);
		qca8k_phy_dbg_write(bus, phy_addr, MDIO_DBG_CONTROL_FEATURE_CONF, 0x6860);
		qca8k_phy_dbg_write(bus, phy_addr, MDIO_DBG_SYSTEM_CONTROL_MODE, 0x2c46);
		qca8k_phy_dbg_write(bus, phy_addr, 0x3c, 0x6000);
		break;
	}

	return 0;
}

static int qca8k_probe(struct phy_device *phydev)
{
	struct qca8k_phy_priv *priv;

	priv = devm_kzalloc(&phydev->mdio.dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	phydev->priv = priv;

	return 0;
}

static struct phy_driver qca8k_drivers[] = {
	{
		.phy_id = QCA8K_PHY_ID_QCA8337,
		.phy_id_mask = QCA8K_PHY_ID_MASK,
		.name = "QCA PHY 8337",
		/* PHY_GBIT_FEATURES */
		.probe = qca8k_probe,
		.flags = PHY_IS_INTERNAL,
		.config_init = qca8k_config_init,
		.soft_reset = genphy_soft_reset,
		.get_sset_count = qca8k_get_sset_count,
		.get_strings = qca8k_get_strings,
		.get_stats = qca8k_get_stats,
	},
};

module_phy_driver(qca8k_drivers);

static struct mdio_device_id __maybe_unused qca8k_tbl[] = {
	{ QCA8K_PHY_ID_QCA8337, QCA8K_PHY_ID_MASK },
	{ }
};

MODULE_DEVICE_TABLE(mdio, qca8k_tbl);
MODULE_DESCRIPTION("Qualcomm QCA8k PHY driver");
MODULE_AUTHOR("Ansuel Smith");
MODULE_LICENSE("GPL");
