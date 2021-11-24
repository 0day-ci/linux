// SPDX-License-Identifier: GPL-2.0+
/*
*
* Zhouyi AI Accelerator driver
*
* Copyright (C) 2020 Arm (China) Ltd.
* Copyright (C) 2021 Cai Huoqing
*/

/**
 * @file zynpu_sysfs.h
 * sysfs interface implementation file
 */

#include <linux/device.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include "zynpu.h"

static struct zynpu_priv *zynpu = NULL;

static int print_reg_info(char *buf, const char *name, int offset)
{
	struct zynpu_io_req io_req;

	if (!zynpu)
		return 0;

	io_req.rw = ZYNPU_IO_READ;
	io_req.offset = offset;
	zynpu_priv_io_rw(zynpu, &io_req);
	return snprintf(buf, 1024, "0x%-*x%-*s0x%08x\n", 6, offset, 22, name, io_req.value);
}

static ssize_t sysfs_zynpu_ext_register_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret = 0;
	char tmp[1024];

	if (!zynpu)
		return 0;

	ret += snprintf(tmp, 1024, "   ZYNPU External Register Values\n");
	strcat(buf, tmp);
	ret += snprintf(tmp, 1024, "----------------------------------------\n");
	strcat(buf, tmp);
	ret += snprintf(tmp, 1024, "%-*s%-*s%-*s\n", 8, "Offset", 22, "Name", 10, "Value");
	strcat(buf, tmp);
	ret += snprintf(tmp, 1024, "----------------------------------------\n");
	strcat(buf, tmp);

	ret += print_reg_info(tmp, "Ctrl Reg", 0x0);
	strcat(buf, tmp);
	ret += print_reg_info(tmp, "Status Reg", 0x4);
	strcat(buf, tmp);
	ret += print_reg_info(tmp, "Start PC Reg", 0x8);
	strcat(buf, tmp);
	ret += print_reg_info(tmp, "Intr PC Reg", 0xC);
	strcat(buf, tmp);
	ret += print_reg_info(tmp, "IPI Ctrl Reg", 0x10);
	strcat(buf, tmp);
	ret += print_reg_info(tmp, "Data Addr 0 Reg", 0x14);
	strcat(buf, tmp);
	ret += print_reg_info(tmp, "Data Addr 1 Reg", 0x18);
	strcat(buf, tmp);
	if (zynpu_priv_get_version(zynpu) == ZYNPU_VERSION_ZHOUYI_V1) {
		ret += print_reg_info(tmp, "Intr Cause Reg", 0x1C);
		strcat(buf, tmp);
		ret += print_reg_info(tmp, "Intr Status Reg", 0x20);
		strcat(buf, tmp);
	} else if (zynpu_priv_get_version(zynpu) == ZYNPU_VERSION_ZHOUYI_V2) {
		ret += print_reg_info(tmp, "Data Addr 2 Reg", 0x1C);
		strcat(buf, tmp);
		ret += print_reg_info(tmp, "Data Addr 3 Reg", 0x20);
		strcat(buf, tmp);
		ret += print_reg_info(tmp, "ASE0 Ctrl Reg", 0xc0);
		strcat(buf, tmp);
		ret += print_reg_info(tmp, "ASE0 High Base Reg", 0xc4);
		strcat(buf, tmp);
		ret += print_reg_info(tmp, "ASE0 Low Base Reg", 0xc8);
		strcat(buf, tmp);
		ret += print_reg_info(tmp, "ASE1 Ctrl Reg", 0xcc);
		strcat(buf, tmp);
		ret += print_reg_info(tmp, "ASE1 High Base Reg", 0xd0);
		strcat(buf, tmp);
		ret += print_reg_info(tmp, "ASE1 Low Base Reg", 0xd4);
		strcat(buf, tmp);
		ret += print_reg_info(tmp, "ASE2 Ctrl Reg", 0xd8);
		strcat(buf, tmp);
		ret += print_reg_info(tmp, "ASE2 High Base Reg", 0xdc);
		strcat(buf, tmp);
		ret += print_reg_info(tmp, "ASE2 Low Base Reg", 0xe0);
		strcat(buf, tmp);
		ret += print_reg_info(tmp, "ASE3 Ctrl Reg", 0xe4);
		strcat(buf, tmp);
		ret += print_reg_info(tmp, "ASE3 High Base Reg", 0xe8);
		strcat(buf, tmp);
		ret += print_reg_info(tmp, "ASE3 Low Base Reg", 0xec);
		strcat(buf, tmp);
	}
	ret += snprintf(tmp, 1024, "----------------------------------------\n");
	strcat(buf, tmp);
	return ret;
}

static ssize_t sysfs_zynpu_ext_register_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int i = 0;
	int ret = 0;
	char *token = NULL;
	char *buf_dup = NULL;
	int value[3] = { 0 };
	int max_offset = 0;
	struct zynpu_io_req io_req;
	zynpu_priv_io_rw(zynpu, &io_req);

	if (!zynpu)
		return 0;

	if (zynpu->is_suspend)
		return 0;

	buf_dup = (char *)kzalloc(1024, GFP_KERNEL);
	snprintf(buf_dup, 1024, buf);

	dev_dbg(dev, "[SYSFS] user input str: %s", buf_dup);

	for (i = 0; i < 3; i++) {
		token = strsep(&buf_dup, "-");
		if (token == NULL) {
			dev_err(dev, "[SYSFS] please echo as this format: <reg_offset>-<write time>-<write value>");
			goto finish;
		}

		dev_dbg(dev, "[SYSFS] to convert str: %s", token);

		ret = kstrtouint(token, 0, &value[i]);
		if (ret) {
			dev_err(dev, "[SYSFS] convert str to int failed (%d): %s", ret, token);
			goto finish;
		}
	}

	if (zynpu_priv_get_version(zynpu) == ZYNPU_VERSION_ZHOUYI_V1)
		max_offset = 0x20;
	else if (zynpu_priv_get_version(zynpu) == ZYNPU_VERSION_ZHOUYI_V2)
		max_offset = 0xec;

	if (value[0] > max_offset) {
		dev_err(dev, "[SYSFS] register offset too large which cannot be write: 0x%x", value[0]);
		goto finish;
	}

	dev_info(dev, "[SYSFS] offset 0x%x, time 0x%x, value 0x%x",
	       value[0], value[1], value[2]);

	io_req.rw = ZYNPU_IO_WRITE;
	io_req.offset = value[0];
	io_req.value = value[2];
	for (i = 0; i < value[1]; i++) {
		dev_info(dev, "[SYSFS] writing register 0x%x with value 0x%x", value[0], value[2]);
		zynpu_priv_io_rw(zynpu, &io_req);
	}

finish:
	return count;
}

static ssize_t sysfs_zynpu_job_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	if (!zynpu)
		return 0;

	return zynpu_job_manager_sysfs_job_show(&zynpu->job_manager, buf);
}

static DEVICE_ATTR(ext_register, 0644, sysfs_zynpu_ext_register_show, sysfs_zynpu_ext_register_store);
static DEVICE_ATTR(job, 0444, sysfs_zynpu_job_show, NULL);

int zynpu_create_sysfs(void *zynpu_priv)
{
	int ret = 0;

	if (!zynpu_priv)
		return -EINVAL;
	zynpu = (struct zynpu_priv *)zynpu_priv;

	device_create_file(zynpu->dev, &dev_attr_ext_register);
	device_create_file(zynpu->dev, &dev_attr_job);

	return ret;
}

void zynpu_destroy_sysfs(void *zynpu_priv)
{
	if (!zynpu_priv)
		return;

	device_remove_file(zynpu->dev, &dev_attr_ext_register);
	device_remove_file(zynpu->dev, &dev_attr_job);

	zynpu = NULL;
}
