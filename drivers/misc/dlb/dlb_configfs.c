// SPDX-License-Identifier: GPL-2.0-only
// Copyright(c) 2017-2020 Intel Corporation

#include <linux/configfs.h>
#include "dlb_configfs.h"

struct dlb_device_configfs dlb_dev_configfs[16];

static int dlb_configfs_create_sched_domain(struct dlb *dlb,
					    void *karg)
{
	struct dlb_create_sched_domain_args *arg = karg;
	struct dlb_cmd_response response = {0};
	int ret;

	mutex_lock(&dlb->resource_mutex);

	ret = dlb_hw_create_sched_domain(&dlb->hw, arg, &response);

	mutex_unlock(&dlb->resource_mutex);

	memcpy(karg, &response, sizeof(response));

	return ret;
}

/*
 * Configfs directory structure for dlb driver implementation:
 *
 *                             config
 *                                |
 *                               dlb
 *                                |
 *                        +------+------+------+------
 *                        |      |      |      |
 *                       dlb0   dlb1   dlb2   dlb3  ...
 *                        |
 *                +-----------+--+--------+-------
 *                |           |           |
 *             domain0     domain1     domain2   ...
 *                |
 *        +-------+-----+------------+---------------+------------+----------
 *        |             |            |               |            |
 * num_ldb_queues     port0         port1   ...    queue0       queue1   ...
 * num_ldb_ports        |		             |
 * ...                is_ldb                   num_sequence_numbers
 * create             cq_depth                 num_qid_inflights
 * start              ...                      num_atomic_iflights
 *                    enable                   ...
 *                    ...
 */

/*
 * ------ Configfs for dlb domains---------
 *
 * These are the templates for show and store functions in domain
 * groups/directories, which minimizes replication of boilerplate
 * code to copy arguments. Most attributes, use the simple template.
 * "name" is the attribute name in the group.
 */
#define DLB_CONFIGFS_DOMAIN_SHOW(name)				\
static ssize_t dlb_cfs_domain_##name##_show(			\
	struct config_item *item,				\
	char *page)						\
{								\
	return sprintf(page, "%u\n",				\
		       to_dlb_cfs_domain(item)->name);		\
}								\

#define DLB_CONFIGFS_DOMAIN_STORE(name)				\
static ssize_t dlb_cfs_domain_##name##_store(			\
	struct config_item *item,				\
	const char *page,					\
	size_t count)						\
{								\
	int ret;						\
	struct dlb_cfs_domain *dlb_cfs_domain =			\
				to_dlb_cfs_domain(item);	\
								\
	ret = kstrtoint(page, 10, &dlb_cfs_domain->name);	\
	if (ret)						\
		return ret;					\
								\
	return count;						\
}								\

DLB_CONFIGFS_DOMAIN_SHOW(status)
DLB_CONFIGFS_DOMAIN_SHOW(domain_id)
DLB_CONFIGFS_DOMAIN_SHOW(num_ldb_queues)
DLB_CONFIGFS_DOMAIN_SHOW(num_ldb_ports)
DLB_CONFIGFS_DOMAIN_SHOW(num_dir_ports)
DLB_CONFIGFS_DOMAIN_SHOW(num_atomic_inflights)
DLB_CONFIGFS_DOMAIN_SHOW(num_hist_list_entries)
DLB_CONFIGFS_DOMAIN_SHOW(num_ldb_credits)
DLB_CONFIGFS_DOMAIN_SHOW(num_dir_credits)
DLB_CONFIGFS_DOMAIN_SHOW(create)

DLB_CONFIGFS_DOMAIN_STORE(num_ldb_queues)
DLB_CONFIGFS_DOMAIN_STORE(num_ldb_ports)
DLB_CONFIGFS_DOMAIN_STORE(num_dir_ports)
DLB_CONFIGFS_DOMAIN_STORE(num_atomic_inflights)
DLB_CONFIGFS_DOMAIN_STORE(num_hist_list_entries)
DLB_CONFIGFS_DOMAIN_STORE(num_ldb_credits)
DLB_CONFIGFS_DOMAIN_STORE(num_dir_credits)

static ssize_t dlb_cfs_domain_create_store(struct config_item *item,
					   const char *page, size_t count)
{
	struct dlb_cfs_domain *dlb_cfs_domain = to_dlb_cfs_domain(item);
	struct dlb_device_configfs *dlb_dev_configfs;
	struct dlb *dlb;
	int ret, create_in;

	dlb_dev_configfs = container_of(dlb_cfs_domain->dev_grp,
					struct dlb_device_configfs,
					dev_group);
	dlb = dlb_dev_configfs->dlb;
	if (!dlb)
		return -EINVAL;

	ret = kstrtoint(page, 10, &create_in);
	if (ret)
		return ret;

	/* Writing 1 to the 'create' triggers scheduling domain creation */
	if (create_in == 1 && dlb_cfs_domain->create == 0) {
		struct dlb_create_sched_domain_args args = {0};

		memcpy(&args.response, &dlb_cfs_domain->status,
		       sizeof(struct dlb_create_sched_domain_args));

		dev_dbg(dlb->dev,
			"Create domain: %s\n",
			dlb_cfs_domain->group.cg_item.ci_namebuf);

		ret = dlb_configfs_create_sched_domain(dlb, &args);

		dlb_cfs_domain->status = args.response.status;
		dlb_cfs_domain->domain_id = args.response.id;

		if (ret) {
			dev_err(dlb->dev,
				"create sched domain failed: ret=%d\n", ret);
			return ret;
		}

		dlb_cfs_domain->create = 1;
	}

	return count;
}

CONFIGFS_ATTR_RO(dlb_cfs_domain_, status);
CONFIGFS_ATTR_RO(dlb_cfs_domain_, domain_id);
CONFIGFS_ATTR(dlb_cfs_domain_, num_ldb_queues);
CONFIGFS_ATTR(dlb_cfs_domain_, num_ldb_ports);
CONFIGFS_ATTR(dlb_cfs_domain_, num_dir_ports);
CONFIGFS_ATTR(dlb_cfs_domain_, num_atomic_inflights);
CONFIGFS_ATTR(dlb_cfs_domain_, num_hist_list_entries);
CONFIGFS_ATTR(dlb_cfs_domain_, num_ldb_credits);
CONFIGFS_ATTR(dlb_cfs_domain_, num_dir_credits);
CONFIGFS_ATTR(dlb_cfs_domain_, create);

static struct configfs_attribute *dlb_cfs_domain_attrs[] = {
	&dlb_cfs_domain_attr_status,
	&dlb_cfs_domain_attr_domain_id,
	&dlb_cfs_domain_attr_num_ldb_queues,
	&dlb_cfs_domain_attr_num_ldb_ports,
	&dlb_cfs_domain_attr_num_dir_ports,
	&dlb_cfs_domain_attr_num_atomic_inflights,
	&dlb_cfs_domain_attr_num_hist_list_entries,
	&dlb_cfs_domain_attr_num_ldb_credits,
	&dlb_cfs_domain_attr_num_dir_credits,
	&dlb_cfs_domain_attr_create,

	NULL,
};

static void dlb_cfs_domain_release(struct config_item *item)
{
	kfree(to_dlb_cfs_domain(item));
}

static struct configfs_item_operations dlb_cfs_domain_item_ops = {
	.release	= dlb_cfs_domain_release,
};

static const struct config_item_type dlb_cfs_domain_type = {
	.ct_item_ops	= &dlb_cfs_domain_item_ops,
	.ct_attrs	= dlb_cfs_domain_attrs,
	.ct_owner	= THIS_MODULE,
};

/*
 *--------- dlb device level configfs -----------
 *
 * Scheduling domains are created in the device-level configfs driectory.
 */
static struct config_group *dlb_cfs_device_make_domain(struct config_group *group,
						       const char *name)
{
	struct dlb_cfs_domain *dlb_cfs_domain;

	dlb_cfs_domain = kzalloc(sizeof(*dlb_cfs_domain), GFP_KERNEL);
	if (!dlb_cfs_domain)
		return ERR_PTR(-ENOMEM);

	dlb_cfs_domain->dev_grp = group;

	config_group_init_type_name(&dlb_cfs_domain->group, name,
				    &dlb_cfs_domain_type);

	return &dlb_cfs_domain->group;
}

static struct configfs_group_operations dlb_cfs_device_group_ops = {
	.make_group     = dlb_cfs_device_make_domain,
};

static const struct config_item_type dlb_cfs_device_type = {
	/* No need for _item_ops() at the device-level, and default
	 * attribute.
	 * .ct_item_ops	= &dlb_cfs_device_item_ops,
	 * .ct_attrs	= dlb_cfs_device_attrs,
	 */

	.ct_group_ops	= &dlb_cfs_device_group_ops,
	.ct_owner	= THIS_MODULE,
};

/*------------------- dlb group subsystem for configfs ----------------
 *
 * we only need a simple configfs item type here that does not let
 * user to create new entry. The group for each dlb device will be
 * generated when the device is detected in dlb_probe().
 */

static const struct config_item_type dlb_device_group_type = {
	.ct_owner	= THIS_MODULE,
};

/* dlb group subsys in configfs */
static struct configfs_subsystem dlb_device_group_subsys = {
	.su_group = {
		.cg_item = {
			.ci_namebuf = "dlb",
			.ci_type = &dlb_device_group_type,
		},
	},
};

/* Create a configfs directory dlbN for each dlb device probed
 * in dlb_probe()
 */
int dlb_configfs_create_device(struct dlb *dlb)
{
	struct config_group *parent_group, *dev_grp;
	char device_name[16];
	int ret = 0;

	snprintf(device_name, 6, "dlb%d", dlb->id);
	parent_group = &dlb_device_group_subsys.su_group;

	dev_grp = &dlb_dev_configfs[dlb->id].dev_group;
	config_group_init_type_name(dev_grp,
				    device_name,
				    &dlb_cfs_device_type);
	ret = configfs_register_group(parent_group, dev_grp);

	if (ret)
		return ret;

	dlb_dev_configfs[dlb->id].dlb = dlb;

	return ret;
}

int configfs_dlb_init(void)
{
	struct configfs_subsystem *subsys;
	int ret;

	/* set up and register configfs subsystem for dlb */
	subsys = &dlb_device_group_subsys;
	config_group_init(&subsys->su_group);
	mutex_init(&subsys->su_mutex);
	ret = configfs_register_subsystem(subsys);
	if (ret) {
		pr_err("Error %d while registering subsystem %s\n",
		       ret, subsys->su_group.cg_item.ci_namebuf);
	}

	return ret;
}

void configfs_dlb_exit(void)
{
	configfs_unregister_subsystem(&dlb_device_group_subsys);
}
