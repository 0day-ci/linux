// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Alveo FPGA VSEC Driver
 *
 * Copyright (C) 2020-2021 Xilinx, Inc.
 *
 * Authors:
 *      Lizhi Hou<Lizhi.Hou@xilinx.com>
 */

#include <linux/regmap.h>
#include "metadata.h"
#include "xdevice.h"
#include "xleaf.h"

#define XRT_VSEC "xrt_vsec"

#define VSEC_TYPE_UUID		0x50
#define VSEC_TYPE_FLASH		0x51
#define VSEC_TYPE_PLATINFO	0x52
#define VSEC_TYPE_MAILBOX	0x53
#define VSEC_TYPE_END		0xff

#define VSEC_UUID_LEN		16

#define VSEC_REG_FORMAT		0x0
#define VSEC_REG_LENGTH		0x4
#define VSEC_REG_ENTRY		0x8

struct xrt_vsec_header {
	u32		format;
	u32		length;
	u32		entry_sz;
	u32		rsvd;
} __packed;

struct xrt_vsec_entry {
	u8		type;
	u8		bar_rev;
	u16		off_lo;
	u32		off_hi;
	u8		ver_type;
	u8		minor;
	u8		major;
	u8		rsvd0;
	u32		rsvd1;
} __packed;

struct vsec_device {
	u8		type;
	char		*ep_name;
	ulong		size;
	char		*compat;
};

static struct vsec_device vsec_devs[] = {
	{
		.type = VSEC_TYPE_UUID,
		.ep_name = XRT_MD_NODE_BLP_ROM,
		.size = VSEC_UUID_LEN,
		.compat = "vsec-uuid",
	},
	{
		.type = VSEC_TYPE_FLASH,
		.ep_name = XRT_MD_NODE_FLASH_VSEC,
		.size = 4096,
		.compat = "vsec-flash",
	},
	{
		.type = VSEC_TYPE_PLATINFO,
		.ep_name = XRT_MD_NODE_PLAT_INFO,
		.size = 4,
		.compat = "vsec-platinfo",
	},
	{
		.type = VSEC_TYPE_MAILBOX,
		.ep_name = XRT_MD_NODE_MAILBOX_VSEC,
		.size = 48,
		.compat = "vsec-mbx",
	},
};

XRT_DEFINE_REGMAP_CONFIG(vsec_regmap_config);

struct xrt_vsec {
	struct xrt_device	*xdev;
	struct regmap		*regmap;
	u32			length;

	char			*metadata;
	char			uuid[VSEC_UUID_LEN];
	int			group;
};

static inline int vsec_read_entry(struct xrt_vsec *vsec, u32 index, struct xrt_vsec_entry *entry)
{
	int ret;

	ret = regmap_bulk_read(vsec->regmap, sizeof(struct xrt_vsec_header) +
			       index * sizeof(struct xrt_vsec_entry), entry,
			       sizeof(struct xrt_vsec_entry) /
			       vsec_regmap_config.reg_stride);

	return ret;
}

static inline u32 vsec_get_bar(struct xrt_vsec_entry *entry)
{
	return (entry->bar_rev >> 4) & 0xf;
}

static inline u64 vsec_get_bar_off(struct xrt_vsec_entry *entry)
{
	return entry->off_lo | ((u64)entry->off_hi << 16);
}

static inline u32 vsec_get_rev(struct xrt_vsec_entry *entry)
{
	return entry->bar_rev & 0xf;
}

static char *type2epname(u32 type)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(vsec_devs); i++) {
		if (vsec_devs[i].type == type)
			return (vsec_devs[i].ep_name);
	}

	return NULL;
}

static ulong type2size(u32 type)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(vsec_devs); i++) {
		if (vsec_devs[i].type == type)
			return (vsec_devs[i].size);
	}

	return 0;
}

static char *type2compat(u32 type)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(vsec_devs); i++) {
		if (vsec_devs[i].type == type)
			return (vsec_devs[i].compat);
	}

	return NULL;
}

static int xrt_vsec_add_node(struct xrt_vsec *vsec,
			     void *md_blob, struct xrt_vsec_entry *p_entry)
{
	struct xrt_md_endpoint ep;
	char compat_ver[64];
	int ret;

	if (!type2epname(p_entry->type))
		return -EINVAL;

	/*
	 * VSEC may have more than 1 mailbox instance for the card
	 * which has more than 1 physical function.
	 * This is not supported for now. Assuming only one mailbox
	 */

	snprintf(compat_ver, sizeof(compat_ver) - 1, "%d-%d.%d.%d",
		 p_entry->ver_type, p_entry->major, p_entry->minor,
		 vsec_get_rev(p_entry));
	ep.ep_name = type2epname(p_entry->type);
	ep.bar_index = vsec_get_bar(p_entry);
	ep.bar_off = vsec_get_bar_off(p_entry);
	ep.size = type2size(p_entry->type);
	ep.compat = type2compat(p_entry->type);
	ep.compat_ver = compat_ver;
	ret = xrt_md_add_endpoint(DEV(vsec->xdev), vsec->metadata, &ep);
	if (ret)
		xrt_err(vsec->xdev, "add ep failed, ret %d", ret);

	return ret;
}

static int xrt_vsec_create_metadata(struct xrt_vsec *vsec)
{
	struct xrt_vsec_entry entry;
	int i, ret;

	ret = xrt_md_create(&vsec->xdev->dev, &vsec->metadata);
	if (ret) {
		xrt_err(vsec->xdev, "create metadata failed");
		return ret;
	}

	for (i = 0; i * sizeof(entry) < vsec->length -
	    sizeof(struct xrt_vsec_header); i++) {
		ret = vsec_read_entry(vsec, i, &entry);
		if (ret) {
			xrt_err(vsec->xdev, "failed read entry %d, ret %d", i, ret);
			goto fail;
		}

		if (entry.type == VSEC_TYPE_END)
			break;
		ret = xrt_vsec_add_node(vsec, vsec->metadata, &entry);
		if (ret)
			goto fail;
	}

	return 0;

fail:
	vfree(vsec->metadata);
	vsec->metadata = NULL;
	return ret;
}

static int xrt_vsec_leaf_call(struct xrt_device *xdev, u32 cmd, void *arg)
{
	int ret = 0;

	switch (cmd) {
	case XRT_XLEAF_EVENT:
		/* Does not handle any event. */
		break;
	default:
		ret = -EINVAL;
		xrt_err(xdev, "should never been called");
		break;
	}

	return ret;
}

static int xrt_vsec_mapio(struct xrt_vsec *vsec)
{
	struct xrt_subdev_platdata *pdata = DEV_PDATA(vsec->xdev);
	struct resource *res = NULL;
	void __iomem *base = NULL;
	const u64 *bar_off;
	const u32 *bar;
	u64 addr;
	int ret;

	if (!pdata || xrt_md_size(DEV(vsec->xdev), pdata->xsp_dtb) == XRT_MD_INVALID_LENGTH) {
		xrt_err(vsec->xdev, "empty metadata");
		return -EINVAL;
	}

	ret = xrt_md_get_prop(DEV(vsec->xdev), pdata->xsp_dtb, XRT_MD_NODE_VSEC,
			      NULL, XRT_MD_PROP_BAR_IDX, (const void **)&bar, NULL);
	if (ret) {
		xrt_err(vsec->xdev, "failed to get bar idx, ret %d", ret);
		return -EINVAL;
	}

	ret = xrt_md_get_prop(DEV(vsec->xdev), pdata->xsp_dtb, XRT_MD_NODE_VSEC,
			      NULL, XRT_MD_PROP_OFFSET, (const void **)&bar_off, NULL);
	if (ret) {
		xrt_err(vsec->xdev, "failed to get bar off, ret %d", ret);
		return -EINVAL;
	}

	xrt_info(vsec->xdev, "Map vsec at bar %d, offset 0x%llx",
		 be32_to_cpu(*bar), be64_to_cpu(*bar_off));

	xleaf_get_root_res(vsec->xdev, be32_to_cpu(*bar), &res);
	if (!res) {
		xrt_err(vsec->xdev, "failed to get bar addr");
		return -EINVAL;
	}

	addr = res->start + be64_to_cpu(*bar_off);

	base = devm_ioremap(&vsec->xdev->dev, addr, vsec_regmap_config.max_register);
	if (!base) {
		xrt_err(vsec->xdev, "Map failed");
		return -EIO;
	}

	vsec->regmap = devm_regmap_init_mmio(&vsec->xdev->dev, base, &vsec_regmap_config);
	if (IS_ERR(vsec->regmap)) {
		xrt_err(vsec->xdev, "regmap %pR failed", res);
		return PTR_ERR(vsec->regmap);
	}

	ret = regmap_read(vsec->regmap, VSEC_REG_LENGTH, &vsec->length);
	if (ret) {
		xrt_err(vsec->xdev, "failed to read length %d", ret);
		return ret;
	}

	return 0;
}

static void xrt_vsec_remove(struct xrt_device *xdev)
{
	struct xrt_vsec	*vsec;

	vsec = xrt_get_drvdata(xdev);

	if (vsec->group >= 0)
		xleaf_destroy_group(xdev, vsec->group);
	vfree(vsec->metadata);
}

static int xrt_vsec_probe(struct xrt_device *xdev)
{
	struct xrt_vsec	*vsec;
	int ret = 0;

	vsec = devm_kzalloc(&xdev->dev, sizeof(*vsec), GFP_KERNEL);
	if (!vsec)
		return -ENOMEM;

	vsec->xdev = xdev;
	vsec->group = -1;
	xrt_set_drvdata(xdev, vsec);

	ret = xrt_vsec_mapio(vsec);
	if (ret)
		goto failed;

	ret = xrt_vsec_create_metadata(vsec);
	if (ret) {
		xrt_err(xdev, "create metadata failed, ret %d", ret);
		goto failed;
	}
	ret = xleaf_create_group(xdev, vsec->metadata);
	if (ret < 0) {
		xrt_err(xdev, "create group failed, ret %d", vsec->group);
		goto failed;
	}
	vsec->group = ret;

	return 0;

failed:
	xrt_vsec_remove(xdev);

	return ret;
}

static struct xrt_dev_endpoints xrt_vsec_endpoints[] = {
	{
		.xse_names = (struct xrt_dev_ep_names []){
			{ .ep_name = XRT_MD_NODE_VSEC },
			{ NULL },
		},
		.xse_min_ep = 1,
	},
	{ 0 },
};

static struct xrt_driver xrt_vsec_driver = {
	.driver = {
		.name = XRT_VSEC,
	},
	.subdev_id = XRT_SUBDEV_VSEC,
	.endpoints = xrt_vsec_endpoints,
	.probe = xrt_vsec_probe,
	.remove = xrt_vsec_remove,
	.leaf_call = xrt_vsec_leaf_call,
};

XRT_LEAF_INIT_FINI_FUNC(vsec);
