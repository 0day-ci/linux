/* SPDX-License-Identifier: ISC */
/*
 * Copyright (C) 2011-2013 Jonas Gorski <jogo@openwrt.org>
 *
 * Included by drivers/net/dsa/b53/b53_priv.h and net/dsa/tag_brcm.c
 */

#include <net/dsa.h>

struct b53_device;
struct phylink_link_state;

struct b53_io_ops {
	int (*read8)(struct b53_device *dev, u8 page, u8 reg, u8 *value);
	int (*read16)(struct b53_device *dev, u8 page, u8 reg, u16 *value);
	int (*read32)(struct b53_device *dev, u8 page, u8 reg, u32 *value);
	int (*read48)(struct b53_device *dev, u8 page, u8 reg, u64 *value);
	int (*read64)(struct b53_device *dev, u8 page, u8 reg, u64 *value);
	int (*write8)(struct b53_device *dev, u8 page, u8 reg, u8 value);
	int (*write16)(struct b53_device *dev, u8 page, u8 reg, u16 value);
	int (*write32)(struct b53_device *dev, u8 page, u8 reg, u32 value);
	int (*write48)(struct b53_device *dev, u8 page, u8 reg, u64 value);
	int (*write64)(struct b53_device *dev, u8 page, u8 reg, u64 value);
	int (*phy_read16)(struct b53_device *dev, int addr, int reg,
			  u16 *value);
	int (*phy_write16)(struct b53_device *dev, int addr, int reg,
			   u16 value);
	int (*irq_enable)(struct b53_device *dev, int port);
	void (*irq_disable)(struct b53_device *dev, int port);
	u8 (*serdes_map_lane)(struct b53_device *dev, int port);
	int (*serdes_link_state)(struct b53_device *dev, int port,
				 struct phylink_link_state *state);
	void (*serdes_config)(struct b53_device *dev, int port,
			      unsigned int mode,
			      const struct phylink_link_state *state);
	void (*serdes_an_restart)(struct b53_device *dev, int port);
	void (*serdes_link_set)(struct b53_device *dev, int port,
				unsigned int mode, phy_interface_t interface,
				bool link_up);
	void (*serdes_phylink_validate)(struct b53_device *dev, int port,
					unsigned long *supported,
					struct phylink_link_state *state);
};

struct b53_port {
	u16 vlan_ctl_mask;
	struct ethtool_eee eee;
};

struct b53_vlan {
	u16 members;
	u16 untag;
	bool valid;
};

struct b53_device {
	struct dsa_switch *ds;
	struct b53_platform_data *pdata;
	const char *name;

	struct mutex reg_mutex;
	struct mutex stats_mutex;
	struct mutex arl_mutex;
	const struct b53_io_ops *ops;

	/* chip specific data */
	u32 chip_id;
	u8 core_rev;
	u8 vta_regs[3];
	u8 duplex_reg;
	u8 jumbo_pm_reg;
	u8 jumbo_size_reg;
	int reset_gpio;
	u8 num_arl_bins;
	u16 num_arl_buckets;
	enum dsa_tag_protocol tag_protocol;

	/* used ports mask */
	u16 enabled_ports;
	unsigned int imp_port;

	/* connect specific data */
	u8 current_page;
	struct device *dev;
	u8 serdes_lane;

	/* Master MDIO bus we got probed from */
	struct mii_bus *bus;

	void *priv;

	/* run time configuration */
	bool enable_jumbo;

	unsigned int num_vlans;
	struct b53_vlan *vlans;
	bool vlan_enabled;
	unsigned int num_ports;
	struct b53_port *ports;
};
