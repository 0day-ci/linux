// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 - Google LLC
 * Author: David Brazdil <dbrazdil@google.com>
 *
 * Driver for Open Profile for DICE.
 *
 * This driver takes ownership of a reserved memory region containing secrets
 * derived following the Open Profile for DICE. The contents of the memory
 * region are not interpreted by the kernel but can be mapped into a userspace
 * process via a character device. The memory region can also be wiped, removing
 * the secrets from memory.
 *
 * Userspace can access the data by (w/o error handling):
 *
 *     int fd = open("/dev/dice", O_RDONLY | O_CLOEXEC);
 *     size_t size = ioctl(fd, DICE_GET_SIZE);
 *     ioctl(fd, DICE_SET_WIPE_ON_CLOSE);
 *     void *data = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
 *     close(fd);
 */

#include <linux/cdev.h>
#include <linux/dice.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>

#define DICE_MKDEV		MKDEV(MAJOR(dice_devt), 0)
#define DICE_MINOR_COUNT	1

enum dice_state {
	DICE_STATE_READY = 0,
	DICE_STATE_BUSY,
	DICE_STATE_BUSY_WIPE_ON_CLOSE,
	DICE_STATE_WIPED,
};

struct dice_data {
	struct device *dev;
	struct cdev cdev;
	atomic_t state;
	phys_addr_t base;
	size_t size;
};

static dev_t dice_devt;
static struct class *dice_class;

static int dice_open(struct inode *inode, struct file *filp)
{
	struct dice_data *data;

	data = container_of(inode->i_cdev, struct dice_data, cdev);

	/* Never allow write access. */
	if (filp->f_mode & FMODE_WRITE)
		return -EROFS;

	switch (atomic_cmpxchg(&data->state, DICE_STATE_READY, DICE_STATE_BUSY)) {
	case DICE_STATE_READY:
		break;
	case DICE_STATE_WIPED:
		/* Return error to inform caller memory has been wiped. */
		return -EACCES;
	default:
		return -EBUSY;
	}

	filp->private_data = data;
	nonseekable_open(inode, filp);
	return 0;
}

static int dice_release(struct inode *inode, struct file *filp)
{
	struct dice_data *data = filp->private_data;
	void *base;

	if (atomic_read(&data->state) == DICE_STATE_BUSY_WIPE_ON_CLOSE) {
		base = devm_memremap(data->dev, data->base, data->size, MEMREMAP_WT);
		if (!WARN_ON(!base)) {
			memzero_explicit(base, data->size);
			devm_memunmap(data->dev, base);
		}
		atomic_set(&data->state, DICE_STATE_WIPED);
		return 0;
	}

	atomic_set(&data->state, DICE_STATE_READY);
	return 0;
}

static int dice_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct dice_data *data = filp->private_data;

	vma->vm_flags |= VM_DONTCOPY;
	return vm_iomap_memory(vma, data->base, data->size);
}

static long dice_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct dice_data *data = filp->private_data;

	switch (cmd) {
	case DICE_GET_SIZE:
		/* Checked against INT_MAX in dice_probe(). */
		return data->size;
	case DICE_SET_WIPE_ON_CLOSE:
		atomic_set(&data->state, DICE_STATE_BUSY_WIPE_ON_CLOSE);
		return 0;
	}

	return -EINVAL;
}

static const struct file_operations dice_fops = {
	.open = dice_open,
	.release = dice_release,
	.mmap = dice_mmap,
	.unlocked_ioctl = dice_ioctl,
	.llseek = no_llseek,
};

static int __init dice_probe(struct platform_device *pdev)
{
	struct device *chr_dev, *dev = &pdev->dev;
	struct device_node *rmem_np;
	struct reserved_mem *rmem;
	struct dice_data *data;
	int ret;

	rmem_np = of_parse_phandle(dev->of_node, "memory-region", 0);
	if (!rmem_np) {
		dev_err(dev, "missing 'memory-region' property\n");
		return -EINVAL;
	}

	rmem = of_reserved_mem_lookup(rmem_np);
	of_node_put(rmem_np);
	if (!rmem) {
		dev_err(dev, "failed to lookup reserved memory\n");
		return -EINVAL;
	}

	if (!PAGE_ALIGNED(rmem->base) || !PAGE_ALIGNED(rmem->size)) {
		dev_err(dev, "memory region must be page-aligned\n");
		return -EINVAL;
	}

	if (rmem->size > INT_MAX) {
		dev_err(dev, "memory region too large\n");
		return -EINVAL;
	}

	data = devm_kmalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	*data = (struct dice_data){
		.dev = dev,
		.base = rmem->base,
		.size = rmem->size,
		.state = ATOMIC_INIT(DICE_STATE_READY),
	};

	cdev_init(&data->cdev, &dice_fops);
	data->cdev.owner = THIS_MODULE;
	ret = cdev_add(&data->cdev, DICE_MKDEV, 1);
	if (ret)
		return ret;

	chr_dev = device_create(dice_class, dev, DICE_MKDEV, NULL, "dice");
	if (IS_ERR(chr_dev)) {
		cdev_del(&data->cdev);
		return PTR_ERR(chr_dev);
	}

	platform_set_drvdata(pdev, data);
	return 0;
}

static int dice_remove(struct platform_device *pdev)
{
	struct dice_data *data = platform_get_drvdata(pdev);

	cdev_del(&data->cdev);
	device_destroy(dice_class, DICE_MKDEV);
	return 0;
}

static char *dice_devnode(struct device *dev, umode_t *mode)
{
	/* Initial permissions: read-only by owner */
	if (mode)
		*mode = 0400;
	return NULL;
}

static const struct of_device_id dice_of_match[] = {
	{ .compatible = "google,dice" },
	{},
};

static struct platform_driver dice_driver = {
	.remove = dice_remove,
	.driver = {
		.name = "dice",
		.of_match_table = dice_of_match,
	},
};

static int __init dice_init(void)
{
	int ret;

	ret = alloc_chrdev_region(&dice_devt, 0, DICE_MINOR_COUNT, "dice");
	if (ret)
		return ret;

	dice_class = class_create(THIS_MODULE, "dice");
	if (IS_ERR(dice_class)) {
		ret = PTR_ERR(dice_class);
		goto fail;
	}
	dice_class->devnode = dice_devnode;

	ret = platform_driver_probe(&dice_driver, dice_probe);
	if (ret)
		goto fail;

	return 0;

fail:
	class_destroy(dice_class);
	unregister_chrdev_region(dice_devt, DICE_MINOR_COUNT);
	return ret;
}

static void __exit dice_exit(void)
{
	platform_driver_unregister(&dice_driver);
	class_destroy(dice_class);
	unregister_chrdev_region(dice_devt, DICE_MINOR_COUNT);
}

module_init(dice_init);
module_exit(dice_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("David Brazdil <dbrazdil@google.com>");
