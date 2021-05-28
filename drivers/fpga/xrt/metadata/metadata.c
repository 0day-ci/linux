// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Alveo FPGA Metadata parse APIs
 *
 * Copyright (C) 2020-2021 Xilinx, Inc.
 *
 * Authors:
 *      Lizhi Hou <Lizhi.Hou@xilinx.com>
 */

#include <linux/libfdt_env.h>
#include "libfdt.h"
#include "metadata.h"

#define XRT_MAX_BLOB_SIZE	(4096 * 25)
#define XRT_MAX_DEPTH 5

static int xrt_md_setprop(struct device *dev, char *blob, int offset,
			  const char *prop, const void *val, int size)
{
	int ret;

	ret = fdt_setprop(blob, offset, prop, val, size);
	if (ret) {
		ret = -EINVAL;
		dev_err(dev, "failed to set prop %d", ret);
	}

	return ret;
}

static int xrt_md_add_node(struct device *dev, char *blob, int parent_offset,
			   const char *ep_name)
{
	int ret;

	ret = fdt_add_subnode(blob, parent_offset, ep_name);
	if (ret < 0 && ret != -FDT_ERR_EXISTS) {
		ret = -EINVAL;
		dev_err(dev, "failed to add node %s. %d", ep_name, ret);
	}

	return ret;
}

static int xrt_md_get_endpoint(struct device *dev, const char *blob,
			       const char *ep_name, const char *compat,
			       int *ep_offset)
{
	const char *name;
	int offset;

	if (compat) {
		for (offset = fdt_next_node((blob), -1, NULL);
		     offset >= 0;
		     offset = fdt_next_node((blob), offset, NULL)) {
			name = fdt_get_name(blob, offset, NULL);
			if (!name || strncmp(name, ep_name, strlen(ep_name) + 1))
				continue;
			if (!fdt_node_check_compatible(blob, offset, compat))
				break;
		}
	} else {
		for (offset = fdt_next_node((blob), -1, NULL);
		     offset >= 0;
		     offset = fdt_next_node((blob), offset, NULL)) {
			name = fdt_get_name(blob, offset, NULL);
			if (name && !strncmp(name, ep_name, strlen(ep_name) + 1))
				break;
		}
	}

	if (offset < 0)
		return -ENODEV;

	*ep_offset = offset;

	return 0;
}

static inline int xrt_md_get_node(struct device *dev, const char *blob,
				  const char *name, const char *compat,
				  int *offset)
{
	int ret = 0;

	if (name) {
		ret = xrt_md_get_endpoint(dev, blob, name, compat,
					  offset);
		if (ret) {
			if (compat) {
				dev_err(dev, "cannot get node %s compat %s, ret %d",
					name, compat, ret);
			} else {
				dev_err(dev, "cannot get node %s, ret %d", name, ret);
			}
			return -EINVAL;
		}
	} else {
		ret = fdt_next_node(blob, -1, NULL);
		if (ret < 0) {
			dev_err(dev, "internal error, ret = %d", ret);
			return -EINVAL;
		}
		*offset = ret;
	}

	return 0;
}

static int xrt_md_overlay(struct device *dev, char *blob, int target,
			  const char *overlay_blob, int overlay_offset,
			  int depth)
{
	int property, subnode;
	int ret;

	if (!blob || !overlay_blob) {
		dev_err(dev, "blob is NULL");
		return -EINVAL;
	}

	if (depth > XRT_MAX_DEPTH) {
		dev_err(dev, "meta data depth beyond %d", XRT_MAX_DEPTH);
		return -EINVAL;
	}

	if (target < 0) {
		target = fdt_next_node(blob, -1, NULL);
		if (target < 0) {
			dev_err(dev, "invalid target");
			return -EINVAL;
		}
	}
	if (overlay_offset < 0) {
		overlay_offset = fdt_next_node(overlay_blob, -1, NULL);
		if (overlay_offset < 0) {
			dev_err(dev, "invalid overlay");
			return -EINVAL;
		}
	}

	fdt_for_each_property_offset(property, overlay_blob, overlay_offset) {
		const char *name;
		const void *prop;
		int prop_len;

		prop = fdt_getprop_by_offset(overlay_blob, property, &name,
					     &prop_len);
		if (!prop || prop_len >= XRT_MAX_BLOB_SIZE || prop_len < 0) {
			dev_err(dev, "internal error");
			return -EINVAL;
		}

		ret = xrt_md_setprop(dev, blob, target, name, prop,
				     prop_len);
		if (ret) {
			dev_err(dev, "setprop failed, ret = %d", ret);
			return ret;
		}
	}

	fdt_for_each_subnode(subnode, overlay_blob, overlay_offset) {
		const char *name = fdt_get_name(overlay_blob, subnode, NULL);
		int nnode;

		nnode = xrt_md_add_node(dev, blob, target, name);
		if (nnode == -FDT_ERR_EXISTS)
			nnode = fdt_subnode_offset(blob, target, name);
		if (nnode < 0) {
			dev_err(dev, "add node failed, ret = %d", nnode);
			return -EINVAL;
		}

		ret = xrt_md_overlay(dev, blob, nnode, overlay_blob, subnode, depth + 1);
		if (ret)
			return ret;
	}

	return 0;
}

u32 xrt_md_size(struct device *dev, const char *blob)
{
	u32 len = fdt_totalsize(blob);

	if (len > XRT_MAX_BLOB_SIZE)
		return XRT_MD_INVALID_LENGTH;

	return len;
}
EXPORT_SYMBOL_GPL(xrt_md_size);

int xrt_md_create(struct device *dev, char **blob)
{
	int ret = 0;

	if (!blob) {
		dev_err(dev, "blob is NULL");
		return -EINVAL;
	}

	*blob = vzalloc(XRT_MAX_BLOB_SIZE);
	if (!*blob)
		return -ENOMEM;

	ret = fdt_create_empty_tree(*blob, XRT_MAX_BLOB_SIZE);
	if (ret) {
		ret = -EINVAL;
		dev_err(dev, "format blob failed, ret = %d", ret);
		goto failed;
	}

	ret = fdt_next_node(*blob, -1, NULL);
	if (ret < 0) {
		ret = -EINVAL;
		dev_err(dev, "No Node, ret = %d", ret);
		goto failed;
	}

	ret = fdt_add_subnode(*blob, 0, XRT_MD_NODE_ENDPOINTS);
	if (ret < 0) {
		ret = -EINVAL;
		dev_err(dev, "add node failed, ret = %d", ret);
		goto failed;
	}

	return 0;

failed:
	vfree(*blob);
	*blob = NULL;

	return ret;
}
EXPORT_SYMBOL_GPL(xrt_md_create);

char *xrt_md_dup(struct device *dev, const char *blob)
{
	char *dup_blob;
	int ret;

	ret = xrt_md_create(dev, &dup_blob);
	if (ret)
		return NULL;
	ret = xrt_md_overlay(dev, dup_blob, -1, blob, -1, 0);
	if (ret) {
		vfree(dup_blob);
		return NULL;
	}

	return dup_blob;
}
EXPORT_SYMBOL_GPL(xrt_md_dup);

int xrt_md_del_endpoint(struct device *dev, char *blob, const char *ep_name,
			const char *compat)
{
	int ep_offset;
	int ret;

	ret = xrt_md_get_endpoint(dev, blob, ep_name, compat, &ep_offset);
	if (ret) {
		dev_err(dev, "can not find ep %s", ep_name);
		return -EINVAL;
	}

	ret = fdt_del_node(blob, ep_offset);
	if (ret) {
		ret = -EINVAL;
		dev_err(dev, "delete node %s failed, ret %d", ep_name, ret);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(xrt_md_del_endpoint);

static int __xrt_md_add_endpoint(struct device *dev, char *blob,
				 struct xrt_md_endpoint *ep, int *offset,
				 const char *parent)
{
	int parent_offset = 0;
	u32 val, count = 0;
	int ep_offset = 0;
	u64 io_range[2];
	char comp[128];
	int ret = 0;

	if (!ep->ep_name) {
		dev_err(dev, "empty name");
		return -EINVAL;
	}

	if (parent) {
		ret = xrt_md_get_endpoint(dev, blob, parent, NULL, &parent_offset);
		if (ret) {
			dev_err(dev, "invalid blob, ret = %d", ret);
			return -EINVAL;
		}
	}

	ep_offset = xrt_md_add_node(dev, blob, parent_offset, ep->ep_name);
	if (ep_offset < 0) {
		dev_err(dev, "add endpoint failed, ret = %d", ret);
		return -EINVAL;
	}
	if (offset)
		*offset = ep_offset;

	if (ep->size != 0) {
		val = cpu_to_be32(ep->bar_index);
		ret = xrt_md_setprop(dev, blob, ep_offset, XRT_MD_PROP_BAR_IDX,
				     &val, sizeof(u32));
		if (ret) {
			dev_err(dev, "set %s failed, ret %d",
				XRT_MD_PROP_BAR_IDX, ret);
			goto failed;
		}
		io_range[0] = cpu_to_be64((u64)ep->bar_off);
		io_range[1] = cpu_to_be64((u64)ep->size);
		ret = xrt_md_setprop(dev, blob, ep_offset, XRT_MD_PROP_IO_OFFSET,
				     io_range, sizeof(io_range));
		if (ret) {
			dev_err(dev, "set %s failed, ret %d",
				XRT_MD_PROP_IO_OFFSET, ret);
			goto failed;
		}
	}

	if (ep->compat) {
		if (ep->compat_ver) {
			count = snprintf(comp, sizeof(comp) - 1,
					 "%s-%s", ep->compat, ep->compat_ver);
			count++;
		}
		if (count >= sizeof(comp)) {
			ret = -EINVAL;
			goto failed;
		}

		count += snprintf(comp + count, sizeof(comp) - count - 1,
				  "%s", ep->compat);
		count++;
		if (count >= sizeof(comp)) {
			ret = -EINVAL;
			goto failed;
		}

		ret = xrt_md_setprop(dev, blob, ep_offset, XRT_MD_PROP_COMPATIBLE,
				     comp, count);
		if (ret) {
			dev_err(dev, "set %s failed, ret %d",
				XRT_MD_PROP_COMPATIBLE, ret);
			goto failed;
		}
	}

failed:
	if (ret)
		xrt_md_del_endpoint(dev, blob, ep->ep_name, NULL);

	return ret;
}

int xrt_md_add_endpoint(struct device *dev, char *blob,
			struct xrt_md_endpoint *ep)
{
	return __xrt_md_add_endpoint(dev, blob, ep, NULL, XRT_MD_NODE_ENDPOINTS);
}
EXPORT_SYMBOL_GPL(xrt_md_add_endpoint);

int xrt_md_find_endpoint(struct device *dev, const char *blob,
			 const char *ep_name, const char *compat,
			 const char **epname)
{
	int offset;
	int ret;

	ret = xrt_md_get_endpoint(dev, blob, ep_name, compat,
				  &offset);
	if (ret)
		return ret;

	if (epname) {
		*epname = fdt_get_name(blob, offset, NULL);
		if (!*epname)
			return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(xrt_md_find_endpoint);

int xrt_md_get_prop(struct device *dev, const char *blob, const char *ep_name,
		    const char *compat, const char *prop,
		    const void **val, int *size)
{
	int offset;
	int ret;

	if (!val) {
		dev_err(dev, "val is null");
		return -EINVAL;
	}

	*val = NULL;
	ret = xrt_md_get_node(dev, blob, ep_name, compat, &offset);
	if (ret)
		return ret;

	*val = fdt_getprop(blob, offset, prop, size);
	if (!*val) {
		dev_dbg(dev, "get ep %s, prop %s failed", ep_name, prop);
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(xrt_md_get_prop);

int xrt_md_set_prop(struct device *dev, char *blob,
		    const char *ep_name, const char *compat,
		    const char *prop, const void *val, int size)
{
	int offset;
	int ret;

	ret = xrt_md_get_node(dev, blob, ep_name, compat, &offset);
	if (ret)
		return ret;

	ret = xrt_md_setprop(dev, blob, offset, prop, val, size);
	if (ret)
		dev_err(dev, "set prop %s failed, ret = %d", prop, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(xrt_md_set_prop);

int xrt_md_copy_endpoint(struct device *dev, char *blob, const char *src_blob,
			 const char *ep_name, const char *compat,
			 const char *new_ep_name)
{
	const char *newepnm = new_ep_name ? new_ep_name : ep_name;
	struct xrt_md_endpoint ep = {0};
	int offset, target;
	const char *parent;
	int ret;

	ret = xrt_md_get_endpoint(dev, src_blob, ep_name, compat,
				  &offset);
	if (ret)
		return -EINVAL;

	ret = xrt_md_get_endpoint(dev, blob, newepnm, compat, &target);
	if (ret) {
		ep.ep_name = newepnm;
		parent = fdt_parent_offset(src_blob, offset) == 0 ? NULL : XRT_MD_NODE_ENDPOINTS;
		ret = __xrt_md_add_endpoint(dev, blob, &ep, &target, parent);
		if (ret)
			return -EINVAL;
	}

	ret = xrt_md_overlay(dev, blob, target, src_blob, offset, 0);
	if (ret)
		dev_err(dev, "overlay failed, ret = %d", ret);

	return ret;
}
EXPORT_SYMBOL_GPL(xrt_md_copy_endpoint);

int xrt_md_get_next_endpoint(struct device *dev, const char *blob,
			     const char *ep_name, const char *compat,
			     char **next_ep, char **next_compat)
{
	int offset, ret;

	*next_ep = NULL;
	*next_compat = NULL;
	if (!ep_name) {
		ret = xrt_md_get_endpoint(dev, blob, XRT_MD_NODE_ENDPOINTS, NULL,
					  &offset);
	} else {
		ret = xrt_md_get_endpoint(dev, blob, ep_name, compat,
					  &offset);
	}

	if (ret)
		return -EINVAL;

	if (ep_name)
		offset = fdt_next_subnode(blob, offset);
	else
		offset = fdt_first_subnode(blob, offset);
	if (offset < 0)
		return -EINVAL;

	*next_ep = (char *)fdt_get_name(blob, offset, NULL);
	*next_compat = (char *)fdt_stringlist_get(blob, offset, XRT_MD_PROP_COMPATIBLE,
						  0, NULL);

	return 0;
}
EXPORT_SYMBOL_GPL(xrt_md_get_next_endpoint);

int xrt_md_get_compatible_endpoint(struct device *dev, const char *blob,
				   const char *compat, const char **ep_name)
{
	int ep_offset;

	ep_offset = fdt_node_offset_by_compatible(blob, -1, compat);
	if (ep_offset < 0) {
		*ep_name = NULL;
		return -ENOENT;
	}

	*ep_name = fdt_get_name(blob, ep_offset, NULL);

	return 0;
}
EXPORT_SYMBOL_GPL(xrt_md_get_compatible_endpoint);

int xrt_md_pack(struct device *dev, char *blob)
{
	int ret;

	ret = fdt_pack(blob);
	if (ret) {
		ret = -EINVAL;
		dev_err(dev, "pack failed %d", ret);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(xrt_md_pack);

int xrt_md_get_interface_uuids(struct device *dev, const char *blob,
			       u32 num_uuids, uuid_t *interface_uuids)
{
	int offset, count = 0;
	const char *uuid_str;
	int ret;

	ret = xrt_md_get_endpoint(dev, blob, XRT_MD_NODE_INTERFACES, NULL, &offset);
	if (ret)
		return -ENOENT;

	for (offset = fdt_first_subnode(blob, offset);
	    offset >= 0;
	    offset = fdt_next_subnode(blob, offset), count++) {
		uuid_str = fdt_getprop(blob, offset, XRT_MD_PROP_INTERFACE_UUID,
				       NULL);
		if (!uuid_str) {
			dev_err(dev, "empty interface uuid node");
			return -EINVAL;
		}

		if (!num_uuids)
			continue;

		if (count == num_uuids) {
			dev_err(dev, "too many interface uuid in blob");
			return -EINVAL;
		}

		if (interface_uuids && count < num_uuids) {
			ret = xrt_md_trans_str2uuid(dev, uuid_str,
						    &interface_uuids[count]);
			if (ret)
				return -EINVAL;
		}
	}
	if (!count)
		count = -ENOENT;

	return count;
}
EXPORT_SYMBOL_GPL(xrt_md_get_interface_uuids);
