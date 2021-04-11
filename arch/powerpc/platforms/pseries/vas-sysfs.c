// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2016-17 IBM Corp.
 */

#define pr_fmt(fmt) "vas: " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/slab.h>
#include <linux/mm.h>

#include "vas.h"

#ifdef CONFIG_SYSFS
static struct kobject *pseries_vas_kobj;
static struct kobject *vas_capabs_kobj;

struct vas_capabs_entry {
	struct kobject kobj;
	struct vas_ct_capabs *capabs;
};

#define to_capabs_entry(entry) container_of(entry, struct vas_capabs_entry, kobj)

static ssize_t avail_lpar_creds_show(struct vas_ct_capabs *capabs, char *buf)
{
	int avail_creds = atomic_read(&capabs->target_lpar_creds) -
			atomic_read(&capabs->used_lpar_creds);
	return sprintf(buf, "%d\n", avail_creds);
}

#define sysfs_capbs_entry_read(_name)					\
static ssize_t _name##_show(struct vas_ct_capabs *capabs, char *buf) 	\
{									\
	return sprintf(buf, "%d\n", atomic_read(&capabs->_name));	\
}

struct vas_sysfs_entry {
	struct attribute attr;
	ssize_t (*show)(struct vas_ct_capabs *, char *);
	ssize_t (*store)(struct vas_ct_capabs *, const char *, size_t);
};

#define VAS_ATTR_RO(_name)	\
	sysfs_capbs_entry_read(_name);		\
	static struct vas_sysfs_entry _name##_attribute = __ATTR(_name,	\
				0444, _name##_show, NULL);

VAS_ATTR_RO(target_lpar_creds);
VAS_ATTR_RO(used_lpar_creds);

static struct vas_sysfs_entry avail_lpar_creds_attribute =
	__ATTR(avail_lpar_creds, 0644, avail_lpar_creds_show, NULL);

static struct attribute *vas_capab_attrs[] = {
	&target_lpar_creds_attribute.attr,
	&used_lpar_creds_attribute.attr,
	&avail_lpar_creds_attribute.attr,
	NULL,
};

static ssize_t vas_type_show(struct kobject *kobj, struct attribute *attr,
			     char *buf)
{
	struct vas_capabs_entry *centry;
	struct vas_ct_capabs *capabs;
	struct vas_sysfs_entry *entry;

	centry = to_capabs_entry(kobj);
	capabs = centry->capabs;
	entry = container_of(attr, struct vas_sysfs_entry, attr);

	if (!entry->show)
		return -EIO;

	return entry->show(capabs, buf);
}

static ssize_t vas_type_store(struct kobject *kobj, struct attribute *attr,
			      const char *buf, size_t count)
{
	struct vas_capabs_entry *centry;
	struct vas_ct_capabs *capabs;
	struct vas_sysfs_entry *entry;

	centry = to_capabs_entry(kobj);
	capabs = centry->capabs;
	entry = container_of(attr, struct vas_sysfs_entry, attr);
	if (!entry->store)
		return -EIO;

	return entry->store(capabs, buf, count);
}

static void vas_type_release(struct kobject *kobj)
{
	struct vas_capabs_entry *centry = to_capabs_entry(kobj);
	kfree(centry);
}

static const struct sysfs_ops vas_sysfs_ops = {
	.show	=	vas_type_show,
	.store	=	vas_type_store,
};

static struct kobj_type vas_attr_type = {
		.release	=	vas_type_release,
		.sysfs_ops      =       &vas_sysfs_ops,
		.default_attrs  =       vas_capab_attrs,
};

/*
 * Add feature specific capability dir entry.
 * Ex: VDefGzip or VQosGzip
 */
int sysfs_add_vas_capabs(struct vas_ct_capabs *capabs)
{
	struct vas_capabs_entry *centry;
	int ret = 0;

	centry = kzalloc(sizeof(*centry), GFP_KERNEL);
	if (!centry)
		return -ENOMEM;

	kobject_init(&centry->kobj, &vas_attr_type);
	centry->capabs = capabs;

	ret = kobject_add(&centry->kobj, vas_capabs_kobj, "%s",
			  capabs->name);

	if (ret) {
		pr_err("VAS: sysfs kobject add / event failed %d\n", ret);
		kobject_put(&centry->kobj);
	}

	return ret;
}

/*
 * Add VAS and VasCaps (overall capabilities) dir entries.
 */
int __init sysfs_pseries_vas_init(struct vas_all_capabs *vas_caps)
{
	pseries_vas_kobj = kobject_create_and_add("vas", kernel_kobj);
	if (!pseries_vas_kobj) {
		pr_err("Failed to create VAS sysfs entry\n");
		return -ENOMEM;
	}

	vas_capabs_kobj = kobject_create_and_add(vas_caps->name,
						 pseries_vas_kobj);
	if (!vas_capabs_kobj) {
		pr_err("Failed to create VAS capabilities kobject\n");
		kobject_put(pseries_vas_kobj);
		return -ENOMEM;
	}

	return 0;
}

#else
int sysfs_add_vas_capabs(struct vas_ct_capabs *capabs)
{
	return 0;
}

int __init sysfs_pseries_vas_init(struct vas_all_capabs *vas_caps)
{
	return 0;
}
#endif
