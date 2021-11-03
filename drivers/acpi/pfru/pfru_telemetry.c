// SPDX-License-Identifier: GPL-2.0
/*
 * ACPI Platform Firmware Runtime Update Telemetry Driver
 *
 * Copyright (C) 2021 Intel Corporation
 * Author: Chen Yu <yu.c.chen@intel.com>
 */
#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/uio.h>
#include <linux/uuid.h>

#include <uapi/linux/pfru.h>

#define PFRU_LOG_UUID	"75191659-8178-4D9D-B88F-AC5E5E93E8BF"

#define PFRU_LOG_EXEC_IDX	0
#define PFRU_LOG_HISTORY_IDX	1

#define PFRU_LOG_ERR		0
#define PFRU_LOG_WARN	1
#define PFRU_LOG_INFO	2
#define PFRU_LOG_VERB	4

#define PFRU_FUNC_SET_LEV		1
#define PFRU_FUNC_GET_LEV		2
#define PFRU_FUNC_GET_DATA		3

#define PFRU_REVID_1		1
#define PFRU_REVID_2		2
#define PFRU_DEFAULT_REV_ID	PFRU_REVID_1

enum log_index {
	LOG_STATUS_IDX = 0,
	LOG_EXT_STATUS_IDX = 1,
	LOG_MAX_SZ_IDX = 2,
	LOG_CHUNK1_LO_IDX = 3,
	LOG_CHUNK1_HI_IDX = 4,
	LOG_CHUNK1_SZ_IDX = 5,
	LOG_CHUNK2_LO_IDX = 6,
	LOG_CHUNK2_HI_IDX = 7,
	LOG_CHUNK2_SZ_IDX = 8,
	LOG_ROLLOVER_CNT_IDX = 9,
	LOG_RESET_CNT_IDX = 10,
	LOG_NR_IDX = 11
};

struct pfru_log_device {
	guid_t uuid;
	int index;
	struct pfru_log_info info;
	struct device *parent_dev;
	struct miscdevice miscdev;
};

static DEFINE_IDA(pfru_log_ida);

static inline struct pfru_log_device *to_pfru_log_dev(struct file *file)
{
	return container_of(file->private_data, struct pfru_log_device, miscdev);
}

static int get_pfru_log_data_info(struct pfru_log_data_info *data_info,
				  struct pfru_log_device *pfru_log_dev)
{
	acpi_handle handle = ACPI_HANDLE(pfru_log_dev->parent_dev);
	union acpi_object *out_obj, in_obj, in_buf;
	int ret = -EINVAL;

	memset(&in_obj, 0, sizeof(in_obj));
	memset(&in_buf, 0, sizeof(in_buf));
	in_obj.type = ACPI_TYPE_PACKAGE;
	in_obj.package.count = 1;
	in_obj.package.elements = &in_buf;
	in_buf.type = ACPI_TYPE_INTEGER;
	in_buf.integer.value = pfru_log_dev->info.log_type;

	out_obj = acpi_evaluate_dsm_typed(handle, &pfru_log_dev->uuid,
					  pfru_log_dev->info.log_revid, PFRU_FUNC_GET_DATA,
					  &in_obj, ACPI_TYPE_PACKAGE);
	if (!out_obj)
		return ret;

	if (out_obj->package.count < LOG_NR_IDX)
		goto free_acpi_buffer;

	if (out_obj->package.elements[LOG_STATUS_IDX].type != ACPI_TYPE_INTEGER)
		goto free_acpi_buffer;

	data_info->status = out_obj->package.elements[LOG_STATUS_IDX].integer.value;

	if (out_obj->package.elements[LOG_EXT_STATUS_IDX].type != ACPI_TYPE_INTEGER)
		goto free_acpi_buffer;

	data_info->ext_status =
		out_obj->package.elements[LOG_EXT_STATUS_IDX].integer.value;

	if (out_obj->package.elements[LOG_MAX_SZ_IDX].type != ACPI_TYPE_INTEGER)
		goto free_acpi_buffer;

	data_info->max_data_size =
		out_obj->package.elements[LOG_MAX_SZ_IDX].integer.value;

	if (out_obj->package.elements[LOG_CHUNK1_LO_IDX].type != ACPI_TYPE_INTEGER)
		goto free_acpi_buffer;

	data_info->chunk1_addr_lo =
		out_obj->package.elements[LOG_CHUNK1_LO_IDX].integer.value;

	if (out_obj->package.elements[LOG_CHUNK1_HI_IDX].type != ACPI_TYPE_INTEGER)
		goto free_acpi_buffer;

	data_info->chunk1_addr_hi =
		out_obj->package.elements[LOG_CHUNK1_HI_IDX].integer.value;

	if (out_obj->package.elements[LOG_CHUNK1_SZ_IDX].type != ACPI_TYPE_INTEGER)
		goto free_acpi_buffer;

	data_info->chunk1_size =
		out_obj->package.elements[LOG_CHUNK1_SZ_IDX].integer.value;

	if (out_obj->package.elements[LOG_CHUNK2_LO_IDX].type != ACPI_TYPE_INTEGER)
		goto free_acpi_buffer;

	data_info->chunk2_addr_lo =
		out_obj->package.elements[LOG_CHUNK2_LO_IDX].integer.value;

	if (out_obj->package.elements[LOG_CHUNK2_HI_IDX].type != ACPI_TYPE_INTEGER)
		goto free_acpi_buffer;

	data_info->chunk2_addr_hi =
		out_obj->package.elements[LOG_CHUNK2_HI_IDX].integer.value;

	if (out_obj->package.elements[LOG_CHUNK2_SZ_IDX].type != ACPI_TYPE_INTEGER)
		goto free_acpi_buffer;

	data_info->chunk2_size =
		out_obj->package.elements[LOG_CHUNK2_SZ_IDX].integer.value;

	if (out_obj->package.elements[LOG_ROLLOVER_CNT_IDX].type != ACPI_TYPE_INTEGER)
		goto free_acpi_buffer;

	data_info->rollover_cnt =
		out_obj->package.elements[LOG_ROLLOVER_CNT_IDX].integer.value;

	if (out_obj->package.elements[LOG_RESET_CNT_IDX].type != ACPI_TYPE_INTEGER)
		goto free_acpi_buffer;

	data_info->reset_cnt =
		out_obj->package.elements[LOG_RESET_CNT_IDX].integer.value;

	ret = 0;

free_acpi_buffer:
	ACPI_FREE(out_obj);

	return ret;
}

static int set_pfru_log_level(int level, struct pfru_log_device *pfru_log_dev)
{
	acpi_handle handle = ACPI_HANDLE(pfru_log_dev->parent_dev);
	union acpi_object *out_obj, *obj, in_obj, in_buf;
	enum pfru_dsm_status status;
	int ret = -EINVAL;

	memset(&in_obj, 0, sizeof(in_obj));
	memset(&in_buf, 0, sizeof(in_buf));
	in_obj.type = ACPI_TYPE_PACKAGE;
	in_obj.package.count = 1;
	in_obj.package.elements = &in_buf;
	in_buf.type = ACPI_TYPE_INTEGER;
	in_buf.integer.value = level;

	out_obj = acpi_evaluate_dsm_typed(handle, &pfru_log_dev->uuid,
					  pfru_log_dev->info.log_revid, PFRU_FUNC_SET_LEV,
					  &in_obj, ACPI_TYPE_PACKAGE);
	if (!out_obj)
		return -EINVAL;

	obj = &out_obj->package.elements[0];
	status = obj->integer.value;
	if (status)
		goto free_acpi_buffer;

	obj = &out_obj->package.elements[1];
	status = obj->integer.value;
	if (status)
		goto free_acpi_buffer;

	ret = 0;

free_acpi_buffer:
	ACPI_FREE(out_obj);

	return ret;
}

static int get_pfru_log_level(struct pfru_log_device *pfru_log_dev)
{
	acpi_handle handle = ACPI_HANDLE(pfru_log_dev->parent_dev);
	union acpi_object *out_obj, *obj;
	enum pfru_dsm_status status;
	int ret = -EINVAL;

	out_obj = acpi_evaluate_dsm_typed(handle, &pfru_log_dev->uuid,
					  pfru_log_dev->info.log_revid, PFRU_FUNC_GET_LEV,
					  NULL, ACPI_TYPE_PACKAGE);
	if (!out_obj)
		return -EINVAL;

	obj = &out_obj->package.elements[0];
	if (obj->type != ACPI_TYPE_INTEGER)
		goto free_acpi_buffer;

	status = obj->integer.value;
	if (status)
		goto free_acpi_buffer;

	obj = &out_obj->package.elements[1];
	if (obj->type != ACPI_TYPE_INTEGER)
		goto free_acpi_buffer;

	status = obj->integer.value;
	if (status)
		goto free_acpi_buffer;

	obj = &out_obj->package.elements[2];
	if (obj->type != ACPI_TYPE_INTEGER)
		goto free_acpi_buffer;

	ret = obj->integer.value;

free_acpi_buffer:
	ACPI_FREE(out_obj);

	return ret;
}

static int valid_log_level(int level)
{
	return level == PFRU_LOG_ERR || level == PFRU_LOG_WARN ||
	       level == PFRU_LOG_INFO || level == PFRU_LOG_VERB;
}

static int valid_log_type(int type)
{
	return type == PFRU_LOG_EXEC_IDX || type == PFRU_LOG_HISTORY_IDX;
}

static inline int valid_log_revid(u32 id)
{
	return id == PFRU_REVID_1 || id == PFRU_REVID_2;
}

static long pfru_log_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct pfru_log_device *pfru_log_dev = to_pfru_log_dev(file);
	struct pfru_log_data_info data_info;
	struct pfru_log_info info;
	void __user *p;
	int ret = 0;

	p = (void __user *)arg;

	switch (cmd) {
	case PFRU_LOG_IOC_SET_INFO:
		if (copy_from_user(&info, p, sizeof(info)))
			return -EFAULT;

		if (valid_log_revid(info.log_revid))
			pfru_log_dev->info.log_revid = info.log_revid;

		if (valid_log_level(info.log_level)) {
			ret = set_pfru_log_level(info.log_level, pfru_log_dev);
			if (ret < 0)
				return ret;

			pfru_log_dev->info.log_level = info.log_level;
		}

		if (valid_log_type(info.log_type))
			pfru_log_dev->info.log_type = info.log_type;

		return 0;
	case PFRU_LOG_IOC_GET_INFO:
		info.log_level = get_pfru_log_level(pfru_log_dev);
		if (ret < 0)
			return ret;

		info.log_type = pfru_log_dev->info.log_type;
		info.log_revid = pfru_log_dev->info.log_revid;
		if (copy_to_user(p, &info, sizeof(info)))
			return -EFAULT;

		return 0;
	case PFRU_LOG_IOC_GET_DATA_INFO:
		ret = get_pfru_log_data_info(&data_info, pfru_log_dev);
		if (ret)
			return ret;

		if (copy_to_user(p, &data_info, sizeof(struct pfru_log_data_info)))
			return -EFAULT;

		return 0;
	default:
		return -ENOTTY;
	}
}

static int
pfru_log_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct pfru_log_device *pfru_log_dev;
	struct pfru_log_data_info info;
	unsigned long psize, vsize;
	phys_addr_t base_addr;
	int ret;

	if (vma->vm_flags & VM_WRITE)
		return -EROFS;

	/* changing from read to write with mprotect is not allowed */
	vma->vm_flags &= ~VM_MAYWRITE;

	pfru_log_dev = to_pfru_log_dev(file);

	ret = get_pfru_log_data_info(&info, pfru_log_dev);
	if (ret)
		return ret;

	base_addr = (phys_addr_t)(info.chunk2_addr_lo | (info.chunk2_addr_hi << 32));
	/* pfru update has not been launched yet */
	if (!base_addr)
		return -ENODEV;

	psize = info.max_data_size;
	/* base address and total buffer size must be page aligned */
	if (!PAGE_ALIGNED(base_addr) || !PAGE_ALIGNED(psize))
		return -ENODEV;

	vsize = vma->vm_end - vma->vm_start;
	if (vsize > psize)
		return -EINVAL;

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	if (io_remap_pfn_range(vma, vma->vm_start, PFN_DOWN(base_addr),
			       vsize, vma->vm_page_prot))
		return -EAGAIN;

	return 0;
}

static const struct file_operations acpi_pfru_log_fops = {
	.owner		= THIS_MODULE,
	.mmap		= pfru_log_mmap,
	.unlocked_ioctl = pfru_log_ioctl,
	.llseek		= noop_llseek,
};

static int acpi_pfru_log_remove(struct platform_device *pdev)
{
	struct pfru_log_device *pfru_log_dev = platform_get_drvdata(pdev);

	misc_deregister(&pfru_log_dev->miscdev);
	kfree(pfru_log_dev->miscdev.nodename);
	kfree(pfru_log_dev->miscdev.name);
	ida_free(&pfru_log_ida, pfru_log_dev->index);

	return 0;
}

static int acpi_pfru_log_probe(struct platform_device *pdev)
{
	acpi_handle handle = ACPI_HANDLE(&pdev->dev);
	struct pfru_log_device *pfru_log_dev;
	int ret;

	if (!acpi_has_method(handle, "_DSM")) {
		dev_dbg(&pdev->dev, "Missing _DSM\n");
		return -ENODEV;
	}

	pfru_log_dev = devm_kzalloc(&pdev->dev, sizeof(*pfru_log_dev), GFP_KERNEL);
	if (!pfru_log_dev)
		return -ENOMEM;

	ret = guid_parse(PFRU_LOG_UUID, &pfru_log_dev->uuid);
	if (ret)
		return ret;

	ret = ida_alloc(&pfru_log_ida, GFP_KERNEL);
	if (ret < 0)
		return ret;

	pfru_log_dev->index = ret;
	pfru_log_dev->info.log_revid = PFRU_DEFAULT_REV_ID;
	pfru_log_dev->parent_dev = &pdev->dev;

	pfru_log_dev->miscdev.minor = MISC_DYNAMIC_MINOR;
	pfru_log_dev->miscdev.name = kasprintf(GFP_KERNEL,
					       "pfru_telemetry%d",
					       pfru_log_dev->index);
	if (!pfru_log_dev->miscdev.name) {
		ret = -ENOMEM;
		goto err_free_ida;
	}

	pfru_log_dev->miscdev.nodename = kasprintf(GFP_KERNEL,
						   "acpi_pfru_telemetry%d",
						   pfru_log_dev->index);
	if (!pfru_log_dev->miscdev.nodename) {
		ret = -ENOMEM;
		goto err_free_dev_name;
	}

	pfru_log_dev->miscdev.fops = &acpi_pfru_log_fops;
	pfru_log_dev->miscdev.parent = &pdev->dev;

	ret = misc_register(&pfru_log_dev->miscdev);
	if (ret)
		goto err_free_dev_nodename;

	platform_set_drvdata(pdev, pfru_log_dev);

	return 0;

err_free_dev_nodename:
	kfree(pfru_log_dev->miscdev.nodename);
err_free_dev_name:
	kfree(pfru_log_dev->miscdev.name);
err_free_ida:
	ida_free(&pfru_log_ida, pfru_log_dev->index);

	return ret;
}

static const struct acpi_device_id acpi_pfru_log_ids[] = {
	{"INTC1081", 0},
	{}
};
MODULE_DEVICE_TABLE(acpi, acpi_pfru_log_ids);

static struct platform_driver acpi_pfru_log_driver = {
	.driver = {
		.name = "pfru_telemetry",
		.acpi_match_table = acpi_pfru_log_ids,
	},
	.probe = acpi_pfru_log_probe,
	.remove = acpi_pfru_log_remove,
};
module_platform_driver(acpi_pfru_log_driver);

MODULE_DESCRIPTION("Platform Firmware Runtime Update Telemetry driver");
MODULE_LICENSE("GPL v2");
