// SPDX-License-Identifier: GPL-2.0
/*
 * USB Power Delivery sysfs entries
 *
 * Copyright (C) 2022, Intel Corporation
 * Author: Heikki Krogerus <heikki.krogerus@linux.intel.com>
 */

#include <linux/device.h>
#include <linux/slab.h>
#include <linux/usb/pd.h>

#include "pd.h"

#define to_pdo(o) container_of(o, struct pdo, kobj)

struct pdo {
	struct kobject kobj;
	int object_position;
	u32 pdo;
	struct list_head node;
};

static void pdo_release(struct kobject *kobj)
{
	kfree(to_pdo(kobj));
}

/* -------------------------------------------------------------------------- */
/* Fixed Supply */

static ssize_t
dual_role_power_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%u\n", !!(to_pdo(kobj)->pdo & PDO_FIXED_DUAL_ROLE));
}

static ssize_t
usb_suspend_supported_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%u\n", !!(to_pdo(kobj)->pdo & PDO_FIXED_SUSPEND));
}

static ssize_t
unconstrained_power_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%u\n", !!(to_pdo(kobj)->pdo & PDO_FIXED_EXTPOWER));
}

static ssize_t
usb_communication_capable_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%u\n", !!(to_pdo(kobj)->pdo & PDO_FIXED_USB_COMM));
}

static ssize_t
dual_role_data_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%u\n", !!(to_pdo(kobj)->pdo & PDO_FIXED_DATA_SWAP));
}

static ssize_t
unchunked_extended_messages_supported_show(struct kobject *kobj,
					   struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%u\n", !!(to_pdo(kobj)->pdo & PDO_FIXED_UNCHUNK_EXT));
}

/*
 * REVISIT: Peak Current requires access also to the RDO.
static ssize_t
peak_current_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	...
}
*/

static ssize_t
fast_role_swap_current_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%u\n", to_pdo(kobj)->pdo >> PDO_FIXED_FRS_CURR_SHIFT) & 3;
}

static ssize_t
voltage_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%umV\n", pdo_fixed_voltage(to_pdo(kobj)->pdo));
}

/* Shared with Variable supplies, both source and sink */
static ssize_t current_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%umA\n", pdo_max_current(to_pdo(kobj)->pdo));
}

/* These additional details are only available with vSafe5V supplies */
static struct kobj_attribute dual_role_power_attr = __ATTR_RO(dual_role_power);
static struct kobj_attribute usb_suspend_supported_attr = __ATTR_RO(usb_suspend_supported);
static struct kobj_attribute unconstrained_power_attr = __ATTR_RO(unconstrained_power);
static struct kobj_attribute usb_communication_capable_attr = __ATTR_RO(usb_communication_capable);
static struct kobj_attribute dual_role_data_attr = __ATTR_RO(dual_role_data);
static struct kobj_attribute
unchunked_extended_messages_supported_attr = __ATTR_RO(unchunked_extended_messages_supported);

/* Visible on all Fixed type source supplies */
/*static struct kobj_attribute peak_current_attr = __ATTR_RO(peak_current);*/
/* Visible on Fixed type sink supplies */
static struct kobj_attribute fast_role_swap_current_attr = __ATTR_RO(fast_role_swap_current);

/* Shared with Variable type supplies */
static struct kobj_attribute maximum_current_attr = {
	.attr = {
		.name = "maximum_current",
		.mode = 0444,
	},
	.show = current_show,
};

static struct kobj_attribute operational_current_attr = {
	.attr = {
		.name = "operational_current",
		.mode = 0444,
	},
	.show = current_show,
};

/* Visible on all Fixed type supplies */
static struct kobj_attribute voltage_attr = __ATTR_RO(voltage);

static struct attribute *source_fixed_supply_attrs[] = {
	&dual_role_power_attr.attr,
	&usb_suspend_supported_attr.attr,
	&unconstrained_power_attr.attr,
	&usb_communication_capable_attr.attr,
	&dual_role_data_attr.attr,
	&unchunked_extended_messages_supported_attr.attr,
	/*&peak_current_attr.attr,*/
	&voltage_attr.attr,
	&maximum_current_attr.attr,
	NULL
};

static umode_t fixed_attr_is_visible(struct kobject *kobj, struct attribute *attr, int n)
{
	if (to_pdo(kobj)->object_position &&
	    /*attr != &peak_current_attr.attr &&*/
	    attr != &voltage_attr.attr &&
	    attr != &maximum_current_attr.attr &&
	    attr != &operational_current_attr.attr)
		return 0;

	return attr->mode;
}

static const struct attribute_group source_fixed_supply_group = {
	.is_visible = fixed_attr_is_visible,
	.attrs = source_fixed_supply_attrs,
};
__ATTRIBUTE_GROUPS(source_fixed_supply);

static struct kobj_type source_fixed_supply_type = {
	.release = pdo_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = source_fixed_supply_groups,
};

static struct attribute *sink_fixed_supply_attrs[] = {
	&dual_role_power_attr.attr,
	&usb_suspend_supported_attr.attr,
	&unconstrained_power_attr.attr,
	&usb_communication_capable_attr.attr,
	&dual_role_data_attr.attr,
	&unchunked_extended_messages_supported_attr.attr,
	&fast_role_swap_current_attr.attr,
	&voltage_attr.attr,
	&operational_current_attr.attr,
	NULL
};

static const struct attribute_group sink_fixed_supply_group = {
	.is_visible = fixed_attr_is_visible,
	.attrs = sink_fixed_supply_attrs,
};
__ATTRIBUTE_GROUPS(sink_fixed_supply);

static struct kobj_type sink_fixed_supply_type = {
	.release = pdo_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = sink_fixed_supply_groups,
};

/* -------------------------------------------------------------------------- */
/* Variable Supply */

static ssize_t
maximum_voltage_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%umV\n", pdo_max_voltage(to_pdo(kobj)->pdo));
}

static ssize_t
minimum_voltage_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%umV\n", pdo_min_voltage(to_pdo(kobj)->pdo));
}

/* Shared with Battery */
static struct kobj_attribute maximum_voltage_attr = __ATTR_RO(maximum_voltage);
static struct kobj_attribute minimum_voltage_attr = __ATTR_RO(minimum_voltage);

static struct attribute *source_variable_supply_attrs[] = {
	&maximum_voltage_attr.attr,
	&minimum_voltage_attr.attr,
	&maximum_current_attr.attr,
	NULL
};
ATTRIBUTE_GROUPS(source_variable_supply);

static struct kobj_type source_variable_supply_type = {
	.release = pdo_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = source_variable_supply_groups,
};

static struct attribute *sink_variable_supply_attrs[] = {
	&maximum_voltage_attr.attr,
	&minimum_voltage_attr.attr,
	&operational_current_attr.attr,
	NULL
};
ATTRIBUTE_GROUPS(sink_variable_supply);

static struct kobj_type sink_variable_supply_type = {
	.release = pdo_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = sink_variable_supply_groups,
};

/* -------------------------------------------------------------------------- */
/* Battery */

static ssize_t
maximum_power_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%umW\n", pdo_max_power(to_pdo(kobj)->pdo));
}

static ssize_t
operational_power_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%umW\n", pdo_max_power(to_pdo(kobj)->pdo));
}

static struct kobj_attribute maximum_power_attr = __ATTR_RO(maximum_power);
static struct kobj_attribute operational_power_attr = __ATTR_RO(operational_power);

static struct attribute *source_battery_attrs[] = {
	&maximum_voltage_attr.attr,
	&minimum_voltage_attr.attr,
	&maximum_power_attr.attr,
	NULL
};
ATTRIBUTE_GROUPS(source_battery);

static struct kobj_type source_battery_type = {
	.release = pdo_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = source_battery_groups,
};

static struct attribute *sink_battery_attrs[] = {
	&maximum_voltage_attr.attr,
	&minimum_voltage_attr.attr,
	&operational_power_attr.attr,
	NULL
};
ATTRIBUTE_GROUPS(sink_battery);

static struct kobj_type sink_battery_type = {
	.release = pdo_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = sink_battery_groups,
};

/* -------------------------------------------------------------------------- */
/* Standard Power Range (SPR) Programmable Power Supply (PPS) */

static ssize_t
pps_power_limited_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%u\n", !!(to_pdo(kobj)->pdo & BIT(27)));
}

static ssize_t
pps_max_voltage_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%umV\n", pdo_pps_apdo_max_voltage(to_pdo(kobj)->pdo));
}

static ssize_t
pps_min_voltage_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%umV\n", pdo_pps_apdo_min_voltage(to_pdo(kobj)->pdo));
}

static ssize_t
pps_max_current_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%umA\n", pdo_pps_apdo_max_current(to_pdo(kobj)->pdo));
}

static struct kobj_attribute pps_power_limited_attr = __ATTR_RO(pps_power_limited);

static struct kobj_attribute pps_max_voltage_attr = {
	.attr = {
		.name = "maximum_voltage",
		.mode = 0444,
	},
	.show = pps_max_voltage_show,
};

static struct kobj_attribute pps_min_voltage_attr = {
	.attr = {
		.name = "minimum_voltage",
		.mode = 0444,
	},
	.show = pps_min_voltage_show,
};

static struct kobj_attribute pps_max_current_attr = {
	.attr = {
		.name = "maximum_current",
		.mode = 0444,
	},
	.show = pps_max_current_show,
};

static struct attribute *source_pps_attrs[] = {
	&pps_power_limited_attr.attr,
	&pps_max_voltage_attr.attr,
	&pps_min_voltage_attr.attr,
	&pps_max_current_attr.attr,
	NULL
};
ATTRIBUTE_GROUPS(source_pps);

static struct kobj_type source_pps_type = {
	.release = pdo_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = source_pps_groups,
};

static struct attribute *sink_pps_attrs[] = {
	&pps_max_voltage_attr.attr,
	&pps_min_voltage_attr.attr,
	&pps_max_current_attr.attr,
	NULL
};
ATTRIBUTE_GROUPS(sink_pps);

static struct kobj_type sink_pps_type = {
	.release = pdo_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = sink_pps_groups,
};

/* -------------------------------------------------------------------------- */

static const char * const supply_name[] = {
	[PDO_TYPE_FIXED] = "fixed_supply",
	[PDO_TYPE_BATT]  = "battery",
	[PDO_TYPE_VAR]	 = "variable_supply",
};

static const char * const apdo_supply_name[] = {
	[APDO_TYPE_PPS]  = "programmable_supply",
};

static struct kobj_type *source_type[] = {
	[PDO_TYPE_FIXED] = &source_fixed_supply_type,
	[PDO_TYPE_BATT]  = &source_battery_type,
	[PDO_TYPE_VAR]   = &source_variable_supply_type,
};

static struct kobj_type *source_apdo_type[] = {
	[APDO_TYPE_PPS]  = &source_pps_type,
};

static struct kobj_type *sink_type[] = {
	[PDO_TYPE_FIXED] = &sink_fixed_supply_type,
	[PDO_TYPE_BATT]  = &sink_battery_type,
	[PDO_TYPE_VAR]   = &sink_variable_supply_type,
};

static struct kobj_type *sink_apdo_type[] = {
	[APDO_TYPE_PPS]  = &sink_pps_type,
};

/* REVISIT: Export when EPR_*_Capabilities need to be supported. */
static int add_pdo(struct pd_capabilities *cap, u32 pdo, int position)
{
	struct kobj_type *type;
	const char *name;
	struct pdo *p;
	int ret;

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	p->pdo = pdo;
	p->object_position = position;

	if (pdo_type(pdo) == PDO_TYPE_APDO) {
		/* FIXME: Only PPS supported for now! Skipping others. */
		if (pdo_apdo_type(pdo) > APDO_TYPE_PPS) {
			dev_warn(kobj_to_dev(cap->kobj.parent->parent),
				 "%s: Unknown APDO type. PDO 0x%08x\n",
				 kobject_name(&cap->kobj), pdo);
			kfree(p);
			return 0;
		}

		if (is_source(cap->role))
			type = source_apdo_type[pdo_apdo_type(pdo)];
		else
			type = sink_apdo_type[pdo_apdo_type(pdo)];

		name = apdo_supply_name[pdo_apdo_type(pdo)];
	} else {
		if (is_source(cap->role))
			type = source_type[pdo_type(pdo)];
		else
			type = sink_type[pdo_type(pdo)];

		name = supply_name[pdo_type(pdo)];
	}

	ret = kobject_init_and_add(&p->kobj, type, &cap->kobj, "%u:%s", position + 1, name);
	if (ret) {
		kobject_put(&p->kobj);
		return ret;
	}

	list_add_tail(&p->node, &cap->pdos);

	return 0;
}

/* -------------------------------------------------------------------------- */

static const char * const cap_name[] = {
	[TYPEC_SINK]    = "sink_capabilities",
	[TYPEC_SOURCE]  = "source_capabilities",
};

static void pd_capabilities_release(struct kobject *kobj)
{
	struct pd_capabilities *cap = to_pd_capabilities(kobj);

	if (is_source(cap->role))
		ida_simple_remove(&cap->pd->source_cap_ids, cap->id);
	else
		ida_simple_remove(&cap->pd->sink_cap_ids, cap->id);

	kfree(cap);
}

static struct kobj_type pd_capabilities_type = {
	.release = pd_capabilities_release,
	.sysfs_ops = &kobj_sysfs_ops,
};

/**
 * pd_register_capabilities - Register a set of capabilities
 * @pd: The USB PD instance that the capabilities belong to
 * @desc: Description of the capablities message
 *
 * This function registers a Capability Message described in @desc. The
 * capabilities will have their own sub-directory under @pd in sysfs. @pd can
 * have multiple sets of capabilities defined for it.
 *
 * The function returns pointer to struct pd_capabilities, or ERR_PRT(errno).
 */
struct pd_capabilities *pd_register_capabilities(struct pd *pd, struct pd_caps_desc *desc)
{
	struct pd_capabilities *cap;
	struct pdo *pdo, *tmp;
	int ret;
	int i;

	cap = kzalloc(sizeof(*cap), GFP_KERNEL);
	if (!cap)
		return ERR_PTR(-ENOMEM);

	ret = ida_simple_get(is_source(desc->role) ? &pd->source_cap_ids :
			     &pd->sink_cap_ids, 0, 0, GFP_KERNEL);
	if (ret < 0) {
		kfree(cap);
		return ERR_PTR(ret);
	}

	cap->id = ret;
	cap->pd = pd;
	cap->role = desc->role;
	INIT_LIST_HEAD(&cap->pdos);

	if (cap->id)
		ret = kobject_init_and_add(&cap->kobj, &pd_capabilities_type, &pd->kobj,
					   "%s%u", cap_name[cap->role], cap->id);
	else
		ret = kobject_init_and_add(&cap->kobj, &pd_capabilities_type, &pd->kobj,
					   "%s", cap_name[cap->role]);
	if (ret)
		goto err_remove_capability;

	for (i = 0; i < PDO_MAX_OBJECTS && desc->pdo[i]; i++) {
		ret = add_pdo(cap, desc->pdo[i], i);
		if (ret)
			goto err_remove_pdos;
	}

	if (is_source(cap->role))
		list_add_tail(&cap->node, &pd->source_capabilities);
	else
		list_add_tail(&cap->node, &pd->sink_capabilities);

	return cap;

err_remove_pdos:
	list_for_each_entry_safe(pdo, tmp, &cap->pdos, node) {
		list_del(&pdo->node);
		kobject_put(&pdo->kobj);
	}

err_remove_capability:
	kobject_put(&cap->kobj);

	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(pd_register_capabilities);

/**
 * pd_unregister_capabilities - Unregister a set of capabilities
 * @cap: The capabilities
 */
void pd_unregister_capabilities(struct pd_capabilities *cap)
{
	struct pdo *pdo, *tmp;

	if (!cap)
		return;

	list_for_each_entry_safe(pdo, tmp, &cap->pdos, node) {
		list_del(&pdo->node);
		kobject_put(&pdo->kobj);
	}

	list_del(&cap->node);
	kobject_put(&cap->kobj);
}
EXPORT_SYMBOL_GPL(pd_unregister_capabilities);

/* -------------------------------------------------------------------------- */

static ssize_t revision_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct pd *pd = to_pd(kobj);

	return sysfs_emit(buf, "%u.%u\n", (pd->revision >> 8) & 0xff, (pd->revision >> 4) & 0xf);
}

static ssize_t version_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct pd *pd = to_pd(kobj);

	return sysfs_emit(buf, "%u.%u\n", (pd->version >> 8) & 0xff, (pd->version >> 4) & 0xf);
}

static struct kobj_attribute revision_attr = __ATTR_RO(revision);
static struct kobj_attribute version_attr = __ATTR_RO(version);

static struct attribute *pd_attrs[] = {
	&revision_attr.attr,
	&version_attr.attr,
	NULL
};

static umode_t pd_attr_is_visible(struct kobject *kobj, struct attribute *attr, int n)
{
	struct pd *pd = to_pd(kobj);

	if (attr == &version_attr.attr && !pd->version)
		return 0;

	return attr->mode;
}

static const struct attribute_group pd_group = {
	.is_visible = pd_attr_is_visible,
	.attrs = pd_attrs,
};
__ATTRIBUTE_GROUPS(pd);

static void pd_release(struct kobject *kobj)
{
	struct pd *pd = to_pd(kobj);

	ida_destroy(&pd->source_cap_ids);
	ida_destroy(&pd->sink_cap_ids);
	put_device(pd->dev);
	kfree(pd);
}

static struct kobj_type pd_type = {
	.release = pd_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = pd_groups,
};

struct pd *pd_register(struct device *dev, struct pd_desc *desc)
{
	struct pd *pd;
	int ret;

	pd = kzalloc(sizeof(*pd), GFP_KERNEL);
	if (!pd)
		return ERR_PTR(-ENOMEM);

	ida_init(&pd->sink_cap_ids);
	ida_init(&pd->source_cap_ids);
	INIT_LIST_HEAD(&pd->sink_capabilities);
	INIT_LIST_HEAD(&pd->source_capabilities);

	pd->dev = get_device(dev);
	pd->revision = desc->revision;
	pd->version = desc->version;

	ret = kobject_init_and_add(&pd->kobj, &pd_type, &dev->kobj, "usb_power_delivery");
	if (ret) {
		kobject_put(&pd->kobj);
		return ERR_PTR(ret);
	}

	return pd;
}

void pd_unregister(struct pd *pd)
{
	kobject_put(&pd->kobj);
}
