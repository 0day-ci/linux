// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Alveo FPGA Driver XCLBIN parser
 *
 * Copyright (C) 2020-2021 Xilinx, Inc.
 *
 * Authors: David Zhang <davidzha@xilinx.com>
 */

#include <asm/errno.h>
#include <linux/vmalloc.h>
#include <linux/device.h>
#include "xclbin-helper.h"
#include "metadata.h"

/* Used for parsing bitstream header */
#define BITSTREAM_EVEN_MAGIC_BYTE	0x0f
#define BITSTREAM_ODD_MAGIC_BYTE	0xf0

static int xrt_xclbin_get_section_hdr(const struct axlf *xclbin,
				      enum axlf_section_kind kind,
				      const struct axlf_section_header **header)
{
	const struct axlf_section_header *phead = NULL;
	u64 xclbin_len;
	int i;

	*header = NULL;
	for (i = 0; i < xclbin->header.num_sections; i++) {
		if (xclbin->sections[i].section_kind == kind) {
			phead = &xclbin->sections[i];
			break;
		}
	}

	if (!phead)
		return -ENOENT;

	xclbin_len = xclbin->header.length;
	if (xclbin_len > XCLBIN_MAX_SIZE ||
	    phead->section_offset + phead->section_size > xclbin_len)
		return -EINVAL;

	*header = phead;
	return 0;
}

static int xrt_xclbin_section_info(const struct axlf *xclbin,
				   enum axlf_section_kind kind,
				   u64 *offset, u64 *size)
{
	const struct axlf_section_header *mem_header = NULL;
	int rc;

	rc = xrt_xclbin_get_section_hdr(xclbin, kind, &mem_header);
	if (rc)
		return rc;

	*offset = mem_header->section_offset;
	*size = mem_header->section_size;

	return 0;
}

/* caller must free the allocated memory for **data */
int xrt_xclbin_get_section(struct device *dev,
			   const struct axlf *buf,
			   enum axlf_section_kind kind,
			   void **data, u64 *len)
{
	const struct axlf *xclbin = (const struct axlf *)buf;
	void *section = NULL;
	u64 offset = 0;
	u64 size = 0;
	int err = 0;

	if (!data) {
		dev_err(dev, "invalid data pointer");
		return -EINVAL;
	}

	err = xrt_xclbin_section_info(xclbin, kind, &offset, &size);
	if (err) {
		dev_dbg(dev, "parsing section failed. kind %d, err = %d", kind, err);
		return err;
	}

	section = vzalloc(size);
	if (!section)
		return -ENOMEM;

	memcpy(section, ((const char *)xclbin) + offset, size);

	*data = section;
	if (len)
		*len = size;

	return 0;
}
EXPORT_SYMBOL_GPL(xrt_xclbin_get_section);

static inline int xclbin_bit_get_string(const unchar *data, u32 size,
					u32 offset, unchar prefix,
					const unchar **str)
{
	int len;
	u32 tmp;

	/* prefix and length will be 3 bytes */
	if (offset + 3  > size)
		return -EINVAL;

	/* Read prefix */
	tmp = data[offset++];
	if (tmp != prefix)
		return -EINVAL;

	/* Get string length */
	len = data[offset++];
	len = (len << 8) | data[offset++];

	if (offset + len > size)
		return -EINVAL;

	if (data[offset + len - 1] != '\0')
		return -EINVAL;

	*str = data + offset;

	return len + 3;
}

/* parse bitstream header */
int xrt_xclbin_parse_bitstream_header(struct device *dev, const unchar *data,
				      u32 size, struct xclbin_bit_head_info *head_info)
{
	u32 offset = 0;
	int len, i;
	u16 magic;

	memset(head_info, 0, sizeof(*head_info));

	/* Get "Magic" length */
	if (size < sizeof(u16)) {
		dev_err(dev, "invalid size");
		return -EINVAL;
	}

	len = data[offset++];
	len = (len << 8) | data[offset++];

	if (offset + len > size) {
		dev_err(dev, "invalid magic len");
		return -EINVAL;
	}
	head_info->magic_length = len;

	for (i = 0; i < head_info->magic_length - 1; i++) {
		magic = data[offset++];
		if (!(i % 2) && magic != BITSTREAM_EVEN_MAGIC_BYTE) {
			dev_err(dev, "invalid magic even byte at %d", offset);
			return -EINVAL;
		}

		if ((i % 2) && magic != BITSTREAM_ODD_MAGIC_BYTE) {
			dev_err(dev, "invalid magic odd byte at %d", offset);
			return -EINVAL;
		}
	}

	if (offset + 3 > size) {
		dev_err(dev, "invalid length of magic end");
		return -EINVAL;
	}
	/* Read null end of magic data. */
	if (data[offset++]) {
		dev_err(dev, "invalid magic end");
		return -EINVAL;
	}

	/* Read 0x01 (short) */
	magic = data[offset++];
	magic = (magic << 8) | data[offset++];

	/* Check the "0x01" half word */
	if (magic != 0x01) {
		dev_err(dev, "invalid magic end");
		return -EINVAL;
	}

	len = xclbin_bit_get_string(data, size, offset, 'a', &head_info->design_name);
	if (len < 0) {
		dev_err(dev, "get design name failed");
		return -EINVAL;
	}

	head_info->version = strstr(head_info->design_name, "Version=") + strlen("Version=");
	offset += len;

	len = xclbin_bit_get_string(data, size, offset, 'b', &head_info->part_name);
	if (len < 0) {
		dev_err(dev, "get part name failed");
		return -EINVAL;
	}
	offset += len;

	len = xclbin_bit_get_string(data, size, offset, 'c', &head_info->date);
	if (len < 0) {
		dev_err(dev, "get data failed");
		return -EINVAL;
	}
	offset += len;

	len = xclbin_bit_get_string(data, size, offset, 'd', &head_info->time);
	if (len < 0) {
		dev_err(dev, "get time failed");
		return -EINVAL;
	}
	offset += len;

	if (offset + 5 >= size) {
		dev_err(dev, "can not get bitstream length");
		return -EINVAL;
	}

	/* Read 'e' */
	if (data[offset++] != 'e') {
		dev_err(dev, "invalid prefix of bitstream length");
		return -EINVAL;
	}

	/* Get byte length of bitstream */
	head_info->bitstream_length = data[offset++];
	head_info->bitstream_length = (head_info->bitstream_length << 8) | data[offset++];
	head_info->bitstream_length = (head_info->bitstream_length << 8) | data[offset++];
	head_info->bitstream_length = (head_info->bitstream_length << 8) | data[offset++];

	head_info->header_length = offset;

	return 0;
}
EXPORT_SYMBOL_GPL(xrt_xclbin_parse_bitstream_header);

struct xrt_clock_desc {
	char	*clock_ep_name;
	u32	clock_xclbin_type;
	char	*clkfreq_ep_name;
} clock_desc[] = {
	{
		.clock_ep_name = XRT_MD_NODE_CLK_KERNEL1,
		.clock_xclbin_type = CT_DATA,
		.clkfreq_ep_name = XRT_MD_NODE_CLKFREQ_K1,
	},
	{
		.clock_ep_name = XRT_MD_NODE_CLK_KERNEL2,
		.clock_xclbin_type = CT_KERNEL,
		.clkfreq_ep_name = XRT_MD_NODE_CLKFREQ_K2,
	},
	{
		.clock_ep_name = XRT_MD_NODE_CLK_KERNEL3,
		.clock_xclbin_type = CT_SYSTEM,
		.clkfreq_ep_name = XRT_MD_NODE_CLKFREQ_HBM,
	},
};

const char *xrt_clock_type2epname(enum XCLBIN_CLOCK_TYPE type)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(clock_desc); i++) {
		if (clock_desc[i].clock_xclbin_type == type)
			return clock_desc[i].clock_ep_name;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(xrt_clock_type2epname);

static const char *clock_type2clkfreq_name(enum XCLBIN_CLOCK_TYPE type)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(clock_desc); i++) {
		if (clock_desc[i].clock_xclbin_type == type)
			return clock_desc[i].clkfreq_ep_name;
	}
	return NULL;
}

static int xrt_xclbin_add_clock_metadata(struct device *dev,
					 const struct axlf *xclbin,
					 char *dtb)
{
	struct clock_freq_topology *clock_topo;
	u16 freq;
	int rc;
	int i;

	/* if clock section does not exist, add nothing and return success */
	rc = xrt_xclbin_get_section(dev, xclbin, CLOCK_FREQ_TOPOLOGY,
				    (void **)&clock_topo, NULL);
	if (rc == -ENOENT)
		return 0;
	else if (rc)
		return rc;

	for (i = 0; i < clock_topo->count; i++) {
		u8 type = clock_topo->clock_freq[i].type;
		const char *ep_name = xrt_clock_type2epname(type);
		const char *counter_name = clock_type2clkfreq_name(type);

		if (!ep_name || !counter_name)
			continue;

		freq = cpu_to_be16(clock_topo->clock_freq[i].freq_MHZ);
		rc = xrt_md_set_prop(dev, dtb, ep_name, NULL, XRT_MD_PROP_CLK_FREQ,
				     &freq, sizeof(freq));
		if (rc)
			break;

		rc = xrt_md_set_prop(dev, dtb, ep_name, NULL, XRT_MD_PROP_CLK_CNT,
				     counter_name, strlen(counter_name) + 1);
		if (rc)
			break;
	}

	vfree(clock_topo);

	return rc;
}

int xrt_xclbin_get_metadata(struct device *dev, const struct axlf *xclbin, char **dtb)
{
	char *md = NULL, *newmd = NULL;
	u64 len, md_len;
	int rc;

	*dtb = NULL;

	rc = xrt_xclbin_get_section(dev, xclbin, PARTITION_METADATA, (void **)&md, &len);
	if (rc)
		goto done;

	md_len = xrt_md_size(dev, md);

	/* Sanity check the dtb section. */
	if (md_len > len) {
		rc = -EINVAL;
		goto done;
	}

	/* use dup function here to convert incoming metadata to writable */
	newmd = xrt_md_dup(dev, md);
	if (!newmd) {
		rc = -EFAULT;
		goto done;
	}

	/* Convert various needed xclbin sections into dtb. */
	rc = xrt_xclbin_add_clock_metadata(dev, xclbin, newmd);

	if (!rc)
		*dtb = newmd;
	else
		vfree(newmd);
done:
	vfree(md);
	return rc;
}
EXPORT_SYMBOL_GPL(xrt_xclbin_get_metadata);
