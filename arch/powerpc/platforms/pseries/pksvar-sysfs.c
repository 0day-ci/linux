// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 IBM Corporation <nayna@linux.ibm.com>
 *
 * This code exposes variables stored in Platform Keystore via sysfs
 */

#define pr_fmt(fmt) "pksvar-sysfs: " fmt

#include <linux/slab.h>
#include <linux/compat.h>
#include <linux/string.h>
#include <linux/of.h>
#include <asm/pks.h>

static struct kobject *pks_kobj;
static struct kobject *prop_kobj;
static struct kobject *os_kobj;

static struct pks_config *config;

struct osvar_sysfs_attr {
	struct bin_attribute bin_attr;
	struct list_head node;
};

static LIST_HEAD(osvar_sysfs_list);


static ssize_t osvar_sysfs_read(struct file *file, struct kobject *kobj,
				struct bin_attribute *bin_attr, char *buf,
				loff_t off, size_t count)
{
	struct pks_var var;
	char *out;
	u32 outlen;
	int rc;

	var.name = (char *)bin_attr->attr.name;
	var.namelen = strlen(var.name) + 1;
	var.prefix = NULL;
	rc = pks_read_var(&var);
	if (rc) {
		pr_err("Error reading object %d\n", rc);
		return rc;
	}

	outlen = sizeof(var.policy) + var.datalen;
	out = kzalloc(outlen, GFP_KERNEL);
	memcpy(out, &var.policy, sizeof(var.policy));
	memcpy(out + sizeof(var.policy), var.data, var.datalen);

	count = outlen;
	memcpy(buf, out, outlen);

	kfree(out);
	return count;
}

static ssize_t osvar_sysfs_write(struct file *file, struct kobject *kobj,
				 struct bin_attribute *bin_attr, char *buf,
				 loff_t off, size_t count)
{
	struct pks_var *var = NULL;
	int rc = 0;
	char *p;
	char *name = (char *)bin_attr->attr.name;
	struct osvar_sysfs_attr *osvar_sysfs = NULL;

	list_for_each_entry(osvar_sysfs, &osvar_sysfs_list, node) {
		if (strncmp(name, osvar_sysfs->bin_attr.attr.name,
			    strlen(name)) == 0) {
			var = osvar_sysfs->bin_attr.private;
			break;
		}
	}

	p = strsep(&name, ".");

	var->datalen = count;
	var->data = kzalloc(count, GFP_KERNEL);
	if (!var->data)
		return -ENOMEM;

	memcpy(var->data, buf, count);
	var->name = p;
	var->namelen = strlen(p) + 1;

	pr_info("var %s of length %d to be written\n", var->name, var->namelen);
	var->prefix = NULL;
	rc = pks_update_signed_var(*var);

	if (rc) {
		pr_err(" write failed with rc is %d\n", rc);
		var->datalen = 0;
		count = rc;
		goto err;
	}

err:
	kfree(var->data);
	return count;
}



static ssize_t version_show(struct kobject *kobj, struct kobj_attribute *attr,
			    char *buf)
{
	return sprintf(buf, "%d\n", config->version);
}

static ssize_t flags_show(struct kobject *kobj, struct kobj_attribute *attr,
			  char *buf)
{
	return sprintf(buf, "%02x\n", config->flags);
}

static ssize_t max_object_label_size_show(struct kobject *kobj,
					  struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", config->maxobjlabelsize);
}

static ssize_t max_object_size_show(struct kobject *kobj,
				    struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", config->maxobjsize);
}

static ssize_t total_size_show(struct kobject *kobj,
			       struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", config->totalsize);
}

static ssize_t used_space_show(struct kobject *kobj,
			       struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", config->usedspace);
}

static ssize_t supported_policies_show(struct kobject *kobj,
				       struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", config->supportedpolicies);
}

static ssize_t create_var_store(struct kobject *kobj,
				struct kobj_attribute *attr,
				const char *buf, size_t count)
{
	int rc;
	struct pks_var *var;
	char *suffix = ".tmp";
	char *name;
	u16 namelen = 0;
	struct osvar_sysfs_attr *osvar_sysfs = NULL;

	namelen = count + strlen(suffix);
	name = kzalloc(namelen, GFP_KERNEL);
	if (!name)
		return -ENOMEM;
	memcpy(name, buf, count-1);
	memcpy(name + (count-1), suffix, strlen(suffix));
	name[namelen] = '\0';

	pr_debug("var %s of length %d to be added\n", name, namelen);

	osvar_sysfs = kzalloc(sizeof(struct osvar_sysfs_attr), GFP_KERNEL);
	if (!osvar_sysfs) {
		rc = -ENOMEM;
		goto err;
	}

	var = kzalloc(sizeof(struct pks_var), GFP_KERNEL);

	if (!var) {
		rc = -ENOMEM;
		goto err;
	}

	var->name = name;
	var->namelen = namelen;
	var->prefix = NULL;
	var->policy = 0;

	sysfs_bin_attr_init(&osvar_sysfs->bin_attr);
	osvar_sysfs->bin_attr.private = var;
	osvar_sysfs->bin_attr.attr.name = name;
	osvar_sysfs->bin_attr.attr.mode = 0600;
	osvar_sysfs->bin_attr.size = 0;
	osvar_sysfs->bin_attr.read = osvar_sysfs_read;
	osvar_sysfs->bin_attr.write = osvar_sysfs_write;

	rc = sysfs_create_bin_file(os_kobj,
			&osvar_sysfs->bin_attr);
	if (rc)
		goto err;

	list_add_tail(&osvar_sysfs->node, &osvar_sysfs_list);
	rc = count;
err:
	return rc;
}

static ssize_t delete_var_store(struct kobject *kobj,
				struct kobj_attribute *attr,
				const char *buf, size_t count)
{
	int rc;
	struct pks_var_name vname;
	struct osvar_sysfs_attr *osvar_sysfs = NULL;

	vname.name = kzalloc(count, GFP_KERNEL);
	if (!vname.name)
		return -ENOMEM;

	memcpy(vname.name, buf, count-1);
	vname.name[count] = '\0';
	vname.namelen = count;

	pr_debug("var %s of length %lu to be deleted\n", buf, count);

	rc = pks_remove_var(NULL, vname);

	if (!rc) {
		list_for_each_entry(osvar_sysfs, &osvar_sysfs_list, node) {
			if (strncmp(vname.name, osvar_sysfs->bin_attr.attr.name,
				    strlen(vname.name)) == 0) {
				list_del(&osvar_sysfs->node);
				sysfs_remove_bin_file(os_kobj, &osvar_sysfs->bin_attr);
				break;
			}
		}
		rc = count;
	}

	return rc;
}

static struct kobj_attribute version_attr = __ATTR_RO(version);
static struct kobj_attribute flags_attr = __ATTR_RO(flags);
static struct kobj_attribute max_object_label_size_attr = __ATTR_RO(max_object_label_size);
static struct kobj_attribute max_object_size_attr = __ATTR_RO(max_object_size);
static struct kobj_attribute total_size_attr = __ATTR_RO(total_size);
static struct kobj_attribute used_space_attr = __ATTR_RO(used_space);
static struct kobj_attribute supported_policies_attr = __ATTR_RO(supported_policies);
static struct kobj_attribute create_var_attr = __ATTR_WO(create_var);
static struct kobj_attribute delete_var_attr = __ATTR_WO(delete_var);

static int __init pks_sysfs_prop_load(void)
{
	int rc;

	config = pks_get_config();
	if (!config)
		return -ENODEV;

	rc = sysfs_create_file(prop_kobj, &version_attr.attr);
	rc = sysfs_create_file(prop_kobj, &flags_attr.attr);
	rc = sysfs_create_file(prop_kobj, &max_object_label_size_attr.attr);
	rc = sysfs_create_file(prop_kobj, &max_object_size_attr.attr);
	rc = sysfs_create_file(prop_kobj, &total_size_attr.attr);
	rc = sysfs_create_file(prop_kobj, &used_space_attr.attr);
	rc = sysfs_create_file(prop_kobj, &supported_policies_attr.attr);

	return 0;
}

static int __init pks_sysfs_os_load(void)
{
	struct osvar_sysfs_attr *osvar_sysfs = NULL;
	struct pks_var_name_list namelist;
	struct pks_var *var;
	int rc;
	int i;

	rc = sysfs_create_file(os_kobj, &create_var_attr.attr);
	rc = sysfs_create_file(os_kobj, &delete_var_attr.attr);
	rc = pks_get_var_ids_for_type(NULL, &namelist);
	if (rc)
		return rc;

	for (i = 0; i < namelist.varcount; i++) {
		var = kzalloc(sizeof(struct pks_var), GFP_KERNEL);
		var->name = namelist.varlist[i].name;
		var->namelen = namelist.varlist[i].namelen;
		var->prefix = NULL;
		rc = pks_read_var(var);
		if (rc) {
			pr_err("Error %d reading object %s\n", rc, var->name);
			continue;
		}

		osvar_sysfs = kzalloc(sizeof(struct osvar_sysfs_attr), GFP_KERNEL);
		if (!osvar_sysfs) {
			rc = -ENOMEM;
			break;
		}

		sysfs_bin_attr_init(&osvar_sysfs->bin_attr);
		osvar_sysfs->bin_attr.private = var;
		osvar_sysfs->bin_attr.attr.name = namelist.varlist[i].name;
		osvar_sysfs->bin_attr.attr.mode = 0600;
		osvar_sysfs->bin_attr.size = var->datalen;
		osvar_sysfs->bin_attr.read = osvar_sysfs_read;
		osvar_sysfs->bin_attr.write = osvar_sysfs_write;

		rc = sysfs_create_bin_file(os_kobj,
				&osvar_sysfs->bin_attr);

		if (rc)
			continue;

		list_add_tail(&osvar_sysfs->node, &osvar_sysfs_list);
	}

	return rc;
}

static int pks_sysfs_init(void)
{
	int rc;

	pks_kobj = kobject_create_and_add("pksvar", firmware_kobj);
	if (!pks_kobj) {
		pr_err("pksvar: Failed to create pks kobj\n");
		return -ENOMEM;
	}

	prop_kobj = kobject_create_and_add("config", pks_kobj);
	if (!prop_kobj) {
		pr_err("secvar: config kobject registration failed.\n");
		kobject_put(pks_kobj);
		return -ENOMEM;
	}

	rc = pks_sysfs_prop_load();
	if (rc)
		return rc;

	os_kobj = kobject_create_and_add("os", pks_kobj);
	if (!os_kobj) {
		pr_err("pksvar: os kobject registration failed.\n");
		kobject_put(os_kobj);
		return -ENOMEM;
	}

	rc = pks_sysfs_os_load();
	if (rc)
		return rc;

	return 0;
}
late_initcall(pks_sysfs_init);
