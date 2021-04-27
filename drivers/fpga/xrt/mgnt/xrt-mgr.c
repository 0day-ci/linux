// SPDX-License-Identifier: GPL-2.0
/*
 * FPGA Manager Support for Xilinx Alveo
 *
 * Copyright (C) 2020-2021 Xilinx, Inc.
 *
 * Authors: Sonal.Santan@xilinx.com
 */

#include <linux/cred.h>
#include <linux/efi.h>
#include <linux/fpga/fpga-mgr.h>
#include <linux/module.h>
#include <linux/vmalloc.h>

#include "xclbin-helper.h"
#include "xleaf.h"
#include "xrt-mgr.h"
#include "xleaf/axigate.h"
#include "xleaf/icap.h"
#include "xmgnt.h"

struct xfpga_class {
	struct xrt_device *xdev;
	char name[64];
};

/*
 * xclbin download plumbing -- find the download subsystem, ICAP and
 * pass the xclbin for heavy lifting
 */
static int xmgnt_download_bitstream(struct xrt_device *xdev,
				    const struct axlf *xclbin)

{
	struct xclbin_bit_head_info bit_header = { 0 };
	struct xrt_device *icap_leaf = NULL;
	struct xrt_icap_wr arg;
	char *bitstream = NULL;
	u64 bit_len;
	int ret;

	ret = xrt_xclbin_get_section(DEV(xdev), xclbin, BITSTREAM, (void **)&bitstream, &bit_len);
	if (ret) {
		xrt_err(xdev, "bitstream not found");
		return -ENOENT;
	}
	ret = xrt_xclbin_parse_bitstream_header(DEV(xdev), bitstream,
						XCLBIN_HWICAP_BITFILE_BUF_SZ,
						&bit_header);
	if (ret) {
		ret = -EINVAL;
		xrt_err(xdev, "invalid bitstream header");
		goto fail;
	}
	if (bit_header.header_length + bit_header.bitstream_length > bit_len) {
		ret = -EINVAL;
		xrt_err(xdev, "invalid bitstream length. header %d, bitstream %d, section len %lld",
			bit_header.header_length, bit_header.bitstream_length, bit_len);
		goto fail;
	}

	icap_leaf = xleaf_get_leaf_by_id(xdev, XRT_SUBDEV_ICAP, XRT_INVALID_DEVICE_INST);
	if (!icap_leaf) {
		ret = -ENODEV;
		xrt_err(xdev, "icap does not exist");
		goto fail;
	}
	arg.xiiw_bit_data = bitstream + bit_header.header_length;
	arg.xiiw_data_len = bit_header.bitstream_length;
	ret = xleaf_call(icap_leaf, XRT_ICAP_WRITE, &arg);
	if (ret) {
		xrt_err(xdev, "write bitstream failed, ret = %d", ret);
		xleaf_put_leaf(xdev, icap_leaf);
		goto fail;
	}

	xleaf_put_leaf(xdev, icap_leaf);
	vfree(bitstream);

	return 0;

fail:
	vfree(bitstream);

	return ret;
}

/*
 * There is no HW prep work we do here since we need the full
 * xclbin for its sanity check.
 */
static int xmgnt_pr_write_init(struct fpga_manager *mgr,
			       struct fpga_image_info *info,
			       const char *buf, size_t count)
{
	const struct axlf *bin = (const struct axlf *)buf;
	struct xfpga_class *obj = mgr->priv;

	if (!(info->flags & FPGA_MGR_PARTIAL_RECONFIG)) {
		xrt_info(obj->xdev, "%s only supports partial reconfiguration\n", obj->name);
		return -EINVAL;
	}

	if (count < sizeof(struct axlf))
		return -EINVAL;

	if (count > bin->header.length)
		return -EINVAL;

	xrt_info(obj->xdev, "Prepare download of xclbin %pUb of length %lld B",
		 &bin->header.uuid, bin->header.length);

	return 0;
}

/*
 * The implementation requries full xclbin image before we can start
 * programming the hardware via ICAP subsystem. The full image is required
 * for checking the validity of xclbin and walking the sections to
 * discover the bitstream.
 */
static int xmgnt_pr_write(struct fpga_manager *mgr,
			  const char *buf, size_t count)
{
	const struct axlf *bin = (const struct axlf *)buf;
	struct xfpga_class *obj = mgr->priv;

	if (bin->header.length != count)
		return -EINVAL;

	return xmgnt_download_bitstream((void *)obj->xdev, bin);
}

static int xmgnt_pr_write_complete(struct fpga_manager *mgr,
				   struct fpga_image_info *info)
{
	const struct axlf *bin = (const struct axlf *)info->buf;
	struct xfpga_class *obj = mgr->priv;

	xrt_info(obj->xdev, "Finished download of xclbin %pUb",
		 &bin->header.uuid);
	return 0;
}

static enum fpga_mgr_states xmgnt_pr_state(struct fpga_manager *mgr)
{
	return FPGA_MGR_STATE_UNKNOWN;
}

static const struct fpga_manager_ops xmgnt_pr_ops = {
	.initial_header_size = sizeof(struct axlf),
	.write_init = xmgnt_pr_write_init,
	.write = xmgnt_pr_write,
	.write_complete = xmgnt_pr_write_complete,
	.state = xmgnt_pr_state,
};

struct fpga_manager *xmgnt_fmgr_probe(struct xrt_device *xdev)
{
	struct xfpga_class *obj = devm_kzalloc(DEV(xdev), sizeof(struct xfpga_class),
					       GFP_KERNEL);
	struct fpga_manager *fmgr = NULL;
	int ret = 0;

	if (!obj)
		return ERR_PTR(-ENOMEM);

	snprintf(obj->name, sizeof(obj->name), "Xilinx Alveo FPGA Manager");
	obj->xdev = xdev;
	fmgr = fpga_mgr_create(&xdev->dev,
			       obj->name,
			       &xmgnt_pr_ops,
			       obj);
	if (!fmgr)
		return ERR_PTR(-ENOMEM);

	ret = fpga_mgr_register(fmgr);
	if (ret) {
		fpga_mgr_free(fmgr);
		return ERR_PTR(ret);
	}
	return fmgr;
}

int xmgnt_fmgr_remove(struct fpga_manager *fmgr)
{
	fpga_mgr_unregister(fmgr);
	return 0;
}
