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
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/uio.h>
#include <linux/uuid.h>
#include <uapi/linux/pfru.h>

enum cap_index {
	CAP_STATUS_IDX,
	CAP_UPDATE_IDX,
	CAP_CODE_TYPE_IDX,
	CAP_FW_VER_IDX,
	CAP_CODE_RT_VER_IDX,
	CAP_DRV_TYPE_IDX,
	CAP_DRV_RT_VER_IDX,
	CAP_DRV_SVN_IDX,
	CAP_PLAT_ID_IDX,
	CAP_OEM_ID_IDX,
	CAP_OEM_INFO_IDX,
	CAP_NR_IDX,
};

enum buf_index {
	BUF_STATUS_IDX,
	BUF_EXT_STATUS_IDX,
	BUF_ADDR_LOW_IDX,
	BUF_ADDR_HI_IDX,
	BUF_SIZE_IDX,
	BUF_NR_IDX,
};

enum update_index {
	UPDATE_STATUS_IDX,
	UPDATE_EXT_STATUS_IDX,
	UPDATE_AUTH_TIME_LOW_IDX,
	UPDATE_AUTH_TIME_HI_IDX,
	UPDATE_EXEC_TIME_LOW_IDX,
	UPDATE_EXEC_TIME_HI_IDX,
	UPDATE_NR_IDX,
};

struct pfru_device {
	guid_t uuid, code_uuid, drv_uuid;
	int rev_id;
	struct device *dev;
};

static struct pfru_device *pfru_dev;

static int query_capability(struct pfru_update_cap_info *cap)
{
	union acpi_object *out_obj;
	acpi_handle handle;
	int ret = -EINVAL;

	handle = ACPI_HANDLE(pfru_dev->dev);
	out_obj = acpi_evaluate_dsm_typed(handle, &pfru_dev->uuid,
					  pfru_dev->rev_id,
					  FUNC_QUERY_UPDATE_CAP,
					  NULL, ACPI_TYPE_PACKAGE);
	if (!out_obj)
		return ret;

	if (out_obj->package.count < CAP_NR_IDX)
		goto free_acpi_buffer;

	if (out_obj->package.elements[CAP_STATUS_IDX].type != ACPI_TYPE_INTEGER)
		goto free_acpi_buffer;

	cap->status = out_obj->package.elements[CAP_STATUS_IDX].integer.value;

	if (out_obj->package.elements[CAP_UPDATE_IDX].type != ACPI_TYPE_INTEGER)
		goto free_acpi_buffer;

	cap->update_cap = out_obj->package.elements[CAP_UPDATE_IDX].integer.value;

	if (out_obj->package.elements[CAP_CODE_TYPE_IDX].type != ACPI_TYPE_BUFFER)
		goto free_acpi_buffer;

	memcpy(&cap->code_type,
	       out_obj->package.elements[CAP_CODE_TYPE_IDX].buffer.pointer,
	       out_obj->package.elements[CAP_CODE_TYPE_IDX].buffer.length);

	if (out_obj->package.elements[CAP_FW_VER_IDX].type != ACPI_TYPE_INTEGER)
		goto free_acpi_buffer;

	cap->fw_version =
		out_obj->package.elements[CAP_FW_VER_IDX].integer.value;

	if (out_obj->package.elements[CAP_CODE_RT_VER_IDX].type != ACPI_TYPE_INTEGER)
		goto free_acpi_buffer;

	cap->code_rt_version =
		out_obj->package.elements[CAP_CODE_RT_VER_IDX].integer.value;

	if (out_obj->package.elements[CAP_DRV_TYPE_IDX].type != ACPI_TYPE_BUFFER)
		goto free_acpi_buffer;

	memcpy(&cap->drv_type,
	       out_obj->package.elements[CAP_DRV_TYPE_IDX].buffer.pointer,
	       out_obj->package.elements[CAP_DRV_TYPE_IDX].buffer.length);

	if (out_obj->package.elements[CAP_DRV_RT_VER_IDX].type != ACPI_TYPE_INTEGER)
		goto free_acpi_buffer;

	cap->drv_rt_version =
		out_obj->package.elements[CAP_DRV_RT_VER_IDX].integer.value;

	if (out_obj->package.elements[CAP_DRV_SVN_IDX].type != ACPI_TYPE_INTEGER)
		goto free_acpi_buffer;

	cap->drv_svn =
		out_obj->package.elements[CAP_DRV_SVN_IDX].integer.value;

	if (out_obj->package.elements[CAP_PLAT_ID_IDX].type != ACPI_TYPE_BUFFER)
		goto free_acpi_buffer;

	memcpy(&cap->platform_id,
	       out_obj->package.elements[CAP_PLAT_ID_IDX].buffer.pointer,
	       out_obj->package.elements[CAP_PLAT_ID_IDX].buffer.length);

	if (out_obj->package.elements[CAP_OEM_ID_IDX].type != ACPI_TYPE_BUFFER)
		goto free_acpi_buffer;

	memcpy(&cap->oem_id,
	       out_obj->package.elements[CAP_OEM_ID_IDX].buffer.pointer,
	       out_obj->package.elements[CAP_OEM_ID_IDX].buffer.length);
	ret = 0;
free_acpi_buffer:
	ACPI_FREE(out_obj);

	return ret;
}

static int query_buffer(struct pfru_com_buf_info *info)
{
	union acpi_object *out_obj;
	acpi_handle handle;
	int ret = -EINVAL;

	handle = ACPI_HANDLE(pfru_dev->dev);
	out_obj = acpi_evaluate_dsm_typed(handle, &pfru_dev->uuid,
					  pfru_dev->rev_id, FUNC_QUERY_BUF,
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

static int get_image_type(efi_manage_capsule_image_header_t *img_hdr)
{
	guid_t *image_type_id = &img_hdr->image_type_id;

	/* check whether this is a code injection or driver update */
	if (guid_equal(image_type_id, &pfru_dev->code_uuid))
		return CODE_INJECT_TYPE;
	else if (guid_equal(image_type_id, &pfru_dev->drv_uuid))
		return DRIVER_UPDATE_TYPE;
	else
		return -EINVAL;
}

static int adjust_efi_size(efi_manage_capsule_image_header_t *img_hdr,
			   int size)
{
	/*
	 * The (u64 hw_ins) was introduced in UEFI spec version 2,
	 * and (u64 capsule_support) was introduced in version 3.
	 * The size needs to be adjusted accordingly. That is to
	 * say, version 1 should subtract the size of hw_ins+capsule_support,
	 * and version 2 should sbstract the size of capsule_support.
	 */
	size += sizeof(efi_manage_capsule_image_header_t);
	switch (img_hdr->ver) {
	case 1:
		size -= 2 * sizeof(u64);
		break;
	case 2:
		size -= sizeof(u64);
		break;
	default:
		/* only support version 1 and 2 */
		return -EINVAL;
	}

	return size;
}

static bool valid_version(const void *data, struct pfru_update_cap_info *cap)
{
	struct pfru_payload_hdr *payload_hdr;
	efi_capsule_header_t *cap_hdr;
	efi_manage_capsule_header_t *m_hdr;
	efi_manage_capsule_image_header_t *m_img_hdr;
	efi_image_auth_t *auth;
	int type, size;

	/*
	 * Sanity check if the capsule image has a newer version
	 * than current one.
	 */
	cap_hdr = (efi_capsule_header_t *)data;
	size = cap_hdr->headersize;
	m_hdr = (efi_manage_capsule_header_t *)(data + size);
	/*
	 * Current data structure size plus variable array indicated
	 * by number of (emb_drv_cnt + payload_cnt)
	 */
	size += sizeof(efi_manage_capsule_header_t) +
		      (m_hdr->emb_drv_cnt + m_hdr->payload_cnt) * sizeof(u64);
	m_img_hdr = (efi_manage_capsule_image_header_t *)(data + size);

	type = get_image_type(m_img_hdr);
	if (type < 0)
		return false;

	size = adjust_efi_size(m_img_hdr, size);
	if (size < 0)
		return false;

	auth = (efi_image_auth_t *)(data + size);
	size += sizeof(u64) + auth->auth_info.hdr.len;
	payload_hdr = (struct pfru_payload_hdr *)(data + size);

	/* Finally, compare the version. */
	if (type == CODE_INJECT_TYPE)
		return payload_hdr->rt_ver >= cap->code_rt_version;
	else
		return payload_hdr->rt_ver >= cap->drv_rt_version;
}

static void dump_update_result(struct pfru_updated_result *result)
{
	dev_dbg(pfru_dev->dev, "Update result:\n");
	dev_dbg(pfru_dev->dev, "Status:%d\n", result->status);
	dev_dbg(pfru_dev->dev, "Extended Status:%d\n", result->ext_status);
	dev_dbg(pfru_dev->dev, "Authentication Time Low:%lld\n",
		result->low_auth_time);
	dev_dbg(pfru_dev->dev, "Authentication Time High:%lld\n",
		result->high_auth_time);
	dev_dbg(pfru_dev->dev, "Execution Time Low:%lld\n",
		result->low_exec_time);
	dev_dbg(pfru_dev->dev, "Execution Time High:%lld\n",
		result->high_exec_time);
}

static int start_acpi_update(int action)
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

	handle = ACPI_HANDLE(pfru_dev->dev);
	out_obj = acpi_evaluate_dsm_typed(handle, &pfru_dev->uuid,
					  pfru_dev->rev_id, FUNC_START,
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

	dump_update_result(&update_result);
	ret = 0;

free_acpi_buffer:
	ACPI_FREE(out_obj);

	return ret;
}

static long pfru_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct pfru_update_cap_info cap;
	void __user *p;
	int ret = 0, rev;

	if (!pfru_dev)
		return -ENODEV;

	p = (void __user *)arg;

	switch (cmd) {
	case PFRU_IOC_QUERY_CAP:
		ret = query_capability(&cap);
		if (ret)
			return ret;

		if (copy_to_user(p, &cap, sizeof(cap)))
			return -EFAULT;

		break;
	case PFRU_IOC_SET_REV:
		if (copy_from_user(&rev, p, sizeof(unsigned int)))
			return -EFAULT;

		if (!pfru_valid_revid(rev))
			return -EINVAL;

		pfru_dev->rev_id = rev;
		break;
	case PFRU_IOC_STAGE:
		ret = start_acpi_update(START_STAGE);
		break;
	case PFRU_IOC_ACTIVATE:
		ret = start_acpi_update(START_ACTIVATE);
		break;
	case PFRU_IOC_STAGE_ACTIVATE:
		ret = start_acpi_update(START_STAGE_ACTIVATE);
		break;
	default:
		ret = -ENOTTY;
		break;
	}

	return ret;
}

static ssize_t pfru_write(struct file *file, const char __user *buf,
			  size_t len, loff_t *ppos)
{
	struct pfru_update_cap_info cap;
	struct pfru_com_buf_info info;
	phys_addr_t phy_addr;
	struct iov_iter iter;
	struct iovec iov;
	char *buf_ptr;
	int ret;

	if (!pfru_dev)
		return -ENODEV;

	ret = query_buffer(&info);
	if (ret)
		return ret;

	if (len > info.buf_size)
		return -EINVAL;

	iov.iov_base = (void __user *)buf;
	iov.iov_len = len;
	iov_iter_init(&iter, WRITE, &iov, 1, len);

	/* map the communication buffer */
	phy_addr = (phys_addr_t)(info.addr_lo | (info.addr_hi << 32));
	buf_ptr = memremap(phy_addr, info.buf_size, MEMREMAP_WB);
	if (IS_ERR(buf_ptr))
		return PTR_ERR(buf_ptr);

	if (!copy_from_iter_full(buf_ptr, len, &iter)) {
		ret = -EINVAL;
		goto unmap;
	}

	/* Check if the capsule header has a valid version number. */
	ret = query_capability(&cap);
	if (ret)
		goto unmap;

	if (cap.status != DSM_SUCCEED)
		ret = -EBUSY;
	else if (!valid_version(buf_ptr, &cap))
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

static struct miscdevice pfru_misc_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "pfru",
	.nodename = "acpi_pfru",
	.fops = &acpi_pfru_fops,
};

static int acpi_pfru_remove(struct platform_device *pdev)
{
	return 0;
}

static int acpi_pfru_probe(struct platform_device *pdev)
{
	acpi_handle handle;
	int ret;

	/* Only one instance is allowed. */
	if (pfru_dev)
		return 0;

	pfru_dev = kzalloc(sizeof(*pfru_dev), GFP_KERNEL);
	if (!pfru_dev)
		return -ENOMEM;

	ret = guid_parse(PFRU_UUID, &pfru_dev->uuid);
	if (ret)
		goto out;

	ret = guid_parse(PFRU_CODE_INJ_UUID, &pfru_dev->code_uuid);
	if (ret)
		goto out;

	ret = guid_parse(PFRU_DRV_UPDATE_UUID, &pfru_dev->drv_uuid);
	if (ret)
		goto out;

	/* default rev id is 1 */
	pfru_dev->rev_id = 1;
	pfru_dev->dev = &pdev->dev;
	handle = ACPI_HANDLE(pfru_dev->dev);
	if (!acpi_has_method(handle, "_DSM")) {
		dev_dbg(&pdev->dev, "Missing _DSM\n");
		ret = -ENODEV;
		goto out;
	}

	return 0;
out:
	kfree(pfru_dev);
	pfru_dev = NULL;

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

static int __init pfru_init(void)
{
	int ret;

	ret = misc_register(&pfru_misc_dev);
	if (ret)
		return ret;

	return platform_driver_register(&acpi_pfru_driver);
}

static void __exit pfru_exit(void)
{
	platform_driver_unregister(&acpi_pfru_driver);
	misc_deregister(&pfru_misc_dev);
	kfree(pfru_dev);
	pfru_dev = NULL;
}

module_init(pfru_init);
module_exit(pfru_exit);

MODULE_DESCRIPTION("Platform Firmware Runtime Update device driver");
MODULE_LICENSE("GPL v2");
