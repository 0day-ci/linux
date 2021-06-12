/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SOC_IMX_BLK_CTL_H
#define __SOC_IMX_BLK_CTL_H

enum imx_blk_ctl_pd_type {
	BLK_CTL_PD,
};

struct imx_blk_ctl_hw {
	int type;
	char *name;
	char *active_pd_name;
	u32 offset;
	u32 mask;
	u32 flags;
	u32 id;
	u32 rst_offset;
	u32 rst_mask;
	u32 errata;
};

struct imx_blk_ctl_domain {
	struct generic_pm_domain genpd;
	struct device *active_pd;
	struct imx_blk_ctl *blk_ctl;
	struct imx_blk_ctl_hw *hw;
	struct device *dev;
	bool hooked;
	u32 id;
};

struct imx_blk_ctl_dev_data {
	struct regmap_config config;
	struct imx_blk_ctl_hw *pds;
	struct imx_blk_ctl_hw *hw_hsk;
	u32 pds_num;
	u32 max_num;
	char *name;
};

struct imx_blk_ctl {
	struct device *dev;
	struct regmap *regmap;
	struct genpd_onecell_data onecell_data;
	const struct imx_blk_ctl_dev_data *dev_data;
	struct clk_bulk_data *clks;
	u32 num_clks;
	struct generic_pm_domain *bus_domain;

	struct mutex lock;
};

#define IMX_BLK_CTL(_type, _name, _active_pd, _id, _offset, _mask, _rst_offset, _rst_mask,	\
		    _flags, _errata)								\
	{											\
		.type = _type,									\
		.name = _name,									\
		.active_pd_name = _active_pd,							\
		.id = _id,									\
		.offset = _offset,								\
		.mask = _mask,									\
		.flags = _flags,								\
		.rst_offset = _rst_offset,							\
		.rst_mask = _rst_mask,								\
		.errata = _errata,								\
	}

#define IMX_BLK_CTL_PD(_name, _active_pd, _id, _offset, _mask, _rst_offset, _rst_mask, _flags)	\
	IMX_BLK_CTL(BLK_CTL_PD, _name, _active_pd, _id, _offset, _mask, _rst_offset,		\
		    _rst_mask, _flags, 0)

#define IMX_BLK_CTL_PD_ERRATA(_name, _active_pd, _id, _offset, _mask, _rst_offset, _rst_mask,	\
			      _flags, _errata)							\
	IMX_BLK_CTL(BLK_CTL_PD, _name, _active_pd, _id, _offset, _mask, _rst_offset,		\
		    _rst_mask, _flags, _errata)

int imx_blk_ctl_register(struct device *dev);

#define IMX_BLK_CTL_PD_HANDSHAKE	BIT(0)
#define IMX_BLK_CTL_PD_RESET		BIT(1)
#define IMX_BLK_CTL_PD_BUS		BIT(2)

const extern struct dev_pm_ops imx_blk_ctl_pm_ops;

#endif
