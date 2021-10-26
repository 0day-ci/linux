// SPDX-License-Identifier: GPL-2.0
/*
 * Componentized device handling.
 */
#include <linux/component.h>
#include <linux/device.h>
#include <linux/idr.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <linux/pm_runtime.h>

#include "base.h"

/**
 * DOC: overview
 *
 * The component helper allows drivers to collect a pile of sub-devices,
 * including their bound drivers, into an aggregate driver. Various subsystems
 * already provide functions to get hold of such components, e.g.
 * of_clk_get_by_name(). The component helper can be used when such a
 * subsystem-specific way to find a device is not available: The component
 * helper fills the niche of aggregate drivers for specific hardware, where
 * further standardization into a subsystem would not be practical. The common
 * example is when a logical device (e.g. a DRM display driver) is spread around
 * the SoC on various components (scanout engines, blending blocks, transcoders
 * for various outputs and so on).
 *
 * The component helper also doesn't solve runtime dependencies, e.g. for system
 * suspend and resume operations. See also :ref:`device links<device_link>`.
 *
 * Components are registered using component_add() and unregistered with
 * component_del(), usually from the driver's probe and disconnect functions.
 *
 * Aggregate drivers first assemble a component match list of what they need
 * using component_match_add(). This is then registered as an aggregate driver
 * using component_aggregate_register(), and unregistered using
 * component_aggregate_unregister().
 */

struct component;

struct component_match_array {
	void *data;
	int (*compare)(struct device *, void *);
	int (*compare_typed)(struct device *, int, void *);
	void (*release)(struct device *, void *);
	struct component *component;
	bool duplicate;
};

struct component_match {
	size_t alloc;
	size_t num;
	struct component_match_array *compare;
};

struct component {
	struct list_head node;
	struct aggregate_device *adev;
	bool bound;

	const struct component_ops *ops;
	int subcomponent;
	struct device *dev;
	struct device_link *link;
};

static DEFINE_MUTEX(component_mutex);
static LIST_HEAD(component_list);
static DEFINE_IDA(aggregate_ida);

#ifdef CONFIG_DEBUG_FS

static struct dentry *component_debugfs_dir;

static int component_devices_show(struct seq_file *s, void *data)
{
	struct aggregate_device *m = s->private;
	struct component_match *match = m->match;
	size_t i;

	mutex_lock(&component_mutex);
	seq_printf(s, "%-40s %20s\n", "aggregate_device name", "status");
	seq_puts(s, "-------------------------------------------------------------\n");
	seq_printf(s, "%-40s %20s\n\n",
		   dev_name(m->parent), m->dev.driver ? "bound" : "not bound");

	seq_printf(s, "%-40s %20s\n", "device name", "status");
	seq_puts(s, "-------------------------------------------------------------\n");
	for (i = 0; i < match->num; i++) {
		struct component *component = match->compare[i].component;

		seq_printf(s, "%-40s %20s\n",
			   component ? dev_name(component->dev) : "(unknown)",
			   component ? (component->bound ? "bound" : "not bound") : "not registered");
	}
	mutex_unlock(&component_mutex);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(component_devices);

static int __init component_debug_init(void)
{
	component_debugfs_dir = debugfs_create_dir("device_component", NULL);

	return 0;
}

core_initcall(component_debug_init);

static void component_debugfs_add(struct aggregate_device *m)
{
	debugfs_create_file(dev_name(m->parent), 0444, component_debugfs_dir, m,
			    &component_devices_fops);
}

static void component_debugfs_del(struct aggregate_device *m)
{
	debugfs_remove(debugfs_lookup(dev_name(m->parent), component_debugfs_dir));
}

#else

static void component_debugfs_add(struct aggregate_device *m)
{ }

static void component_debugfs_del(struct aggregate_device *m)
{ }

#endif

struct aggregate_bus_find_data {
	const struct component_master_ops *ops;
	struct device *parent;
};

static int aggregate_bus_find_match(struct device *dev, const void *_data)
{
	struct aggregate_device *adev = to_aggregate_device(dev);
	const struct aggregate_bus_find_data *data = _data;

	if (adev->parent == data->parent &&
	    (!data->ops || adev->ops == data->ops))
		return 1;

	return 0;
}

static struct component *find_component(struct aggregate_device *adev,
	struct component_match_array *mc)
{
	struct component *c;

	list_for_each_entry(c, &component_list, node) {
		if (c->adev && c->adev != adev)
			continue;

		if (mc->compare && mc->compare(c->dev, mc->data))
			return c;

		if (mc->compare_typed &&
		    mc->compare_typed(c->dev, c->subcomponent, mc->data))
			return c;
	}

	return NULL;
}

static int find_components(struct aggregate_device *adev)
{
	struct component_match *match = adev->match;
	size_t i;

	/*
	 * Scan the array of match functions and attach
	 * any components which are found to this adev.
	 */
	for (i = 0; i < match->num; i++) {
		struct component_match_array *mc = &match->compare[i];
		struct component *c;
		bool duplicate;

		dev_dbg(adev->parent, "Looking for component %zu\n", i);

		if (match->compare[i].component)
			continue;

		c = find_component(adev, mc);
		if (!c)
			return 0;

		duplicate = !!c->adev;
		dev_dbg(adev->parent, "found component %s, duplicate %u\n",
			dev_name(c->dev), duplicate);

		/* Attach this component to the adev */
		match->compare[i].duplicate = duplicate;
		match->compare[i].component = c;
		if (duplicate)
			continue;

		/* Matches put in component_del() */
		get_device(&adev->dev);
		c->link = device_link_add(&adev->dev, c->dev,
					  DL_FLAG_STATELESS | DL_FLAG_PM_RUNTIME);
		c->adev = adev;
	}

	return 1;
}

/* Detach component from associated aggregate_device */
static void remove_component(struct aggregate_device *adev, struct component *c)
{
	size_t i;

	/* Detach the component from this adev. */
	for (i = 0; i < adev->match->num; i++)
		if (adev->match->compare[i].component == c)
			adev->match->compare[i].component = NULL;
}

static void devm_component_match_release(struct device *parent, void *res)
{
	struct component_match *match = res;
	unsigned int i;

	for (i = 0; i < match->num; i++) {
		struct component_match_array *mc = &match->compare[i];

		if (mc->release)
			mc->release(parent, mc->data);
	}

	kfree(match->compare);
}

static int component_match_realloc(struct component_match *match, size_t num)
{
	struct component_match_array *new;

	if (match->alloc == num)
		return 0;

	new = kmalloc_array(num, sizeof(*new), GFP_KERNEL);
	if (!new)
		return -ENOMEM;

	if (match->compare) {
		memcpy(new, match->compare, sizeof(*new) *
					    min(match->num, num));
		kfree(match->compare);
	}
	match->compare = new;
	match->alloc = num;

	return 0;
}

static void __component_match_add(struct device *parent,
	struct component_match **matchptr,
	void (*release)(struct device *, void *),
	int (*compare)(struct device *, void *),
	int (*compare_typed)(struct device *, int, void *),
	void *compare_data)
{
	struct component_match *match = *matchptr;

	if (IS_ERR(match))
		return;

	if (!match) {
		match = devres_alloc(devm_component_match_release,
				     sizeof(*match), GFP_KERNEL);
		if (!match) {
			*matchptr = ERR_PTR(-ENOMEM);
			return;
		}

		devres_add(parent, match);

		*matchptr = match;
	}

	if (match->num == match->alloc) {
		size_t new_size = match->alloc + 16;
		int ret;

		ret = component_match_realloc(match, new_size);
		if (ret) {
			*matchptr = ERR_PTR(ret);
			return;
		}
	}

	match->compare[match->num].compare = compare;
	match->compare[match->num].compare_typed = compare_typed;
	match->compare[match->num].release = release;
	match->compare[match->num].data = compare_data;
	match->compare[match->num].component = NULL;
	match->num++;
}

/**
 * component_match_add_release - add a component match entry with release callback
 * @parent: parent device of the aggregate driver
 * @matchptr: pointer to the list of component matches
 * @release: release function for @compare_data
 * @compare: compare function to match against all components
 * @compare_data: opaque pointer passed to the @compare function
 *
 * Adds a new component match to the list stored in @matchptr, which the
 * aggregate driver needs to function. The list of component matches pointed to
 * by @matchptr must be initialized to NULL before adding the first match. This
 * only matches against components added with component_add().
 *
 * The allocated match list in @matchptr is automatically released using devm
 * actions, where upon @release will be called to free any references held by
 * @compare_data, e.g. when @compare_data is a &device_node that must be
 * released with of_node_put().
 *
 * See also component_match_add() and component_match_add_typed().
 */
void component_match_add_release(struct device *parent,
	struct component_match **matchptr,
	void (*release)(struct device *, void *),
	int (*compare)(struct device *, void *), void *compare_data)
{
	__component_match_add(parent, matchptr, release, compare, NULL,
			      compare_data);
}
EXPORT_SYMBOL(component_match_add_release);

/**
 * component_match_add_typed - add a component match entry for a typed component
 * @parent: parent device of the aggregate driver
 * @matchptr: pointer to the list of component matches
 * @compare_typed: compare function to match against all typed components
 * @compare_data: opaque pointer passed to the @compare function
 *
 * Adds a new component match to the list stored in @matchptr, which the
 * aggregate driver needs to function. The list of component matches pointed to
 * by @matchptr must be initialized to NULL before adding the first match. This
 * only matches against components added with component_add_typed().
 *
 * The allocated match list in @matchptr is automatically released using devm
 * actions.
 *
 * See also component_match_add_release() and component_match_add_typed().
 */
void component_match_add_typed(struct device *parent,
	struct component_match **matchptr,
	int (*compare_typed)(struct device *, int, void *), void *compare_data)
{
	__component_match_add(parent, matchptr, NULL, NULL, compare_typed,
			      compare_data);
}
EXPORT_SYMBOL(component_match_add_typed);

static void free_aggregate_device(struct aggregate_device *adev)
{
	struct component_match *match = adev->match;
	int i;

	component_debugfs_del(adev);

	if (match) {
		for (i = 0; i < match->num; i++) {
			struct component *c = match->compare[i].component;
			if (c)
				c->adev = NULL;
		}
	}

	ida_free(&aggregate_ida, adev->id);
	kfree(adev);
}

static void aggregate_device_release(struct device *dev)
{
	struct aggregate_device *adev = to_aggregate_device(dev);

	free_aggregate_device(adev);
}

static int aggregate_device_match(struct device *dev, struct device_driver *drv)
{
	const struct aggregate_driver *adrv = to_aggregate_driver(drv);
	struct aggregate_device *adev = to_aggregate_device(dev);
	int ret;

	/* Is this driver associated with this device */
	if (adrv != adev->adrv)
		return 0;

	/* Should we start to assemble? */
	mutex_lock(&component_mutex);
	ret = find_components(adev);
	mutex_unlock(&component_mutex);

	return ret;
}

/* TODO: Remove once all aggregate drivers use component_aggregate_register() */
static int component_probe_bind(struct aggregate_device *adev)
{
	return adev->ops->bind(adev->parent);
}

static void component_remove_unbind(struct aggregate_device *adev)
{
	adev->ops->unbind(adev->parent);
}

static int aggregate_driver_probe(struct device *dev)
{
	const struct aggregate_driver *adrv = to_aggregate_driver(dev->driver);
	struct aggregate_device *adev = to_aggregate_device(dev);
	bool modern = adrv->probe != component_probe_bind;
	int ret;

	/* Only do runtime PM when drivers migrate */
	if (modern) {
		pm_runtime_get_noresume(dev);
		pm_runtime_set_active(dev);
		pm_runtime_enable(dev);
	}

	mutex_lock(&component_mutex);
	if (devres_open_group(adev->parent, NULL, GFP_KERNEL)) {
		ret = adrv->probe(adev);
		if (ret)
			devres_release_group(adev->parent, NULL);
	} else {
		ret = -ENOMEM;
	}
	mutex_unlock(&component_mutex);

	if (ret && modern) {
		pm_runtime_disable(dev);
		pm_runtime_set_suspended(dev);
		pm_runtime_put_noidle(dev);
	}

	return ret;
}

static void aggregate_driver_remove(struct device *dev)
{
	const struct aggregate_driver *adrv = to_aggregate_driver(dev->driver);
	struct aggregate_device *adev = to_aggregate_device(dev);
	bool modern = adrv->remove != component_remove_unbind;

	/* Only do runtime PM when drivers migrate */
	if (modern)
		pm_runtime_get_sync(dev);
	adrv->remove(to_aggregate_device(dev));
	devres_release_group(adev->parent, NULL);
	if (!modern)
		return;

	pm_runtime_put_noidle(dev);

	pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);
	pm_runtime_put_noidle(dev);
}

static void aggregate_driver_shutdown(struct device *dev)
{
	const struct aggregate_driver *adrv = to_aggregate_driver(dev->driver);

	if (adrv && adrv->shutdown)
		adrv->shutdown(to_aggregate_device(dev));
}

static struct bus_type aggregate_bus_type = {
	.name		= "aggregate",
	.match		= aggregate_device_match,
	.probe		= aggregate_driver_probe,
	.remove		= aggregate_driver_remove,
	.shutdown	= aggregate_driver_shutdown,
};

/* Callers take ownership of return value, should call put_device() */
static struct aggregate_device *__aggregate_find(struct device *parent,
	const struct component_master_ops *ops)
{
	struct device *dev;
	struct aggregate_bus_find_data data = {
		.ops = ops,
		.parent = parent,
	};

	dev = bus_find_device(&aggregate_bus_type, NULL, &data,
			      aggregate_bus_find_match);

	return dev ? to_aggregate_device(dev) : NULL;
}

static int aggregate_driver_register(struct aggregate_driver *adrv)
{
	adrv->driver.bus = &aggregate_bus_type;
	return driver_register(&adrv->driver);
}

static void aggregate_driver_unregister(struct aggregate_driver *adrv)
{
	driver_unregister(&adrv->driver);
}

static struct aggregate_device *aggregate_device_add(struct device *parent,
	const struct component_master_ops *ops, struct aggregate_driver *adrv,
	struct component_match *match)
{
	struct aggregate_device *adev;
	int ret, id;

	/* Reallocate the match array for its true size */
	ret = component_match_realloc(match, match->num);
	if (ret)
		return ERR_PTR(ret);

	adev = kzalloc(sizeof(*adev), GFP_KERNEL);
	if (!adev)
		return ERR_PTR(-ENOMEM);

	id = ida_alloc(&aggregate_ida, GFP_KERNEL);
	if (id < 0) {
		kfree(adev);
		return ERR_PTR(id);
	}

	adev->id = id;
	adev->parent = parent;
	adev->dev.bus = &aggregate_bus_type;
	adev->dev.release = aggregate_device_release;
	adev->ops = ops;
	adev->match = match;
	adev->adrv = adrv;
	dev_set_name(&adev->dev, "aggregate%d", id);

	ret = device_register(&adev->dev);
	if (ret) {
		put_device(&adev->dev);
		return ERR_PTR(ret);
	}

	component_debugfs_add(adev);

	return adev;
}

/**
 * component_master_add_with_match - register an aggregate driver
 * @parent: parent device of the aggregate driver
 * @ops: callbacks for the aggregate driver
 * @match: component match list for the aggregate driver
 *
 * Registers a new aggregate driver consisting of the components added to @match
 * by calling one of the component_match_add() functions. Once all components in
 * @match are available, it will be assembled by calling
 * &component_master_ops.bind from @ops. Must be unregistered by calling
 * component_master_del().
 *
 * Deprecated: Use component_aggregate_register() instead.
 */
int component_master_add_with_match(struct device *parent,
	const struct component_master_ops *ops,
	struct component_match *match)
{
	struct aggregate_driver *adrv;
	struct aggregate_device *adev;
	int ret = 0;

	adrv = kzalloc(sizeof(*adrv), GFP_KERNEL);
	if (!adrv)
		return -ENOMEM;

	adev = aggregate_device_add(parent, ops, adrv, match);
	if (IS_ERR(adev)) {
		ret = PTR_ERR(adev);
		goto err;
	}

	adrv->probe = component_probe_bind;
	adrv->remove = component_remove_unbind;
	adrv->driver.owner = THIS_MODULE;
	adrv->driver.name = dev_name(&adev->dev);

	ret = aggregate_driver_register(adrv);
	if (!ret)
		return 0;

	put_device(&adev->dev);
err:
	kfree(adrv);
	return ret;
}
EXPORT_SYMBOL_GPL(component_master_add_with_match);

/**
 * component_aggregate_register - register an aggregate driver
 * @parent: parent device of the aggregate driver
 * @adrv: aggregate driver to register
 *
 * Registers a new aggregate driver consisting of the components added to @adrv.match
 * by calling one of the component_match_add() functions. Once all components in
 * @match are available, the aggregate driver will be assembled by calling
 * &adrv.bind. Must be unregistered by calling component_aggregate_unregister().
 */
int component_aggregate_register(struct device *parent,
	struct aggregate_driver *adrv, struct component_match *match)
{
	struct aggregate_device *adev;
	int ret;

	adev = aggregate_device_add(parent, NULL, adrv, match);
	if (IS_ERR(adev))
		return PTR_ERR(adev);

	ret = aggregate_driver_register(adrv);
	if (ret)
		put_device(&adev->dev);

	return ret;
}
EXPORT_SYMBOL_GPL(component_aggregate_register);

/**
 * component_master_del - unregister an aggregate driver
 * @parent: parent device of the aggregate driver
 * @ops: callbacks for the aggregate driver
 *
 * Unregisters an aggregate driver registered with
 * component_master_add_with_match(). If necessary the aggregate driver is first
 * disassembled by calling &component_master_ops.unbind from @ops.
 *
 * Deprecated: Use component_aggregate_unregister() instead.
 */
void component_master_del(struct device *parent,
	const struct component_master_ops *ops)
{
	struct aggregate_device *adev;
	struct aggregate_driver *adrv;
	struct device_driver *drv;

	mutex_lock(&component_mutex);
	adev = __aggregate_find(parent, ops);
	mutex_unlock(&component_mutex);

	if (adev) {
		drv = adev->dev.driver;
		if (drv) {
			adrv = to_aggregate_driver(drv);
			aggregate_driver_unregister(adrv);
			kfree(adrv);
		}

		device_unregister(&adev->dev);
	}
	put_device(&adev->dev);
}
EXPORT_SYMBOL_GPL(component_master_del);

/**
 * component_aggregate_unregister - unregister an aggregate driver
 * @parent: parent device of the aggregate driver
 * @adrv: registered aggregate driver
 *
 * Unregisters an aggregate driver registered with
 * component_aggregate_register(). If necessary the aggregate driver is first
 * disassembled.
 */
void component_aggregate_unregister(struct device *parent,
	struct aggregate_driver *adrv)
{
	struct aggregate_device *adev;

	mutex_lock(&component_mutex);
	adev = __aggregate_find(parent, NULL);
	mutex_unlock(&component_mutex);

	if (adev)
		device_unregister(&adev->dev);
	put_device(&adev->dev);

	aggregate_driver_unregister(adrv);
}
EXPORT_SYMBOL_GPL(component_aggregate_unregister);

static void component_unbind(struct component *component,
	struct aggregate_device *adev, void *data)
{
	WARN_ON(!component->bound);

	if (component->ops && component->ops->unbind)
		component->ops->unbind(component->dev, adev->parent, data);
	component->bound = false;

	/* Release all resources claimed in the binding of this component */
	devres_release_group(component->dev, component);
}

/**
 * component_unbind_all - unbind all components of an aggregate driver
 * @parent: parent device of the aggregate driver
 * @data: opaque pointer, passed to all components
 *
 * Unbinds all components of the aggregate device by passing @data to their
 * &component_ops.unbind functions. Should be called from
 * &component_master_ops.unbind.
 */
void component_unbind_all(struct device *parent, void *data)
{
	struct aggregate_device *adev;
	struct component *c;
	size_t i;

	WARN_ON(!mutex_is_locked(&component_mutex));

	adev = __aggregate_find(parent, NULL);
	if (!adev)
		return;

	/* Unbind components in reverse order */
	for (i = adev->match->num; i--; )
		if (!adev->match->compare[i].duplicate) {
			c = adev->match->compare[i].component;
			component_unbind(c, adev, data);
		}

	put_device(&adev->dev);
}
EXPORT_SYMBOL_GPL(component_unbind_all);

static int component_bind(struct component *component, struct aggregate_device *adev,
	void *data)
{
	int ret;

	/*
	 * Each component initialises inside its own devres group.
	 * This allows us to roll-back a failed component without
	 * affecting anything else.
	 */
	if (!devres_open_group(adev->parent, NULL, GFP_KERNEL))
		return -ENOMEM;

	/*
	 * Also open a group for the device itself: this allows us
	 * to release the resources claimed against the sub-device
	 * at the appropriate moment.
	 */
	if (!devres_open_group(component->dev, component, GFP_KERNEL)) {
		devres_release_group(adev->parent, NULL);
		return -ENOMEM;
	}

	dev_dbg(adev->parent, "binding %s (ops %ps)\n",
		dev_name(component->dev), component->ops);

	ret = component->ops->bind(component->dev, adev->parent, data);
	if (!ret) {
		component->bound = true;

		/*
		 * Close the component device's group so that resources
		 * allocated in the binding are encapsulated for removal
		 * at unbind.  Remove the group on the DRM device as we
		 * can clean those resources up independently.
		 */
		devres_close_group(component->dev, NULL);
		devres_remove_group(adev->parent, NULL);

		dev_info(adev->parent, "bound %s (ops %ps)\n",
			 dev_name(component->dev), component->ops);
	} else {
		devres_release_group(component->dev, NULL);
		devres_release_group(adev->parent, NULL);

		if (ret != -EPROBE_DEFER)
			dev_err(adev->parent, "failed to bind %s (ops %ps): %d\n",
				dev_name(component->dev), component->ops, ret);
	}

	return ret;
}

/**
 * component_bind_all - bind all components of an aggregate driver
 * @parent: parent device of the aggregate driver
 * @data: opaque pointer, passed to all components
 *
 * Binds all components of the aggregate @dev by passing @data to their
 * &component_ops.bind functions. Should be called from
 * &component_master_ops.bind.
 */
int component_bind_all(struct device *parent, void *data)
{
	struct aggregate_device *adev;
	struct component *c;
	size_t i;
	int ret = 0;

	WARN_ON(!mutex_is_locked(&component_mutex));

	adev = __aggregate_find(parent, NULL);
	if (!adev)
		return -EINVAL;

	/* Bind components in match order */
	for (i = 0; i < adev->match->num; i++)
		if (!adev->match->compare[i].duplicate) {
			c = adev->match->compare[i].component;
			ret = component_bind(c, adev, data);
			if (ret)
				break;
		}

	if (ret != 0) {
		for (; i > 0; i--)
			if (!adev->match->compare[i - 1].duplicate) {
				c = adev->match->compare[i - 1].component;
				component_unbind(c, adev, data);
			}
	}
	put_device(&adev->dev);

	return ret;
}
EXPORT_SYMBOL_GPL(component_bind_all);

static int __component_add(struct device *dev, const struct component_ops *ops,
	int subcomponent)
{
	struct component *component;
	int ret;

	component = kzalloc(sizeof(*component), GFP_KERNEL);
	if (!component)
		return -ENOMEM;

	component->ops = ops;
	component->dev = dev;
	component->subcomponent = subcomponent;

	dev_dbg(dev, "adding component (ops %ps)\n", ops);

	mutex_lock(&component_mutex);
	list_add_tail(&component->node, &component_list);
	mutex_unlock(&component_mutex);

	/*
	 * Try to bind.
	 *
	 * Note: we don't check the return value here because component devices
	 * don't care that the aggregate device can actually probe or not. They
	 * only care about adding themselves to the component_list and then
	 * waiting for their component_ops::bind_component callback to be
	 * called.
	 */
	ret = bus_rescan_devices(&aggregate_bus_type);

	return 0;
}

/**
 * component_add_typed - register a component
 * @dev: component device
 * @ops: component callbacks
 * @subcomponent: nonzero identifier for subcomponents
 *
 * Register a new component for @dev. Functions in @ops will be call when the
 * aggregate driver is ready to bind the overall driver by calling
 * component_bind_all(). See also &struct component_ops.
 *
 * @subcomponent must be nonzero and is used to differentiate between multiple
 * components registerd on the same device @dev. These components are match
 * using component_match_add_typed().
 *
 * The component needs to be unregistered at driver unload/disconnect by
 * calling component_del().
 *
 * See also component_add().
 */
int component_add_typed(struct device *dev, const struct component_ops *ops,
	int subcomponent)
{
	if (WARN_ON(subcomponent == 0))
		return -EINVAL;

	return __component_add(dev, ops, subcomponent);
}
EXPORT_SYMBOL_GPL(component_add_typed);

/**
 * component_add - register a component
 * @dev: component device
 * @ops: component callbacks
 *
 * Register a new component for @dev. Functions in @ops will be called when the
 * aggregate driver is ready to bind the overall driver by calling
 * component_bind_all(). See also &struct component_ops.
 *
 * The component needs to be unregistered at driver unload/disconnect by
 * calling component_del().
 *
 * See also component_add_typed() for a variant that allows multipled different
 * components on the same device.
 */
int component_add(struct device *dev, const struct component_ops *ops)
{
	return __component_add(dev, ops, 0);
}
EXPORT_SYMBOL_GPL(component_add);

/**
 * component_del - unregister a component
 * @dev: component device
 * @ops: component callbacks
 *
 * Unregister a component added with component_add(). If the component is bound
 * into an aggregate driver, this will force the entire aggregate driver, including
 * all its components, to be unbound.
 */
void component_del(struct device *dev, const struct component_ops *ops)
{
	struct aggregate_device *adev = NULL;
	struct component *c, *component = NULL;

	mutex_lock(&component_mutex);
	list_for_each_entry(c, &component_list, node)
		if (c->dev == dev && c->ops == ops) {
			list_del(&c->node);
			component = c;
			break;
		}

	if (component && component->adev) {
		adev = component->adev;
		remove_component(adev, component);
	}

	mutex_unlock(&component_mutex);

	if (adev) {
		/* Force unbind */
		device_driver_detach(&adev->dev);
		device_link_del(component->link);
		put_device(&adev->dev);
	}

	WARN_ON(!component);
	kfree(component);
}
EXPORT_SYMBOL_GPL(component_del);

static int __init aggregate_bus_init(void)
{
	return bus_register(&aggregate_bus_type);
}
postcore_initcall(aggregate_bus_init);
