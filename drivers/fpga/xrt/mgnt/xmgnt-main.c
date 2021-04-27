// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Alveo FPGA MGNT PF entry point driver
 *
 * Copyright (C) 2020-2021 Xilinx, Inc.
 *
 * Authors:
 *	Sonal Santan <sonals@xilinx.com>
 */

#include <linux/firmware.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include "xclbin-helper.h"
#include "metadata.h"
#include "xleaf.h"
#include <linux/xrt/xmgnt-ioctl.h>
#include "xleaf/devctl.h"
#include "xmgnt-main.h"
#include "xrt-mgr.h"
#include "xleaf/icap.h"
#include "xleaf/axigate.h"
#include "xmgnt.h"

#define XMGNT_MAIN "xmgnt_main"
#define XMGNT_SUPP_XCLBIN_MAJOR 2

#define XMGNT_FLAG_FLASH_READY	1
#define XMGNT_FLAG_DEVCTL_READY	2

#define XMGNT_UUID_STR_LEN	(UUID_SIZE * 2 + 1)

struct xmgnt_main {
	struct xrt_device *xdev;
	struct axlf *firmware_blp;
	struct axlf *firmware_plp;
	struct axlf *firmware_ulp;
	u32 flags;
	struct fpga_manager *fmgr;
	struct mutex lock; /* busy lock */
	uuid_t *blp_interface_uuids;
	u32 blp_interface_uuid_num;
};

/*
 * VBNV stands for Vendor, BoardID, Name, Version. It is a string
 * which describes board and shell.
 *
 * Caller is responsible for freeing the returned string.
 */
char *xmgnt_get_vbnv(struct xrt_device *xdev)
{
	struct xmgnt_main *xmm = xrt_get_drvdata(xdev);
	const char *vbnv;
	char *ret;
	int i;

	if (xmm->firmware_plp)
		vbnv = xmm->firmware_plp->header.platform_vbnv;
	else if (xmm->firmware_blp)
		vbnv = xmm->firmware_blp->header.platform_vbnv;
	else
		return NULL;

	ret = kstrdup(vbnv, GFP_KERNEL);
	if (!ret)
		return NULL;

	for (i = 0; i < strlen(ret); i++) {
		if (ret[i] == ':' || ret[i] == '.')
			ret[i] = '_';
	}
	return ret;
}

static int get_dev_uuid(struct xrt_device *xdev, char *uuidstr, size_t len)
{
	struct xrt_devctl_rw devctl_arg = { 0 };
	struct xrt_device *devctl_leaf;
	char uuid_buf[UUID_SIZE];
	uuid_t uuid;
	int err;

	devctl_leaf = xleaf_get_leaf_by_epname(xdev, XRT_MD_NODE_BLP_ROM);
	if (!devctl_leaf) {
		xrt_err(xdev, "can not get %s", XRT_MD_NODE_BLP_ROM);
		return -EINVAL;
	}

	devctl_arg.xdr_id = XRT_DEVCTL_ROM_UUID;
	devctl_arg.xdr_buf = uuid_buf;
	devctl_arg.xdr_len = sizeof(uuid_buf);
	devctl_arg.xdr_offset = 0;
	err = xleaf_call(devctl_leaf, XRT_DEVCTL_READ, &devctl_arg);
	xleaf_put_leaf(xdev, devctl_leaf);
	if (err) {
		xrt_err(xdev, "can not get uuid: %d", err);
		return err;
	}
	import_uuid(&uuid, uuid_buf);
	xrt_md_trans_uuid2str(&uuid, uuidstr);

	return 0;
}

int xmgnt_hot_reset(struct xrt_device *xdev)
{
	int ret = xleaf_broadcast_event(xdev, XRT_EVENT_PRE_HOT_RESET, false);

	if (ret) {
		xrt_err(xdev, "offline failed, hot reset is canceled");
		return ret;
	}

	xleaf_hot_reset(xdev);
	xleaf_broadcast_event(xdev, XRT_EVENT_POST_HOT_RESET, false);
	return 0;
}

static ssize_t reset_store(struct device *dev, struct device_attribute *da,
			   const char *buf, size_t count)
{
	struct xrt_device *xdev = to_xrt_dev(dev);

	xmgnt_hot_reset(xdev);
	return count;
}
static DEVICE_ATTR_WO(reset);

static ssize_t VBNV_show(struct device *dev, struct device_attribute *da, char *buf)
{
	struct xrt_device *xdev = to_xrt_dev(dev);
	ssize_t ret;
	char *vbnv;

	vbnv = xmgnt_get_vbnv(xdev);
	if (!vbnv)
		return -EINVAL;
	ret = sprintf(buf, "%s\n", vbnv);
	kfree(vbnv);
	return ret;
}
static DEVICE_ATTR_RO(VBNV);

/* logic uuid is the uuid uniquely identfy the partition */
static ssize_t logic_uuids_show(struct device *dev, struct device_attribute *da, char *buf)
{
	struct xrt_device *xdev = to_xrt_dev(dev);
	char uuid[XMGNT_UUID_STR_LEN];
	ssize_t ret;

	/* Getting UUID pointed to by VSEC, should be the same as logic UUID of BLP. */
	ret = get_dev_uuid(xdev, uuid, sizeof(uuid));
	if (ret)
		return ret;
	ret = sprintf(buf, "%s\n", uuid);
	return ret;
}
static DEVICE_ATTR_RO(logic_uuids);

static ssize_t interface_uuids_show(struct device *dev, struct device_attribute *da, char *buf)
{
	struct xrt_device *xdev = to_xrt_dev(dev);
	struct xmgnt_main *xmm = xrt_get_drvdata(xdev);
	ssize_t ret = 0;
	u32 i;

	for (i = 0; i < xmm->blp_interface_uuid_num; i++) {
		char uuidstr[XMGNT_UUID_STR_LEN];

		xrt_md_trans_uuid2str(&xmm->blp_interface_uuids[i], uuidstr);
		ret += sprintf(buf + ret, "%s\n", uuidstr);
	}
	return ret;
}
static DEVICE_ATTR_RO(interface_uuids);

static struct attribute *xmgnt_main_attrs[] = {
	&dev_attr_reset.attr,
	&dev_attr_VBNV.attr,
	&dev_attr_logic_uuids.attr,
	&dev_attr_interface_uuids.attr,
	NULL,
};

static const struct attribute_group xmgnt_main_attrgroup = {
	.attrs = xmgnt_main_attrs,
};

static int load_firmware_from_disk(struct xrt_device *xdev, struct axlf **fw_buf, size_t *len)
{
	char uuid[XMGNT_UUID_STR_LEN];
	const struct firmware *fw;
	char fw_name[256];
	int err = 0;

	*len = 0;
	err = get_dev_uuid(xdev, uuid, sizeof(uuid));
	if (err)
		return err;

	snprintf(fw_name, sizeof(fw_name), "xilinx/%s/partition.xsabin", uuid);
	xrt_info(xdev, "try loading fw: %s", fw_name);

	err = request_firmware(&fw, fw_name, DEV(xdev));
	if (err)
		return err;

	*fw_buf = vmalloc(fw->size);
	if (!*fw_buf) {
		release_firmware(fw);
		return -ENOMEM;
	}

	*len = fw->size;
	memcpy(*fw_buf, fw->data, fw->size);

	release_firmware(fw);
	return 0;
}

static const struct axlf *xmgnt_get_axlf_firmware(struct xmgnt_main *xmm, enum provider_kind kind)
{
	switch (kind) {
	case XMGNT_BLP:
		return xmm->firmware_blp;
	case XMGNT_PLP:
		return xmm->firmware_plp;
	case XMGNT_ULP:
		return xmm->firmware_ulp;
	default:
		xrt_err(xmm->xdev, "unknown axlf kind: %d", kind);
		return NULL;
	}
}

/* The caller needs to free the returned dtb buffer */
char *xmgnt_get_dtb(struct xrt_device *xdev, enum provider_kind kind)
{
	struct xmgnt_main *xmm = xrt_get_drvdata(xdev);
	const struct axlf *provider;
	char *dtb = NULL;
	int rc;

	provider = xmgnt_get_axlf_firmware(xmm, kind);
	if (!provider)
		return dtb;

	rc = xrt_xclbin_get_metadata(DEV(xdev), provider, &dtb);
	if (rc)
		xrt_err(xdev, "failed to find dtb: %d", rc);
	return dtb;
}

/* The caller needs to free the returned uuid buffer */
static const char *get_uuid_from_firmware(struct xrt_device *xdev, const struct axlf *xclbin)
{
	const void *uuiddup = NULL;
	const void *uuid = NULL;
	void *dtb = NULL;
	int rc;

	rc = xrt_xclbin_get_section(DEV(xdev), xclbin, PARTITION_METADATA, &dtb, NULL);
	if (rc)
		return NULL;

	rc = xrt_md_get_prop(DEV(xdev), dtb, NULL, NULL, XRT_MD_PROP_LOGIC_UUID, &uuid, NULL);
	if (!rc)
		uuiddup = kstrdup(uuid, GFP_KERNEL);
	vfree(dtb);
	return uuiddup;
}

static bool is_valid_firmware(struct xrt_device *xdev,
			      const struct axlf *xclbin, size_t fw_len)
{
	const char *fw_buf = (const char *)xclbin;
	size_t axlflen = xclbin->header.length;
	char dev_uuid[XMGNT_UUID_STR_LEN];
	const char *fw_uuid;
	int err;

	err = get_dev_uuid(xdev, dev_uuid, sizeof(dev_uuid));
	if (err)
		return false;

	if (memcmp(fw_buf, XCLBIN_VERSION2, sizeof(XCLBIN_VERSION2)) != 0) {
		xrt_err(xdev, "unknown fw format");
		return false;
	}

	if (axlflen > fw_len) {
		xrt_err(xdev, "truncated fw, length: %zu, expect: %zu", fw_len, axlflen);
		return false;
	}

	if (xclbin->header.version_major != XMGNT_SUPP_XCLBIN_MAJOR) {
		xrt_err(xdev, "firmware is not supported");
		return false;
	}

	fw_uuid = get_uuid_from_firmware(xdev, xclbin);
	if (!fw_uuid || strncmp(fw_uuid, dev_uuid, sizeof(dev_uuid)) != 0) {
		xrt_err(xdev, "bad fw UUID: %s, expect: %s",
			fw_uuid ? fw_uuid : "<none>", dev_uuid);
		kfree(fw_uuid);
		return false;
	}

	kfree(fw_uuid);
	return true;
}

int xmgnt_get_provider_uuid(struct xrt_device *xdev, enum provider_kind kind, uuid_t *uuid)
{
	struct xmgnt_main *xmm = xrt_get_drvdata(xdev);
	const struct axlf *fwbuf;
	const char *fw_uuid;
	int rc = -ENOENT;

	mutex_lock(&xmm->lock);

	fwbuf = xmgnt_get_axlf_firmware(xmm, kind);
	if (!fwbuf)
		goto done;

	fw_uuid = get_uuid_from_firmware(xdev, fwbuf);
	if (!fw_uuid)
		goto done;

	rc = xrt_md_trans_str2uuid(DEV(xdev), fw_uuid, uuid);
	kfree(fw_uuid);

done:
	mutex_unlock(&xmm->lock);
	return rc;
}

static int xmgnt_create_blp(struct xmgnt_main *xmm)
{
	const struct axlf *provider = xmgnt_get_axlf_firmware(xmm, XMGNT_BLP);
	struct xrt_device *xdev = xmm->xdev;
	int rc = 0;
	char *dtb = NULL;

	dtb = xmgnt_get_dtb(xdev, XMGNT_BLP);
	if (!dtb) {
		xrt_err(xdev, "did not get BLP metadata");
		return -EINVAL;
	}

	rc = xmgnt_process_xclbin(xmm->xdev, xmm->fmgr, provider, XMGNT_BLP);
	if (rc) {
		xrt_err(xdev, "failed to process BLP: %d", rc);
		goto failed;
	}

	rc = xleaf_create_group(xdev, dtb);
	if (rc < 0)
		xrt_err(xdev, "failed to create BLP group: %d", rc);
	else
		rc = 0;

	WARN_ON(xmm->blp_interface_uuids);
	rc = xrt_md_get_interface_uuids(&xdev->dev, dtb, 0, NULL);
	if (rc > 0) {
		xmm->blp_interface_uuid_num = rc;
		xmm->blp_interface_uuids =
			kcalloc(xmm->blp_interface_uuid_num, sizeof(uuid_t), GFP_KERNEL);
		if (!xmm->blp_interface_uuids) {
			rc = -ENOMEM;
			goto failed;
		}
		xrt_md_get_interface_uuids(&xdev->dev, dtb, xmm->blp_interface_uuid_num,
					   xmm->blp_interface_uuids);
	}

failed:
	vfree(dtb);
	return rc;
}

static int xmgnt_load_firmware(struct xmgnt_main *xmm)
{
	struct xrt_device *xdev = xmm->xdev;
	size_t fwlen;
	int rc;

	rc = load_firmware_from_disk(xdev, &xmm->firmware_blp, &fwlen);
	if (!rc && is_valid_firmware(xdev, xmm->firmware_blp, fwlen))
		xmgnt_create_blp(xmm);
	else
		xrt_err(xdev, "failed to find firmware, giving up: %d", rc);
	return rc;
}

static void xmgnt_main_event_cb(struct xrt_device *xdev, void *arg)
{
	struct xmgnt_main *xmm = xrt_get_drvdata(xdev);
	struct xrt_event *evt = (struct xrt_event *)arg;
	enum xrt_events e = evt->xe_evt;
	struct xrt_device *leaf;
	enum xrt_subdev_id id;

	id = evt->xe_subdev.xevt_subdev_id;
	switch (e) {
	case XRT_EVENT_POST_CREATION: {
		if (id == XRT_SUBDEV_DEVCTL && !(xmm->flags & XMGNT_FLAG_DEVCTL_READY)) {
			leaf = xleaf_get_leaf_by_epname(xdev, XRT_MD_NODE_BLP_ROM);
			if (leaf) {
				xmm->flags |= XMGNT_FLAG_DEVCTL_READY;
				xleaf_put_leaf(xdev, leaf);
			}
		} else if (id == XRT_SUBDEV_QSPI && !(xmm->flags & XMGNT_FLAG_FLASH_READY)) {
			xmm->flags |= XMGNT_FLAG_FLASH_READY;
		} else {
			break;
		}

		if (xmm->flags & XMGNT_FLAG_DEVCTL_READY)
			xmgnt_load_firmware(xmm);
		break;
	}
	case XRT_EVENT_PRE_REMOVAL:
		break;
	default:
		xrt_dbg(xdev, "ignored event %d", e);
		break;
	}
}

static int xmgnt_main_probe(struct xrt_device *xdev)
{
	struct xmgnt_main *xmm;

	xrt_info(xdev, "probing...");

	xmm = devm_kzalloc(DEV(xdev), sizeof(*xmm), GFP_KERNEL);
	if (!xmm)
		return -ENOMEM;

	xmm->xdev = xdev;
	xmm->fmgr = xmgnt_fmgr_probe(xdev);
	if (IS_ERR(xmm->fmgr))
		return PTR_ERR(xmm->fmgr);

	xrt_set_drvdata(xdev, xmm);
	mutex_init(&xmm->lock);

	/* Ready to handle req thru sysfs nodes. */
	if (sysfs_create_group(&DEV(xdev)->kobj, &xmgnt_main_attrgroup))
		xrt_err(xdev, "failed to create sysfs group");
	return 0;
}

static void xmgnt_main_remove(struct xrt_device *xdev)
{
	struct xmgnt_main *xmm = xrt_get_drvdata(xdev);

	/* By now, group driver should prevent any inter-leaf call. */

	xrt_info(xdev, "leaving...");

	kfree(xmm->blp_interface_uuids);
	vfree(xmm->firmware_blp);
	vfree(xmm->firmware_plp);
	vfree(xmm->firmware_ulp);
	xmgnt_region_cleanup_all(xdev);
	xmgnt_fmgr_remove(xmm->fmgr);
	sysfs_remove_group(&DEV(xdev)->kobj, &xmgnt_main_attrgroup);
}

static int
xmgnt_mainleaf_call(struct xrt_device *xdev, u32 cmd, void *arg)
{
	struct xmgnt_main *xmm = xrt_get_drvdata(xdev);
	int ret = 0;

	switch (cmd) {
	case XRT_XLEAF_EVENT:
		xmgnt_main_event_cb(xdev, arg);
		break;
	case XRT_MGNT_MAIN_GET_AXLF_SECTION: {
		struct xrt_mgnt_main_get_axlf_section *get =
			(struct xrt_mgnt_main_get_axlf_section *)arg;
		const struct axlf *firmware = xmgnt_get_axlf_firmware(xmm, get->xmmigas_axlf_kind);

		if (!firmware) {
			ret = -ENOENT;
		} else {
			ret = xrt_xclbin_get_section(DEV(xdev), firmware,
						     get->xmmigas_section_kind,
						     &get->xmmigas_section,
						     &get->xmmigas_section_size);
		}
		break;
	}
	case XRT_MGNT_MAIN_GET_VBNV: {
		char **vbnv_p = (char **)arg;

		*vbnv_p = xmgnt_get_vbnv(xdev);
		if (!*vbnv_p)
			ret = -EINVAL;
		break;
	}
	default:
		xrt_err(xdev, "unknown cmd: %d", cmd);
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int xmgnt_main_open(struct inode *inode, struct file *file)
{
	struct xrt_device *xdev = xleaf_devnode_open(inode);

	/* Device may have gone already when we get here. */
	if (!xdev)
		return -ENODEV;

	xrt_info(xdev, "opened");
	file->private_data = xrt_get_drvdata(xdev);
	return 0;
}

static int xmgnt_main_close(struct inode *inode, struct file *file)
{
	struct xmgnt_main *xmm = file->private_data;

	xleaf_devnode_close(inode);

	xrt_info(xmm->xdev, "closed");
	return 0;
}

/*
 * Called for xclbin download xclbin load ioctl.
 */
static int xmgnt_bitstream_axlf_fpga_mgr(struct xmgnt_main *xmm, void *axlf, size_t size)
{
	int ret;

	WARN_ON(!mutex_is_locked(&xmm->lock));

	/*
	 * Should any error happens during download, we can't trust
	 * the cached xclbin any more.
	 */
	vfree(xmm->firmware_ulp);
	xmm->firmware_ulp = NULL;

	ret = xmgnt_process_xclbin(xmm->xdev, xmm->fmgr, axlf, XMGNT_ULP);
	if (ret == 0)
		xmm->firmware_ulp = axlf;

	return ret;
}

static int bitstream_axlf_ioctl(struct xmgnt_main *xmm, const void __user *arg)
{
	struct xmgnt_ioc_bitstream_axlf ioc_obj = { 0 };
	struct axlf xclbin_obj = { {0} };
	size_t copy_buffer_size = 0;
	void *copy_buffer = NULL;
	int ret = 0;

	if (copy_from_user((void *)&ioc_obj, arg, sizeof(ioc_obj)))
		return -EFAULT;
	if (copy_from_user((void *)&xclbin_obj, ioc_obj.xclbin, sizeof(xclbin_obj)))
		return -EFAULT;
	if (memcmp(xclbin_obj.magic, XCLBIN_VERSION2, sizeof(XCLBIN_VERSION2)))
		return -EINVAL;

	copy_buffer_size = xclbin_obj.header.length;
	if (copy_buffer_size > XCLBIN_MAX_SIZE || copy_buffer_size < sizeof(xclbin_obj))
		return -EINVAL;
	if (xclbin_obj.header.version_major != XMGNT_SUPP_XCLBIN_MAJOR)
		return -EINVAL;

	copy_buffer = vmalloc(copy_buffer_size);
	if (!copy_buffer)
		return -ENOMEM;

	if (copy_from_user(copy_buffer, ioc_obj.xclbin, copy_buffer_size)) {
		vfree(copy_buffer);
		return -EFAULT;
	}

	ret = xmgnt_bitstream_axlf_fpga_mgr(xmm, copy_buffer, copy_buffer_size);
	if (ret)
		vfree(copy_buffer);

	return ret;
}

static long xmgnt_main_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct xmgnt_main *xmm = filp->private_data;
	long result = 0;

	if (_IOC_TYPE(cmd) != XMGNT_IOC_MAGIC)
		return -ENOTTY;

	mutex_lock(&xmm->lock);

	xrt_info(xmm->xdev, "ioctl cmd %d, arg %ld", cmd, arg);
	switch (cmd) {
	case XMGNT_IOCICAPDOWNLOAD_AXLF:
		result = bitstream_axlf_ioctl(xmm, (const void __user *)arg);
		break;
	default:
		result = -ENOTTY;
		break;
	}

	mutex_unlock(&xmm->lock);
	return result;
}

static struct xrt_dev_endpoints xrt_mgnt_main_endpoints[] = {
	{
		.xse_names = (struct xrt_dev_ep_names []){
			{ .ep_name = XRT_MD_NODE_MGNT_MAIN },
			{ NULL },
		},
		.xse_min_ep = 1,
	},
	{ 0 },
};

static struct xrt_driver xmgnt_main_driver = {
	.driver	= {
		.name = XMGNT_MAIN,
	},
	.file_ops = {
		.xsf_ops = {
			.owner = THIS_MODULE,
			.open = xmgnt_main_open,
			.release = xmgnt_main_close,
			.unlocked_ioctl = xmgnt_main_ioctl,
		},
		.xsf_dev_name = "xmgnt",
	},
	.subdev_id = XRT_SUBDEV_MGNT_MAIN,
	.endpoints = xrt_mgnt_main_endpoints,
	.probe = xmgnt_main_probe,
	.remove = xmgnt_main_remove,
	.leaf_call = xmgnt_mainleaf_call,
};

int xmgnt_register_leaf(void)
{
	return xrt_register_driver(&xmgnt_main_driver);
}

void xmgnt_unregister_leaf(void)
{
	xrt_unregister_driver(&xmgnt_main_driver);
}
