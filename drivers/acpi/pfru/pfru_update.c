// SPDX-License-Identifier: GPL-2.0
/*
 * ACPI Platform Firmware Runtime Update Device Driver
 *
 * Copyright (C) 2021 Intel Corporation
 * Author: Chen Yu <yu.c.chen@intel.com>
 */
#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/efi.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/uio.h>
#include <linux/uuid.h>

#include <uapi/linux/pfru.h>

#define PFRU_UUID		"ECF9533B-4A3C-4E89-939E-C77112601C6D"
#define PFRU_CODE_INJ_UUID		"B2F84B79-7B6E-4E45-885F-3FB9BB185402"
#define PFRU_DRV_UPDATE_UUID		"4569DD8C-75F1-429A-A3D6-24DE8097A0DF"

#define PFRU_FUNC_STANDARD_QUERY	0
#define PFRU_FUNC_QUERY_UPDATE_CAP	1
#define PFRU_FUNC_QUERY_BUF		2
#define PFRU_FUNC_START		3

#define PFRU_CODE_INJECT_TYPE	1
#define PFRU_DRIVER_UPDATE_TYPE	2

#define PFRU_REVID_1		1
#define PFRU_REVID_2		2
#define PFRU_DEFAULT_REV_ID	PFRU_REVID_1

enum cap_index {
	CAP_STATUS_IDX = 0,
	CAP_UPDATE_IDX = 1,
	CAP_CODE_TYPE_IDX = 2,
	CAP_FW_VER_IDX = 3,
	CAP_CODE_RT_VER_IDX = 4,
	CAP_DRV_TYPE_IDX = 5,
	CAP_DRV_RT_VER_IDX = 6,
	CAP_DRV_SVN_IDX = 7,
	CAP_PLAT_ID_IDX = 8,
	CAP_OEM_ID_IDX = 9,
	CAP_OEM_INFO_IDX = 10,
	CAP_NR_IDX = 11
};

enum buf_index {
	BUF_STATUS_IDX = 0,
	BUF_EXT_STATUS_IDX = 1,
	BUF_ADDR_LOW_IDX = 2,
	BUF_ADDR_HI_IDX = 3,
	BUF_SIZE_IDX = 4,
	BUF_NR_IDX = 5
};

enum update_index {
	UPDATE_STATUS_IDX = 0,
	UPDATE_EXT_STATUS_IDX = 1,
	UPDATE_AUTH_TIME_LOW_IDX = 2,
	UPDATE_AUTH_TIME_HI_IDX = 3,
	UPDATE_EXEC_TIME_LOW_IDX = 4,
	UPDATE_EXEC_TIME_HI_IDX = 5,
	UPDATE_NR_IDX = 6
};

enum pfru_start_action {
	START_STAGE = 0,
	START_ACTIVATE = 1,
	START_STAGE_ACTIVATE = 2,
};

struct pfru_device {
	guid_t uuid, code_uuid, drv_uuid;
	u32 rev_id, index;
	struct device *parent_dev;
	struct miscdevice miscdev;
};

static DEFINE_IDA(pfru_ida);

static inline int pfru_valid_revid(u32 id)
{
	return id == PFRU_REVID_1 || id == PFRU_REVID_2;
}

static inline struct pfru_device *to_pfru_dev(struct file *file)
{
	return container_of(file->private_data, struct pfru_device, miscdev);
}

static int query_capability(struct pfru_update_cap_info *cap_hdr,
			    struct pfru_device *pfru_dev)
{
	acpi_handle handle = ACPI_HANDLE(pfru_dev->parent_dev);
	union acpi_object *out_obj;
	int ret = -EINVAL;

	out_obj = acpi_evaluate_dsm_typed(handle, &pfru_dev->uuid,
					  pfru_dev->rev_id,
					  PFRU_FUNC_QUERY_UPDATE_CAP,
					  NULL, ACPI_TYPE_PACKAGE);
	if (!out_obj)
		return ret;

	if (out_obj->package.count < CAP_NR_IDX)
		goto free_acpi_buffer;

	if (out_obj->package.elements[CAP_STATUS_IDX].type != ACPI_TYPE_INTEGER)
		goto free_acpi_buffer;

	cap_hdr->status = out_obj->package.elements[CAP_STATUS_IDX].integer.value;

	if (out_obj->package.elements[CAP_UPDATE_IDX].type != ACPI_TYPE_INTEGER)
		goto free_acpi_buffer;

	cap_hdr->update_cap = out_obj->package.elements[CAP_UPDATE_IDX].integer.value;

	if (out_obj->package.elements[CAP_CODE_TYPE_IDX].type != ACPI_TYPE_BUFFER)
		goto free_acpi_buffer;

	memcpy(&cap_hdr->code_type,
	       out_obj->package.elements[CAP_CODE_TYPE_IDX].buffer.pointer,
	       out_obj->package.elements[CAP_CODE_TYPE_IDX].buffer.length);

	if (out_obj->package.elements[CAP_FW_VER_IDX].type != ACPI_TYPE_INTEGER)
		goto free_acpi_buffer;

	cap_hdr->fw_version =
		out_obj->package.elements[CAP_FW_VER_IDX].integer.value;

	if (out_obj->package.elements[CAP_CODE_RT_VER_IDX].type != ACPI_TYPE_INTEGER)
		goto free_acpi_buffer;

	cap_hdr->code_rt_version =
		out_obj->package.elements[CAP_CODE_RT_VER_IDX].integer.value;

	if (out_obj->package.elements[CAP_DRV_TYPE_IDX].type != ACPI_TYPE_BUFFER)
		goto free_acpi_buffer;

	memcpy(&cap_hdr->drv_type,
	       out_obj->package.elements[CAP_DRV_TYPE_IDX].buffer.pointer,
	       out_obj->package.elements[CAP_DRV_TYPE_IDX].buffer.length);

	if (out_obj->package.elements[CAP_DRV_RT_VER_IDX].type != ACPI_TYPE_INTEGER)
		goto free_acpi_buffer;

	cap_hdr->drv_rt_version =
		out_obj->package.elements[CAP_DRV_RT_VER_IDX].integer.value;

	if (out_obj->package.elements[CAP_DRV_SVN_IDX].type != ACPI_TYPE_INTEGER)
		goto free_acpi_buffer;

	cap_hdr->drv_svn =
		out_obj->package.elements[CAP_DRV_SVN_IDX].integer.value;

	if (out_obj->package.elements[CAP_PLAT_ID_IDX].type != ACPI_TYPE_BUFFER)
		goto free_acpi_buffer;

	memcpy(&cap_hdr->platform_id,
	       out_obj->package.elements[CAP_PLAT_ID_IDX].buffer.pointer,
	       out_obj->package.elements[CAP_PLAT_ID_IDX].buffer.length);

	if (out_obj->package.elements[CAP_OEM_ID_IDX].type != ACPI_TYPE_BUFFER)
		goto free_acpi_buffer;

	memcpy(&cap_hdr->oem_id,
	       out_obj->package.elements[CAP_OEM_ID_IDX].buffer.pointer,
	       out_obj->package.elements[CAP_OEM_ID_IDX].buffer.length);

	if (out_obj->package.elements[CAP_OEM_INFO_IDX].type != ACPI_TYPE_BUFFER)
		goto free_acpi_buffer;

	cap_hdr->oem_info_len =
		out_obj->package.elements[CAP_OEM_INFO_IDX].buffer.length;

	ret = 0;

free_acpi_buffer:
	ACPI_FREE(out_obj);

	return ret;
}

static int query_buffer(struct pfru_com_buf_info *info,
			struct pfru_device *pfru_dev)
{
	acpi_handle handle = ACPI_HANDLE(pfru_dev->parent_dev);
	union acpi_object *out_obj;
	int ret = -EINVAL;

	out_obj = acpi_evaluate_dsm_typed(handle, &pfru_dev->uuid,
					  pfru_dev->rev_id, PFRU_FUNC_QUERY_BUF,
					  NULL, ACPI_TYPE_PACKAGE);
	if (!out_obj)
		return ret;

	if (out_obj->package.count < BUF_NR_IDX)
		goto free_acpi_buffer;

	if (out_obj->package.elements[BUF_STATUS_IDX].type != ACPI_TYPE_INTEGER)
		goto free_acpi_buffer;

	info->status = out_obj->package.elements[BUF_STATUS_IDX].integer.value;

	if (out_obj->package.elements[BUF_EXT_STATUS_IDX].type != ACPI_TYPE_INTEGER)
		goto free_acpi_buffer;

	info->ext_status =
		out_obj->package.elements[BUF_EXT_STATUS_IDX].integer.value;

	if (out_obj->package.elements[BUF_ADDR_LOW_IDX].type != ACPI_TYPE_INTEGER)
		goto free_acpi_buffer;

	info->addr_lo =
		out_obj->package.elements[BUF_ADDR_LOW_IDX].integer.value;

	if (out_obj->package.elements[BUF_ADDR_HI_IDX].type != ACPI_TYPE_INTEGER)
		goto free_acpi_buffer;

	info->addr_hi =
		out_obj->package.elements[BUF_ADDR_HI_IDX].integer.value;

	if (out_obj->package.elements[BUF_SIZE_IDX].type != ACPI_TYPE_INTEGER)
		goto free_acpi_buffer;

	info->buf_size = out_obj->package.elements[BUF_SIZE_IDX].integer.value;

	ret = 0;

free_acpi_buffer:
	ACPI_FREE(out_obj);

	return ret;
}

static int get_image_type(struct efi_manage_capsule_image_header *img_hdr,
			  struct pfru_device *pfru_dev)
{
	efi_guid_t *image_type_id = &img_hdr->image_type_id;

	/* check whether this is a code injection or driver update */
	if (guid_equal(image_type_id, &pfru_dev->code_uuid))
		return PFRU_CODE_INJECT_TYPE;

	if (guid_equal(image_type_id, &pfru_dev->drv_uuid))
		return PFRU_DRIVER_UPDATE_TYPE;

	return -EINVAL;
}

static int adjust_efi_size(struct efi_manage_capsule_image_header *img_hdr,
			   int size)
{
	/*
	 * The (u64 hw_ins) was introduced in UEFI spec version 2,
	 * and (u64 capsule_support) was introduced in version 3.
	 * The size needs to be adjusted accordingly. That is to
	 * say, version 1 should subtract the size of hw_ins+capsule_support,
	 * and version 2 should sbstract the size of capsule_support.
	 */
	size += sizeof(struct efi_manage_capsule_image_header);
	switch (img_hdr->ver) {
	case 1:
		return size - 2 * sizeof(u64);
	case 2:
		return size - sizeof(u64);
	default:
		/* only support version 1 and 2 */
		return -EINVAL;
	}
}

static bool valid_version(const void *data, struct pfru_update_cap_info *cap,
			  struct pfru_device *pfru_dev)
{
	struct pfru_payload_hdr *payload_hdr;
	const efi_capsule_header_t *cap_hdr = data;
	struct efi_manage_capsule_header *m_hdr;
	struct efi_manage_capsule_image_header *m_img_hdr;
	struct efi_image_auth *auth;
	int type, size;

	/*
	 * Sanity check if the capsule image has a newer version
	 * than current one.
	 */
	size = cap_hdr->headersize;
	m_hdr = (struct efi_manage_capsule_header *)(data + size);
	/*
	 * Current data structure size plus variable array indicated
	 * by number of (emb_drv_cnt + payload_cnt)
	 */
	size += offsetof(struct efi_manage_capsule_header, offset_list) +
		(m_hdr->emb_drv_cnt + m_hdr->payload_cnt) * sizeof(u64);
	m_img_hdr = (struct efi_manage_capsule_image_header *)(data + size);

	type = get_image_type(m_img_hdr, pfru_dev);
	if (type < 0)
		return false;

	size = adjust_efi_size(m_img_hdr, size);
	if (size < 0)
		return false;

	auth = (struct efi_image_auth *)(data + size);
	size += sizeof(u64) + auth->auth_info.hdr.len;
	payload_hdr = (struct pfru_payload_hdr *)(data + size);

	/* finally compare the version */
	if (type == PFRU_CODE_INJECT_TYPE)
		return payload_hdr->rt_ver >= cap->code_rt_version;
	else
		return payload_hdr->rt_ver >= cap->drv_rt_version;
}

static void dump_update_result(struct pfru_updated_result *result,
			       struct pfru_device *pfru_dev)
{
	dev_dbg(pfru_dev->parent_dev, "Update result:\n");
	dev_dbg(pfru_dev->parent_dev, "Status:%d\n", result->status);
	dev_dbg(pfru_dev->parent_dev, "Extended Status:%d\n", result->ext_status);
	dev_dbg(pfru_dev->parent_dev, "Authentication Time Low:%lld\n",
		result->low_auth_time);
	dev_dbg(pfru_dev->parent_dev, "Authentication Time High:%lld\n",
		result->high_auth_time);
	dev_dbg(pfru_dev->parent_dev, "Execution Time Low:%lld\n",
		result->low_exec_time);
	dev_dbg(pfru_dev->parent_dev, "Execution Time High:%lld\n",
		result->high_exec_time);
}

static int start_acpi_update(int action, struct pfru_device *pfru_dev)
{
	union acpi_object *out_obj, in_obj, in_buf;
	struct pfru_updated_result update_result;
	acpi_handle handle;
	int ret = -EINVAL;

	memset(&in_obj, 0, sizeof(in_obj));
	memset(&in_buf, 0, sizeof(in_buf));
	in_obj.type = ACPI_TYPE_PACKAGE;
	in_obj.package.count = 1;
	in_obj.package.elements = &in_buf;
	in_buf.type = ACPI_TYPE_INTEGER;
	in_buf.integer.value = action;

	handle = ACPI_HANDLE(pfru_dev->parent_dev);
	out_obj = acpi_evaluate_dsm_typed(handle, &pfru_dev->uuid,
					  pfru_dev->rev_id, PFRU_FUNC_START,
					  &in_obj, ACPI_TYPE_PACKAGE);
	if (!out_obj)
		return ret;

	if (out_obj->package.count < UPDATE_NR_IDX)
		goto free_acpi_buffer;

	if (out_obj->package.elements[UPDATE_STATUS_IDX].type != ACPI_TYPE_INTEGER)
		goto free_acpi_buffer;

	update_result.status =
		out_obj->package.elements[UPDATE_STATUS_IDX].integer.value;

	if (out_obj->package.elements[UPDATE_EXT_STATUS_IDX].type != ACPI_TYPE_INTEGER)
		goto free_acpi_buffer;

	update_result.ext_status =
		out_obj->package.elements[UPDATE_EXT_STATUS_IDX].integer.value;

	if (out_obj->package.elements[UPDATE_AUTH_TIME_LOW_IDX].type != ACPI_TYPE_INTEGER)
		goto free_acpi_buffer;

	update_result.low_auth_time =
		out_obj->package.elements[UPDATE_AUTH_TIME_LOW_IDX].integer.value;

	if (out_obj->package.elements[UPDATE_AUTH_TIME_HI_IDX].type != ACPI_TYPE_INTEGER)
		goto free_acpi_buffer;

	update_result.high_auth_time =
		out_obj->package.elements[UPDATE_AUTH_TIME_HI_IDX].integer.value;

	if (out_obj->package.elements[UPDATE_EXEC_TIME_LOW_IDX].type != ACPI_TYPE_INTEGER)
		goto free_acpi_buffer;

	update_result.low_exec_time =
		out_obj->package.elements[UPDATE_EXEC_TIME_LOW_IDX].integer.value;

	if (out_obj->package.elements[UPDATE_EXEC_TIME_HI_IDX].type != ACPI_TYPE_INTEGER)
		goto free_acpi_buffer;

	update_result.high_exec_time =
		out_obj->package.elements[UPDATE_EXEC_TIME_HI_IDX].integer.value;

	dump_update_result(&update_result, pfru_dev);
	ret = 0;

free_acpi_buffer:
	ACPI_FREE(out_obj);

	return ret;
}

static long pfru_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct pfru_update_cap_info cap_hdr;
	struct pfru_device *pfru_dev = to_pfru_dev(file);
	void __user *p = (void __user *)arg;
	u32 rev;
	int ret;

	switch (cmd) {
	case PFRU_IOC_QUERY_CAP:
		ret = query_capability(&cap_hdr, pfru_dev);
		if (ret)
			return ret;

		if (copy_to_user(p, &cap_hdr, sizeof(cap_hdr)))
			return -EFAULT;

		return 0;
	case PFRU_IOC_SET_REV:
		if (copy_from_user(&rev, p, sizeof(u32)))
			return -EFAULT;

		if (!pfru_valid_revid(rev))
			return -EINVAL;

		pfru_dev->rev_id = rev;

		return 0;
	case PFRU_IOC_STAGE:
		return start_acpi_update(START_STAGE, pfru_dev);
	case PFRU_IOC_ACTIVATE:
		return start_acpi_update(START_ACTIVATE, pfru_dev);
	case PFRU_IOC_STAGE_ACTIVATE:
		return start_acpi_update(START_STAGE_ACTIVATE, pfru_dev);
	default:
		return -ENOTTY;
	}
}

static ssize_t pfru_write(struct file *file, const char __user *buf,
			  size_t len, loff_t *ppos)
{
	struct pfru_device *pfru_dev = to_pfru_dev(file);
	struct pfru_update_cap_info cap;
	struct pfru_com_buf_info buf_info;
	phys_addr_t phy_addr;
	struct iov_iter iter;
	struct iovec iov;
	char *buf_ptr;
	int ret;

	ret = query_buffer(&buf_info, pfru_dev);
	if (ret)
		return ret;

	if (len > buf_info.buf_size)
		return -EINVAL;

	iov.iov_base = (void __user *)buf;
	iov.iov_len = len;
	iov_iter_init(&iter, WRITE, &iov, 1, len);

	/* map the communication buffer */
	phy_addr = (phys_addr_t)(buf_info.addr_lo | (buf_info.addr_hi << 32));
	buf_ptr = memremap(phy_addr, buf_info.buf_size, MEMREMAP_WB);
	if (IS_ERR(buf_ptr))
		return PTR_ERR(buf_ptr);

	if (!copy_from_iter_full(buf_ptr, len, &iter)) {
		ret = -EINVAL;
		goto unmap;
	}

	/* check if the capsule header has a valid version number */
	ret = query_capability(&cap, pfru_dev);
	if (ret)
		goto unmap;

	if (cap.status != DSM_SUCCEED)
		ret = -EBUSY;
	else if (!valid_version(buf_ptr, &cap, pfru_dev))
		ret = -EINVAL;

unmap:
	memunmap(buf_ptr);

	return ret ?: len;
}

static const struct file_operations acpi_pfru_fops = {
	.owner		= THIS_MODULE,
	.write		= pfru_write,
	.unlocked_ioctl = pfru_ioctl,
	.llseek		= noop_llseek,
};

static int acpi_pfru_remove(struct platform_device *pdev)
{
	struct pfru_device *pfru_dev = platform_get_drvdata(pdev);

	misc_deregister(&pfru_dev->miscdev);
	kfree(pfru_dev->miscdev.nodename);
	kfree(pfru_dev->miscdev.name);
	ida_free(&pfru_ida, pfru_dev->index);

	return 0;
}

static int acpi_pfru_probe(struct platform_device *pdev)
{
	acpi_handle handle = ACPI_HANDLE(&pdev->dev);
	struct pfru_device *pfru_dev;
	int ret;

	if (!acpi_has_method(handle, "_DSM")) {
		dev_dbg(&pdev->dev, "Missing _DSM\n");
		return -ENODEV;
	}

	pfru_dev = devm_kzalloc(&pdev->dev, sizeof(*pfru_dev), GFP_KERNEL);
	if (!pfru_dev)
		return -ENOMEM;

	ret = guid_parse(PFRU_UUID, &pfru_dev->uuid);
	if (ret)
		return ret;

	ret = guid_parse(PFRU_CODE_INJ_UUID, &pfru_dev->code_uuid);
	if (ret)
		return ret;

	ret = guid_parse(PFRU_DRV_UPDATE_UUID, &pfru_dev->drv_uuid);
	if (ret)
		return ret;

	ret = ida_alloc(&pfru_ida, GFP_KERNEL);
	if (ret < 0)
		return ret;

	pfru_dev->index = ret;
	pfru_dev->rev_id = PFRU_DEFAULT_REV_ID;
	pfru_dev->parent_dev = &pdev->dev;

	pfru_dev->miscdev.minor = MISC_DYNAMIC_MINOR;
	pfru_dev->miscdev.name = kasprintf(GFP_KERNEL,
					   "pfru%d", pfru_dev->index);
	if (!pfru_dev->miscdev.name) {
		ret = -ENOMEM;
		goto err_free_ida;
	}

	pfru_dev->miscdev.nodename = kasprintf(GFP_KERNEL,
					       "acpi_pfru%d", pfru_dev->index);
	if (!pfru_dev->miscdev.nodename) {
		ret = -ENOMEM;
		goto err_free_dev_name;
	}

	pfru_dev->miscdev.fops = &acpi_pfru_fops;
	pfru_dev->miscdev.parent = &pdev->dev;

	ret = misc_register(&pfru_dev->miscdev);
	if (ret)
		goto err_free_dev_nodename;

	platform_set_drvdata(pdev, pfru_dev);

	return 0;

err_free_dev_nodename:
	kfree(pfru_dev->miscdev.nodename);
err_free_dev_name:
	kfree(pfru_dev->miscdev.name);
err_free_ida:
	ida_free(&pfru_ida, pfru_dev->index);

	return ret;
}

static const struct acpi_device_id acpi_pfru_ids[] = {
	{"INTC1080", 0},
	{}
};
MODULE_DEVICE_TABLE(acpi, acpi_pfru_ids);

static struct platform_driver acpi_pfru_driver = {
	.driver = {
		.name = "pfru_update",
		.acpi_match_table = acpi_pfru_ids,
	},
	.probe = acpi_pfru_probe,
	.remove = acpi_pfru_remove,
};
module_platform_driver(acpi_pfru_driver);

MODULE_DESCRIPTION("Platform Firmware Runtime Update device driver");
MODULE_LICENSE("GPL v2");
