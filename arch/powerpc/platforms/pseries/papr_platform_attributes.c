// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PAPR platform energy attributes driver
 *
 * This driver creates a sys file at /sys/firmware/papr/ which contains
 * files keyword - value pairs that specify energy configuration of the system.
 *
 * Copyright 2021 IBM Corp.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/hugetlb.h>
#include <asm/lppaca.h>
#include <asm/hvcall.h>
#include <asm/firmware.h>
#include <asm/time.h>
#include <asm/prom.h>
#include <asm/vdso_datapage.h>
#include <asm/vio.h>
#include <asm/mmu.h>
#include <asm/machdep.h>
#include <asm/drmem.h>

#include "pseries.h"

#define MAX_ATTRS	3
#define MAX_NAME_LEN	16

struct papr_attr {
	u64 id;
	struct kobj_attribute attr;
};
struct papr_group {
	char name[MAX_NAME_LEN];
	struct attribute_group pg;
	struct papr_attr *pgattrs;
} *pgs;

struct kobject *papr_kobj;
struct kobject *escale_kobj;
struct hv_energy_scale_buffer *em_buf;
struct energy_scale_attributes *ea;

static ssize_t papr_show_desc(struct kobject *kobj,
			       struct kobj_attribute *attr,
			       char *buf)
{
	struct papr_attr *pattr = container_of(attr, struct papr_attr, attr);
	int idx, ret = 0;

	/*
	 * We do not expect the name to change, hence use the old value
	 * and save a HCALL
	 */
	for (idx = 0; idx < be64_to_cpu(em_buf->num_attr); idx++) {
		if (pattr->id == be64_to_cpu(ea[idx].attr_id)) {
			ret = sprintf(buf, "%s\n", ea[idx].attr_desc);
			if (ret < 0)
				ret = -EIO;
			break;
		}
	}

	return ret;
}

static ssize_t papr_show_value(struct kobject *kobj,
				struct kobj_attribute *attr,
				char *buf)
{
	struct papr_attr *pattr = container_of(attr, struct papr_attr, attr);
	struct hv_energy_scale_buffer *t_buf;
	struct energy_scale_attributes *t_ea;
	int data_offset, ret = 0;

	t_buf = kmalloc(sizeof(*t_buf), GFP_KERNEL);
	if (t_buf == NULL)
		return -ENOMEM;

	ret = plpar_hcall_norets(H_GET_ENERGY_SCALE_INFO, 0,
				 pattr->id, virt_to_phys(t_buf),
				 sizeof(*t_buf));

	if (ret != H_SUCCESS) {
		pr_warn("hcall failed: H_GET_ENERGY_SCALE_INFO");
		goto out;
	}

	data_offset = be64_to_cpu(t_buf->array_offset) -
			(sizeof(t_buf->num_attr) +
			sizeof(t_buf->array_offset) +
			sizeof(t_buf->data_header_version));

	t_ea = (struct energy_scale_attributes *) &t_buf->data[data_offset];

	ret = sprintf(buf, "%llu\n", be64_to_cpu(t_ea->attr_value));
	if (ret < 0)
		ret = -EIO;
out:
	kfree(t_buf);

	return ret;
}

static ssize_t papr_show_value_desc(struct kobject *kobj,
				     struct kobj_attribute *attr,
				     char *buf)
{
	struct papr_attr *pattr = container_of(attr, struct papr_attr, attr);
	struct hv_energy_scale_buffer *t_buf;
	struct energy_scale_attributes *t_ea;
	int data_offset, ret = 0;

	t_buf = kmalloc(sizeof(*t_buf), GFP_KERNEL);
	if (t_buf == NULL)
		return -ENOMEM;

	ret = plpar_hcall_norets(H_GET_ENERGY_SCALE_INFO, 0,
				 pattr->id, virt_to_phys(t_buf),
				 sizeof(*t_buf));

	if (ret != H_SUCCESS) {
		pr_warn("hcall failed: H_GET_ENERGY_SCALE_INFO");
		goto out;
	}

	data_offset = be64_to_cpu(t_buf->array_offset) -
			(sizeof(t_buf->num_attr) +
			sizeof(t_buf->array_offset) +
			sizeof(t_buf->data_header_version));

	t_ea = (struct energy_scale_attributes *) &t_buf->data[data_offset];

	ret = sprintf(buf, "%s\n", t_ea->attr_value_desc);
	if (ret < 0)
		ret = -EIO;
out:
	kfree(t_buf);

	return ret;
}

static struct papr_ops_info {
	const char *attr_name;
	ssize_t (*show)(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf);
} ops_info[] = {
	{ "desc", papr_show_desc },
	{ "value", papr_show_value },
	{ "value_desc", papr_show_value_desc },
};

static void add_attr(u64 id, int index, struct papr_attr *attr)
{
	attr->id = id;
	sysfs_attr_init(&attr->attr.attr);
	attr->attr.attr.name = ops_info[index].attr_name;
	attr->attr.attr.mode = 0444;
	attr->attr.show = ops_info[index].show;
}

static int add_attr_group(u64 id, int len, struct papr_group *pg,
			  bool show_val_desc)
{
	int i;

	for (i = 0; i < len; i++) {
		if (!strcmp(ops_info[i].attr_name, "value_desc") &&
		    !show_val_desc) {
			continue;
		}
		add_attr(id, i, &pg->pgattrs[i]);
		pg->pg.attrs[i] = &pg->pgattrs[i].attr.attr;
	}

	return sysfs_create_group(escale_kobj, &pg->pg);
}


static int __init papr_init(void)
{
	uint64_t num_attr;
	int ret, idx, i, data_offset;

	em_buf = kmalloc(sizeof(*em_buf), GFP_KERNEL);
	if (em_buf == NULL)
		return -ENOMEM;
	/*
	 * hcall(
	 * uint64 H_GET_ENERGY_SCALE_INFO,  // Get energy scale info
	 * uint64 flags,            // Per the flag request
	 * uint64 firstAttributeId, // The attribute id
	 * uint64 bufferAddress,    // Guest physical address of the output buffer
	 * uint64 bufferSize);      // The size in bytes of the output buffer
	 */
	ret = plpar_hcall_norets(H_GET_ENERGY_SCALE_INFO, 0, 0,
				 virt_to_phys(em_buf), sizeof(*em_buf));

	if (!firmware_has_feature(FW_FEATURE_LPAR) || ret != H_SUCCESS ||
	    em_buf->data_header_version != 0x1) {
		pr_warn("hcall failed: H_GET_ENERGY_SCALE_INFO");
		goto out;
	}

	num_attr = be64_to_cpu(em_buf->num_attr);

	/*
	 * Typecast the energy buffer to the attribute structure at the offset
	 * specified in the buffer
	 */
	data_offset = be64_to_cpu(em_buf->array_offset) -
			(sizeof(em_buf->num_attr) +
			sizeof(em_buf->array_offset) +
			sizeof(em_buf->data_header_version));

	ea = (struct energy_scale_attributes *) &em_buf->data[data_offset];

	pgs = kcalloc(num_attr, sizeof(*pgs), GFP_KERNEL);
	if (!pgs)
		goto out_pgs;

	papr_kobj = kobject_create_and_add("papr", firmware_kobj);
	if (!papr_kobj) {
		pr_warn("kobject_create_and_add papr failed\n");
		goto out_kobj;
	}

	escale_kobj = kobject_create_and_add("energy_scale_info", papr_kobj);
	if (!escale_kobj) {
		pr_warn("kobject_create_and_add energy_scale_info failed\n");
		goto out_ekobj;
	}

	for (idx = 0; idx < num_attr; idx++) {
		char buf[4];
		bool show_val_desc = true;

		pgs[idx].pgattrs = kcalloc(MAX_ATTRS,
					   sizeof(*pgs[idx].pgattrs),
					   GFP_KERNEL);
		if (!pgs[idx].pgattrs)
			goto out_kobj;

		pgs[idx].pg.attrs = kcalloc(MAX_ATTRS + 1,
					    sizeof(*pgs[idx].pg.attrs),
					    GFP_KERNEL);
		if (!pgs[idx].pg.attrs) {
			kfree(pgs[idx].pgattrs);
			goto out_kobj;
		}

		sprintf(buf, "%lld", be64_to_cpu(ea[idx].attr_id));
		pgs[idx].pg.name = buf;

		/* Do not add the value description if it does not exist */
		if (strlen(ea[idx].attr_value_desc) == 0)
			show_val_desc = false;

		if (add_attr_group(be64_to_cpu(ea[idx].attr_id),
				   MAX_ATTRS, &pgs[idx], show_val_desc)) {
			pr_warn("Failed to create papr attribute group %s\n",
				pgs[idx].pg.name);
			goto out_pgattrs;
		}
	}

	return 0;

out_pgattrs:
	for (i = 0; i < MAX_ATTRS; i++) {
		kfree(pgs[i].pgattrs);
		kfree(pgs[i].pg.attrs);
	}
out_ekobj:
	kobject_put(escale_kobj);
out_kobj:
	kobject_put(papr_kobj);
out_pgs:
	kfree(pgs);
out:
	kfree(em_buf);

	return -ENOMEM;
}

machine_device_initcall(pseries, papr_init);
