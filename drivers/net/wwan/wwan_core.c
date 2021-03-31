// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2021, Linaro Ltd <loic.poulain@linaro.org> */

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/idr.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/wwan.h>

#define WWAN_MAX_MINORS 256 /* Allow the whole available cdev range of minors */

static DEFINE_MUTEX(wwan_register_lock); /* WWAN device create|remove lock */
static DEFINE_IDA(minors); /* minors for WWAN port chardevs */
static DEFINE_IDA(wwan_dev_ids); /* for unique WWAN device IDs */
static struct class *wwan_class;
static int wwan_major;

#define to_wwan_dev(d) container_of(d, struct wwan_device, dev)
#define to_wwan_port(d) container_of(d, struct wwan_port, dev)

/**
 * struct wwan_device - The structure that defines a WWAN device
 *
 * @id: WWAN device unique ID.
 * @dev: Underlying device.
 * @port_id: Current available port ID to pick.
 */
struct wwan_device {
	unsigned int id;
	struct device dev;
	atomic_t port_id;
};

static void wwan_dev_release(struct device *dev)
{
	struct wwan_device *wwandev = to_wwan_dev(dev);

	ida_free(&wwan_dev_ids, wwandev->id);
	kfree(wwandev);
}

static const struct device_type wwan_dev_type = {
	.name    = "wwan_dev",
	.release = wwan_dev_release,
};

static int wwan_dev_parent_match(struct device *dev, const void *parent)
{
	return (dev->type == &wwan_dev_type && dev->parent == parent);
}

static struct wwan_device *wwan_dev_get_by_parent(struct device *parent)
{
	struct device *dev;

	dev = class_find_device(wwan_class, NULL, parent, wwan_dev_parent_match);
	if (!dev)
		return ERR_PTR(-ENODEV);

	return to_wwan_dev(dev);
}

/* This function allocates and registers a new WWAN device OR if a WWAN device
 * already exist for the given parent, it gets a reference and return it.
 * This function is not exported (for now), it is called indirectly via
 * wwan_create_port().
 */
static struct wwan_device *wwan_create_dev(struct device *parent)
{
	struct wwan_device *wwandev;
	int err, id;

	/* The 'find-alloc-register' operation must be protected against
	 * concurrent execution, a WWAN device is possibly shared between
	 * multiple callers or concurrently unregistered from wwan_remove_dev().
	 */
	mutex_lock(&wwan_register_lock);

	/* If wwandev already exist, return it */
	wwandev = wwan_dev_get_by_parent(parent);
	if (!IS_ERR(wwandev))
		goto done_unlock;

	id = ida_alloc(&wwan_dev_ids, GFP_KERNEL);
	if (id < 0)
		goto done_unlock;

	wwandev = kzalloc(sizeof(*wwandev), GFP_KERNEL);
	if (!wwandev) {
		ida_free(&wwan_dev_ids, id);
		goto done_unlock;
	}

	wwandev->dev.parent = parent;
	wwandev->dev.class = wwan_class;
	wwandev->dev.type = &wwan_dev_type;
	wwandev->id = id;
	dev_set_name(&wwandev->dev, "wwan%d", wwandev->id);

	err = device_register(&wwandev->dev);
	if (err) {
		put_device(&wwandev->dev);
		wwandev = NULL;
	}

done_unlock:
	mutex_unlock(&wwan_register_lock);

	return wwandev;
}

static int is_wwan_child(struct device *dev, void *data)
{
	return dev->class == wwan_class;
}

static void wwan_remove_dev(struct wwan_device *wwandev)
{
	int ret;

	/* Prevent concurrent picking from wwan_create_dev */
	mutex_lock(&wwan_register_lock);

	/* WWAN device is created and registered (get+add) along with its first
	 * child port, and subsequent port registrations only grab a reference
	 * (get). The WWAN device must then be unregistered (del+put) along with
	 * its latest port, and reference simply dropped (put) otherwise.
	 */
	ret = device_for_each_child(&wwandev->dev, NULL, is_wwan_child);
	if (!ret)
		device_unregister(&wwandev->dev);
	else
		put_device(&wwandev->dev);

	mutex_unlock(&wwan_register_lock);
}

/* ------- WWAN port management ------- */

static void wwan_port_release(struct device *dev)
{
	struct wwan_port *port = to_wwan_port(dev);

	ida_free(&minors, MINOR(port->dev.devt));
	kfree(to_wwan_port(dev));
}

static const struct device_type wwan_port_dev_type = {
	.name = "wwan_port",
	.release = wwan_port_release,
};

static int wwan_port_minor_match(struct device *dev, const void *minor)
{
	return (dev->type == &wwan_port_dev_type &&
		MINOR(dev->devt) == *(unsigned int *)minor);
}

static struct wwan_port *wwan_port_get_by_minor(unsigned int minor)
{
	struct device *dev;

	dev = class_find_device(wwan_class, NULL, &minor, wwan_port_minor_match);
	if (!dev)
		return ERR_PTR(-ENODEV);

	return to_wwan_port(dev);
}

/* Keep aligned with wwan_port_type enum */
static const char * const wwan_port_type_str[] = {
	"AT",
	"MBIM",
	"QMI",
	"QCDM",
	"FIREHOSE"
};

struct wwan_port *wwan_create_port(struct device *parent,
				   enum wwan_port_type type,
				   const struct file_operations *fops,
				   void *private_data)
{
	struct wwan_device *wwandev;
	struct wwan_port *port;
	int minor, err = -ENOMEM;

	if (type >= WWAN_PORT_MAX || !fops)
		return ERR_PTR(-EINVAL);

	/* A port is always a child of a WWAN device, retrieve (allocate or
	 * pick) the WWAN device based on the provided parent device.
	 */
	wwandev = wwan_create_dev(parent);
	if (IS_ERR(wwandev))
		return ERR_PTR(PTR_ERR(wwandev));

	/* A port is exposed as character device, get a minor */
	minor = ida_alloc_range(&minors, 0, WWAN_MAX_MINORS, GFP_KERNEL);
	if (minor < 0)
		goto error_wwandev_remove;

	port = kzalloc(sizeof(*port), GFP_KERNEL);
	if (!port) {
		ida_free(&minors, minor);
		goto error_wwandev_remove;
	}

	port->type = type;
	port->fops = fops;
	port->dev.parent = &wwandev->dev;
	port->dev.class = wwan_class;
	port->dev.type = &wwan_port_dev_type;
	port->dev.devt = MKDEV(wwan_major, minor);
	dev_set_drvdata(&port->dev, private_data);

	/* create unique name based on wwan device id, port index and type */
	dev_set_name(&port->dev, "wwan%up%u%s", wwandev->id,
		     atomic_inc_return(&wwandev->port_id),
		     wwan_port_type_str[port->type]);

	err = device_register(&port->dev);
	if (err)
		goto error_put_device;

	return port;

error_put_device:
	put_device(&port->dev);
error_wwandev_remove:
	wwan_remove_dev(wwandev);

	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(wwan_create_port);

void wwan_remove_port(struct wwan_port *port)
{
	struct wwan_device *wwandev = to_wwan_dev(port->dev.parent);

	dev_set_drvdata(&port->dev, NULL);
	device_unregister(&port->dev);

	/* Release related wwan device */
	wwan_remove_dev(wwandev);
}
EXPORT_SYMBOL_GPL(wwan_remove_port);

static int wwan_port_open(struct inode *inode, struct file *file)
{
	const struct file_operations *new_fops;
	struct wwan_port *port;
	int err = 0;

	port = wwan_port_get_by_minor(iminor(inode));
	if (IS_ERR(port))
		return PTR_ERR(port);

	/* Place the port private data in the file's private_data so it can
	 * be used by the file operations, including f_op->open below.
	 */
	file->private_data = dev_get_drvdata(&port->dev);
	stream_open(inode, file);

	/* For now, there is no wwan port ops API, so we simply let the wwan
	 * port driver implements its own fops.
	 */
	new_fops = fops_get(port->fops);
	replace_fops(file, new_fops);
	if (file->f_op->open)
		err = file->f_op->open(inode, file);

	put_device(&port->dev); /* balance wwan_port_get_by_minor */

	return err;
}

static const struct file_operations wwan_port_fops = {
	/* these fops will be replaced by registered wwan_port fops */
	.owner	= THIS_MODULE,
	.open	= wwan_port_open,
	.llseek = noop_llseek,
};

static int __init wwan_init(void)
{
	wwan_class = class_create(THIS_MODULE, "wwan");
	if (IS_ERR(wwan_class))
		return PTR_ERR(wwan_class);

	/* chrdev used for wwan ports */
	wwan_major = register_chrdev(0, "wwanport", &wwan_port_fops);
	if (wwan_major < 0) {
		class_destroy(wwan_class);
		return wwan_major;
	}

	return 0;
}

static void __exit wwan_exit(void)
{
	unregister_chrdev(wwan_major, "wwanport");
	class_destroy(wwan_class);
}

module_init(wwan_init);
module_exit(wwan_exit);

MODULE_AUTHOR("Loic Poulain <loic.poulain@linaro.org>");
MODULE_DESCRIPTION("WWAN core");
MODULE_LICENSE("GPL v2");
