// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2021 Linaro Limited
 *
 * Author: Daniel Lezcano <daniel.lezcano@linaro.org>
 *
 * The DTPM framework defines a set of devices which are power capable.
 *
 * The configfs allows to create a hierarchy of devices in order
 * to reflect the constraints we want to apply to them.
 *
 * Each dtpm node is created via a mkdir operation in the configfs
 * directory. It will create the corresponding dtpm device in the
 * sysfs and the 'device' will contain the absolute path to the dtpm
 * node in the sysfs, thus allowing to do the connection between the
 * created dtpm node in the configfs hierarchy and the dtpm node in
 * the powercap framework.
 *
 * The dtpm nodes can be real or virtual. The former is a real device
 * where acting on its power is possible and is registered in a dtpm
 * framework's list with an unique name. A creation with mkdir with
 * one of the registered name will instanciate the dtpm device. If the
 * name is not in the registered list, it will create a virtual node
 * where its purpose is to aggregate the power characteristics of its
 * children which can virtual or real.
 *
 * It is not allowed to create a node if another one in the hierarchy
 * has the same name. That ensures the consistency and prevents
 * multiple instanciation of the same dtpm device.
 */
#include <linux/dtpm.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/configfs.h>

static struct config_group *cstrn_group;

static struct config_item_type dtpm_cstrn_type;

static const struct config_item_type dtpm_root_group_type = {
	.ct_owner = THIS_MODULE,
};

static struct configfs_subsystem dtpm_subsys = {
	.su_group = {
		.cg_item = {
			.ci_namebuf = "dtpm",
			.ci_type = &dtpm_root_group_type,
		},
	},
	.su_mutex = __MUTEX_INITIALIZER(dtpm_subsys.su_mutex),
};

static bool dtpm_configfs_exists(struct config_group *grp, const char *name)
{
	struct list_head *entry;

	list_for_each(entry, &grp->cg_children) {
		struct config_item *item =
			container_of(entry, struct config_item, ci_entry);

		if (config_item_name(item) &&
		    !strcmp(config_item_name(item), name))
			return true;

		if (dtpm_configfs_exists(to_config_group(item), name))
			return true;
	}

	return false;
}

static struct config_group *dtpm_cstrn_make_group(struct config_group *grp, const char *name)
{
	struct dtpm *d, *p;
	int ret;

	if (dtpm_configfs_exists(cstrn_group, name))
		return ERR_PTR(-EEXIST);

	d = dtpm_lookup(name);
	if (!d) {
		d = kzalloc(sizeof(*d), GFP_KERNEL);
		if (!d)
			return ERR_PTR(-ENOMEM);
		dtpm_init(d, NULL);
	}

	config_group_init_type_name(&d->cfg, name, &dtpm_cstrn_type);

	/*
	 * Retrieve the dtpm parent node. The first dtpm node in the
	 * hierarchy constraint is the root node, thus it does not
	 * have a parent.
	 */
	p = (grp == cstrn_group) ? NULL :
		container_of(grp, struct dtpm, cfg);

	ret = dtpm_register(name, d, p);
	if (ret)
		goto dtpm_free;

	if (!try_module_get(THIS_MODULE)) {
		ret = -ENODEV;
		goto dtpm_unregister;
	}

	return &d->cfg;

dtpm_unregister:
	dtpm_unregister(d);
dtpm_free:
	if (!d->ops)
		kfree(d);

	return ERR_PTR(ret);
}

static void dtpm_cstrn_drop_group(struct config_group *grp,
				  struct config_item *cfg)
{
	struct config_group *cg = to_config_group(cfg);
	struct dtpm *d = container_of(cg, struct dtpm, cfg);

	dtpm_unregister(d);
	if (!d->ops)
		kfree(d);
	module_put(THIS_MODULE);
	config_item_put(cfg);
}

static struct configfs_group_operations dtpm_cstrn_group_ops = {
	.make_group = dtpm_cstrn_make_group,
	.drop_item = dtpm_cstrn_drop_group,
};

static ssize_t dtpm_cstrn_device_show(struct config_item *cfg, char *str)
{
	struct config_group *cg = to_config_group(cfg);
	struct dtpm *d = container_of(cg, struct dtpm, cfg);
	struct kobject *kobj = &d->zone.dev.kobj;
	char *string = kobject_get_path(kobj, GFP_KERNEL);
	ssize_t len;

	if (!string)
		return -EINVAL;

	len = sprintf(str, "%s\n", string);

	kfree(string);

	return len;
}

CONFIGFS_ATTR_RO(dtpm_cstrn_, device);

static struct configfs_attribute *dtpm_cstrn_attrs[] = {
	&dtpm_cstrn_attr_device,
	NULL
};

static struct config_item_type dtpm_cstrn_type = {
	.ct_owner = THIS_MODULE,
	.ct_group_ops = &dtpm_cstrn_group_ops,
};

static int __init dtpm_configfs_init(void)
{
	int ret;

	config_group_init(&dtpm_subsys.su_group);

	ret = configfs_register_subsystem(&dtpm_subsys);
	if (ret)
		return ret;

	cstrn_group = configfs_register_default_group(&dtpm_subsys.su_group,
						      "constraints",
						      &dtpm_cstrn_type);

	/*
	 * The default group does not contain attributes but the other
	 * group will
	 */
	dtpm_cstrn_type.ct_attrs = dtpm_cstrn_attrs;

	return PTR_ERR_OR_ZERO(cstrn_group);
}
module_init(dtpm_configfs_init);

static void __exit dtpm_configfs_exit(void)
{
	configfs_unregister_default_group(cstrn_group);
	configfs_unregister_subsystem(&dtpm_subsys);
}
module_exit(dtpm_configfs_exit);

MODULE_DESCRIPTION("DTPM configuration driver");
MODULE_AUTHOR("Daniel Lezcano");
MODULE_LICENSE("GPL v2");
