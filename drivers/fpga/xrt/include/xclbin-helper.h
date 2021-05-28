/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2021 Xilinx, Inc.
 *
 * Authors:
 *    David Zhang <davidzha@xilinx.com>
 *    Sonal Santan <sonal.santan@xilinx.com>
 */

#ifndef _XCLBIN_HELPER_H_
#define _XCLBIN_HELPER_H_

#include <linux/types.h>
#include <linux/device.h>
#include <linux/xrt/xclbin.h>

#define XCLBIN_VERSION2	"xclbin2"
#define XCLBIN_HWICAP_BITFILE_BUF_SZ 1024
#define XCLBIN_MAX_SZ_1G (1024 * 1024 * 1024) /* Assuming xclbin <= 1G, always */

enum axlf_section_kind;
struct axlf;

/**
 * Bitstream header information as defined by Xilinx tools.
 * Please note that this struct definition is not owned by the driver.
 */
struct xclbin_bit_head_info {
	u32 header_length;		/* Length of header in 32 bit words */
	u32 bitstream_length;		/* Length of bitstream to read in bytes */
	const unchar *design_name;	/* Design name get from bitstream */
	const unchar *part_name;	/* Part name read from bitstream */
	const unchar *date;		/* Date read from bitstream header */
	const unchar *time;		/* Bitstream creation time */
	u32 magic_length;		/* Length of the magic numbers */
	const unchar *version;		/* Version string */
};

/* caller must free the allocated memory for **data. len could be NULL. */
int xrt_xclbin_get_section(struct device *dev,  const struct axlf *xclbin,
			   enum axlf_section_kind kind, void **data,
			   uint64_t *len);
int xrt_xclbin_get_metadata(struct device *dev, const struct axlf *xclbin, char **dtb);
int xrt_xclbin_parse_bitstream_header(struct device *dev, const unchar *data,
				      u32 size, struct xclbin_bit_head_info *head_info);
const char *xrt_clock_type2epname(enum XCLBIN_CLOCK_TYPE type);

#endif /* _XCLBIN_HELPER_H_ */
