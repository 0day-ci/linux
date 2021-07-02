// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * sysfs test driver
 *
 * Copyright (C) 2021 Luis Chamberlain <mcgrof@kernel.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or at your option any
 * later version; or, when distributed separately from the Linux kernel or
 * when incorporated into other software packages, subject to the following
 * license:
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of copyleft-next (version 0.3.1 or later) as published
 * at http://copyleft-next.org/.
 */

/*
 * This module allows us to add race conditions which we can test for
 * against the sysfs filesystem.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/async.h>
#include <linux/delay.h>
#include <linux/vmalloc.h>
#include <linux/debugfs.h>
#include <linux/rtnetlink.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/kernfs.h>

#ifdef CONFIG_FAIL_KERNFS_KNOBS
MODULE_IMPORT_NS(KERNFS_DEBUG_PRIVATE);
#endif

static bool enable_lock;
module_param(enable_lock, bool_enable_only, 0644);
MODULE_PARM_DESC(enable_lock,
		 "enable locking on reads / stores from the start");

static bool enable_lock_on_rmmod;
module_param(enable_lock_on_rmmod, bool_enable_only, 0644);
MODULE_PARM_DESC(enable_lock_on_rmmod,
		 "enable locking on rmmod");

static bool use_rtnl_lock;
module_param(use_rtnl_lock, bool_enable_only, 0644);
MODULE_PARM_DESC(use_rtnl_lock,
		 "use an rtnl_lock instead of the device mutex_lock");

static unsigned int write_delay_msec_y = 500;
module_param_named(write_delay_msec_y, write_delay_msec_y, uint, 0644);
MODULE_PARM_DESC(write_delay_msec_y, "msec write delay for writes to y");

static unsigned int test_devtype;
module_param_named(devtype, test_devtype, uint, 0644);
MODULE_PARM_DESC(devtype, "device type to register");

static bool enable_busy_alloc;
module_param(enable_busy_alloc, bool_enable_only, 0644);
MODULE_PARM_DESC(enable_busy_alloc, "do a fake allocation during writes");

static bool enable_debugfs;
module_param(enable_debugfs, bool_enable_only, 0644);
MODULE_PARM_DESC(enable_debugfs, "enable a few debugfs files");

static bool enable_verbose_writes;
module_param(enable_verbose_writes, bool_enable_only, 0644);
MODULE_PARM_DESC(enable_debugfs, "enable stores to print verbose information");

static unsigned int delay_rmmod_ms;
module_param_named(delay_rmmod_ms, delay_rmmod_ms, uint, 0644);
MODULE_PARM_DESC(delay_rmmod_ms, "if set how many ms to delay rmmod before device deletion");

static bool enable_verbose_rmmod;
module_param(enable_verbose_rmmod, bool_enable_only, 0644);
MODULE_PARM_DESC(enable_verbose_rmmod, "enable verbose print messages on rmmod");

#ifdef CONFIG_FAIL_KERNFS_KNOBS
static bool enable_completion_on_rmmod;
module_param(enable_completion_on_rmmod, bool_enable_only, 0644);
MODULE_PARM_DESC(enable_completion_on_rmmod,
		 "enable sending a kernfs completion on rmmod");
#endif

static int sysfs_test_major;

/**
 * test_config - used for configuring how the sysfs test device will behave
 *
 * @enable_lock: if enabled a lock will be used when reading/storing variables
 * @enable_lock_on_rmmod: if enabled a lock will be used when reading/storing
 *	sysfs attributes, but it will also be used to lock on rmmod. This is
 *	useful to test for a deadlock.
 * @use_rtnl_lock: if enabled instead of configuration specific mutex, we'll
 *	use the rtnl_lock. If your test case is modifying this on the fly
 *	while doing other stores / reads, things will break as a lock can be
 *	left contending. Best is that tests use this knob serially, without
 *	allowing userspace to modify other knobs while this one changes.
 * @write_delay_msec_y: the amount of delay to use when writing to y
 * @enable_busy_alloc: if enabled we'll do a large allocation between
 *	writes. We immediately free right away. We also schedule to give the
 *	kernel some time to re-use any memory we don't need. This is intened
 *	to mimic typical driver behaviour.
 */
struct test_config {
	bool enable_lock;
	bool enable_lock_on_rmmod;
	bool use_rtnl_lock;
	unsigned int write_delay_msec_y;
	bool enable_busy_alloc;
};

/**
 * enum sysfs_test_devtype - sysfs device type
 * @TESTDEV_TYPE_MISC: misc device type
 * @TESTDEV_TYPE_BLOCK: use a block device for the sysfs test device.
 */
enum sysfs_test_devtype {
	TESTDEV_TYPE_MISC = 0,
	TESTDEV_TYPE_BLOCK,
};

/**
 * sysfs_test_device - test device to help test sysfs
 *
 * @devtype: the type of device to use
 * @config: configuration for the test
 * @config_mutex: protects configuration of test
 * @misc_dev: we use a misc device under the hood
 * @disk: represents a disk when used as a block device
 * @dev: pointer to misc_dev's own struct device
 * @dev_idx: unique ID for test device
 * @x: variable we can use to test read / store
 * @y: slow variable we can use to test read / store
 */
struct sysfs_test_device {
	enum sysfs_test_devtype devtype;
	struct test_config config;
	struct mutex config_mutex;
	struct miscdevice misc_dev;
	struct gendisk *disk;
	struct device *dev;
	int dev_idx;
	int x;
	int y;
};

struct sysfs_test_device *first_test_dev;

static struct miscdevice *dev_to_misc_dev(struct device *dev)
{
	return dev_get_drvdata(dev);
}

static struct sysfs_test_device *misc_dev_to_test_dev(struct miscdevice *misc_dev)
{
	return container_of(misc_dev, struct sysfs_test_device, misc_dev);
}

static struct sysfs_test_device *devblock_to_test_dev(struct device *dev)
{
	return (struct sysfs_test_device *)dev_to_disk(dev)->private_data;
}

static struct sysfs_test_device *devmisc_to_testdev(struct device *dev)
{
	struct miscdevice *misc_dev;

	misc_dev = dev_to_misc_dev(dev);
	return misc_dev_to_test_dev(misc_dev);
}

static struct sysfs_test_device *dev_to_test_dev(struct device *dev)
{
	if (test_devtype == TESTDEV_TYPE_MISC)
		return devmisc_to_testdev(dev);
	else if (test_devtype == TESTDEV_TYPE_BLOCK)
		return devblock_to_test_dev(dev);
	return NULL;
}

static void test_dev_config_lock(struct sysfs_test_device *test_dev)
{
	struct test_config *config;

	config = &test_dev->config;
	if (config->enable_lock) {
		if (config->use_rtnl_lock)
			rtnl_lock();
		else
			mutex_lock(&test_dev->config_mutex);
	}
}

static void test_dev_config_unlock(struct sysfs_test_device *test_dev)
{
	struct test_config *config;

	config = &test_dev->config;
	if (config->enable_lock) {
		if (config->use_rtnl_lock)
			rtnl_unlock();
		else
			mutex_unlock(&test_dev->config_mutex);
	}
}

static void test_dev_config_lock_rmmod(struct sysfs_test_device *test_dev)
{
	struct test_config *config;

	config = &test_dev->config;
	if (config->enable_lock_on_rmmod)
		test_dev_config_lock(test_dev);
}

static void test_dev_config_unlock_rmmod(struct sysfs_test_device *test_dev)
{
	struct test_config *config;

	config = &test_dev->config;
	if (config->enable_lock_on_rmmod)
		test_dev_config_unlock(test_dev);
}

static void free_test_dev_sysfs(struct sysfs_test_device *test_dev)
{
	if (test_dev) {
		kfree_const(test_dev->misc_dev.name);
		test_dev->misc_dev.name = NULL;
		kfree(test_dev);
		test_dev = NULL;
	}
}

static void test_sysfs_reset_vals(struct sysfs_test_device *test_dev)
{
	test_dev->x = 3;
	test_dev->y = 4;
}

static ssize_t config_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	struct sysfs_test_device *test_dev = dev_to_test_dev(dev);
	struct test_config *config = &test_dev->config;
	int len = 0;

	test_dev_config_lock(test_dev);

	len += snprintf(buf, PAGE_SIZE,
			"Configuration for: %s\n",
			dev_name(dev));

	len += snprintf(buf+len, PAGE_SIZE - len,
			"x:\t%d\n",
			test_dev->x);

	len += snprintf(buf+len, PAGE_SIZE - len,
			"y:\t%d\n",
			test_dev->y);

	len += snprintf(buf+len, PAGE_SIZE - len,
			"enable_lock:\t%s\n",
			config->enable_lock ? "true" : "false");

	len += snprintf(buf+len, PAGE_SIZE - len,
			"enable_lock_on_rmmmod:\t%s\n",
			config->enable_lock_on_rmmod ? "true" : "false");

	len += snprintf(buf+len, PAGE_SIZE - len,
			"use_rtnl_lock:\t%s\n",
			config->use_rtnl_lock ? "true" : "false");

	len += snprintf(buf+len, PAGE_SIZE - len,
			"write_delay_msec_y:\t%d\n",
			config->write_delay_msec_y);

	len += snprintf(buf+len, PAGE_SIZE - len,
			"enable_busy_alloc:\t%s\n",
			config->enable_busy_alloc ? "true" : "false");

	len += snprintf(buf+len, PAGE_SIZE - len,
			"enable_debugfs:\t%s\n",
			enable_debugfs ? "true" : "false");

	len += snprintf(buf+len, PAGE_SIZE - len,
			"enable_verbose_writes:\t%s\n",
			enable_verbose_writes ? "true" : "false");

#ifdef CONFIG_FAIL_KERNFS_KNOBS
	len += snprintf(buf+len, PAGE_SIZE - len,
			"enable_completion_on_rmmod:\t%s\n",
			enable_completion_on_rmmod ? "true" : "false");
#endif

	test_dev_config_unlock(test_dev);

	return len;
}
static DEVICE_ATTR_RO(config);

static ssize_t reset_store(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct sysfs_test_device *test_dev = dev_to_test_dev(dev);
	struct test_config *config;

	config = &test_dev->config;

	/*
	 * We compromise and simplify this condition and do not use a lock
	 * here as the lock type can change.
	 */
	config->enable_lock = false;
	config->enable_lock_on_rmmod = false;
	config->use_rtnl_lock = false;
	config->enable_busy_alloc = false;
	test_sysfs_reset_vals(test_dev);

	dev_info(dev, "reset\n");

	return count;
}
static DEVICE_ATTR_WO(reset);

static void test_dev_busy_alloc(struct sysfs_test_device *test_dev)
{
	struct test_config *config;
	char *ignore;

	config = &test_dev->config;
	if (!config->enable_busy_alloc)
		return;

	ignore = kzalloc(sizeof(struct sysfs_test_device) * 10, GFP_KERNEL);
	kfree(ignore);

	schedule();
}

static ssize_t test_dev_x_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct sysfs_test_device *test_dev = dev_to_test_dev(dev);
	int ret;

	test_dev_busy_alloc(test_dev);
	test_dev_config_lock(test_dev);

	ret = kstrtoint(buf, 10, &test_dev->x);
	if (ret)
		count = ret;

	if (enable_verbose_writes)
		dev_info(test_dev->dev, "wrote x = %d\n", test_dev->x);

	test_dev_config_unlock(test_dev);

	return count;
}

static ssize_t test_dev_x_show(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	struct sysfs_test_device *test_dev = dev_to_test_dev(dev);
	int ret;

	test_dev_config_lock(test_dev);
	ret = snprintf(buf, PAGE_SIZE, "%d\n", test_dev->x);
	test_dev_config_unlock(test_dev);

	return ret;
}
static DEVICE_ATTR_RW(test_dev_x);

static ssize_t test_dev_y_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct sysfs_test_device *test_dev = dev_to_test_dev(dev);
	struct test_config *config;
	int y;
	int ret;

	test_dev_busy_alloc(test_dev);
	test_dev_config_lock(test_dev);

	config = &test_dev->config;

	ret = kstrtoint(buf, 10, &y);
	if (ret)
		count = ret;

	msleep(config->write_delay_msec_y);
	test_dev->y = test_dev->x + y + 7;

	if (enable_verbose_writes)
		dev_info(test_dev->dev, "wrote y = %d\n", test_dev->y);

	test_dev_config_unlock(test_dev);

	return count;
}

static ssize_t test_dev_y_show(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	struct sysfs_test_device *test_dev = dev_to_test_dev(dev);
	int ret;

	test_dev_config_lock(test_dev);
	ret = snprintf(buf, PAGE_SIZE, "%d\n", test_dev->y);
	test_dev_config_unlock(test_dev);

	return ret;
}
static DEVICE_ATTR_RW(test_dev_y);

static ssize_t config_enable_lock_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct sysfs_test_device *test_dev = dev_to_test_dev(dev);
	struct test_config *config;
	int ret;
	int val;

	config = &test_dev->config;
	ret = kstrtoint(buf, 10, &val);
	if (ret)
		return ret;

	/*
	 * We compromise for simplicty and do not lock when changing
	 * locking configuration, with the assumption userspace tests
	 * will know this.
	 */
	if (val)
		config->enable_lock = true;
	else
		config->enable_lock = false;

	return count;
}

static ssize_t config_enable_lock_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct sysfs_test_device *test_dev = dev_to_test_dev(dev);
	struct test_config *config;
	ssize_t ret;

	config = &test_dev->config;

	test_dev_config_lock(test_dev);
	ret = snprintf(buf, PAGE_SIZE, "%d\n", config->enable_lock);
	test_dev_config_unlock(test_dev);

	return ret;
}
static DEVICE_ATTR_RW(config_enable_lock);

static ssize_t config_enable_lock_on_rmmod_store(struct device *dev,
						 struct device_attribute *attr,
						 const char *buf, size_t count)
{
	struct sysfs_test_device *test_dev = dev_to_test_dev(dev);
	struct test_config *config;
	int ret;
	int val;

	config = &test_dev->config;
	ret = kstrtoint(buf, 10, &val);
	if (ret)
		return ret;

	test_dev_config_lock(test_dev);
	if (val)
		config->enable_lock_on_rmmod = true;
	else
		config->enable_lock_on_rmmod = false;
	test_dev_config_unlock(test_dev);

	return count;
}

static ssize_t config_enable_lock_on_rmmod_show(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	struct sysfs_test_device *test_dev = dev_to_test_dev(dev);
	struct test_config *config;
	ssize_t ret;

	config = &test_dev->config;

	test_dev_config_lock(test_dev);
	ret = snprintf(buf, PAGE_SIZE, "%d\n", config->enable_lock_on_rmmod);
	test_dev_config_unlock(test_dev);

	return ret;
}
static DEVICE_ATTR_RW(config_enable_lock_on_rmmod);

static ssize_t config_use_rtnl_lock_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct sysfs_test_device *test_dev = dev_to_test_dev(dev);
	struct test_config *config;
	int ret;
	int val;

	config = &test_dev->config;
	ret = kstrtoint(buf, 10, &val);
	if (ret)
		return ret;

	/*
	 * We compromise and simplify this condition and do not use a lock
	 * here as the lock type can change.
	 */
	if (val)
		config->use_rtnl_lock = true;
	else
		config->use_rtnl_lock = false;

	return count;
}

static ssize_t config_use_rtnl_lock_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct sysfs_test_device *test_dev = dev_to_test_dev(dev);
	struct test_config *config;

	config = &test_dev->config;

	return snprintf(buf, PAGE_SIZE, "%d\n", config->use_rtnl_lock);
}
static DEVICE_ATTR_RW(config_use_rtnl_lock);

static ssize_t config_write_delay_msec_y_store(struct device *dev,
					       struct device_attribute *attr,
					       const char *buf, size_t count)
{
	struct sysfs_test_device *test_dev = dev_to_test_dev(dev);
	struct test_config *config;
	int ret;
	int val;

	config = &test_dev->config;
	ret = kstrtoint(buf, 10, &val);
	if (ret)
		return ret;

	test_dev_config_lock(test_dev);
	config->write_delay_msec_y = val;
	test_dev_config_unlock(test_dev);

	return count;
}

static ssize_t config_write_delay_msec_y_show(struct device *dev,
					      struct device_attribute *attr,
					      char *buf)
{
	struct sysfs_test_device *test_dev = dev_to_test_dev(dev);
	struct test_config *config;

	config = &test_dev->config;

	return snprintf(buf, PAGE_SIZE, "%d\n", config->write_delay_msec_y);
}
static DEVICE_ATTR_RW(config_write_delay_msec_y);

static ssize_t config_enable_busy_alloc_store(struct device *dev,
					      struct device_attribute *attr,
					      const char *buf, size_t count)
{
	struct sysfs_test_device *test_dev = dev_to_test_dev(dev);
	struct test_config *config;
	int ret;
	int val;

	config = &test_dev->config;
	ret = kstrtoint(buf, 10, &val);
	if (ret)
		return ret;

	test_dev_config_lock(test_dev);
	config->enable_busy_alloc = val;
	test_dev_config_unlock(test_dev);

	return count;
}

static ssize_t config_enable_busy_alloc_show(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	struct sysfs_test_device *test_dev = dev_to_test_dev(dev);
	struct test_config *config;

	config = &test_dev->config;

	return snprintf(buf, PAGE_SIZE, "%d\n", config->enable_busy_alloc);
}
static DEVICE_ATTR_RW(config_enable_busy_alloc);

#define TEST_SYSFS_DEV_ATTR(name)		(&dev_attr_##name.attr)

static struct attribute *test_dev_attrs[] = {
	/* Generic driver knobs go here */
	TEST_SYSFS_DEV_ATTR(config),
	TEST_SYSFS_DEV_ATTR(reset),

	/* These are used to test sysfs */
	TEST_SYSFS_DEV_ATTR(test_dev_x),
	TEST_SYSFS_DEV_ATTR(test_dev_y),

	/*
	 * These are configuration knobs to modify how we test sysfs when
	 * doing reads / stores.
	 */
	TEST_SYSFS_DEV_ATTR(config_enable_lock),
	TEST_SYSFS_DEV_ATTR(config_enable_lock_on_rmmod),
	TEST_SYSFS_DEV_ATTR(config_use_rtnl_lock),
	TEST_SYSFS_DEV_ATTR(config_write_delay_msec_y),
	TEST_SYSFS_DEV_ATTR(config_enable_busy_alloc),

	NULL,
};

ATTRIBUTE_GROUPS(test_dev);

static int sysfs_test_dev_alloc_miscdev(struct sysfs_test_device *test_dev)
{
	struct miscdevice *misc_dev;

	misc_dev = &test_dev->misc_dev;
	misc_dev->minor = MISC_DYNAMIC_MINOR;
	misc_dev->name = kasprintf(GFP_KERNEL, "test_sysfs%d", test_dev->dev_idx);
	if (!misc_dev->name) {
		pr_err("Cannot alloc misc_dev->name\n");
		return -ENOMEM;
	}
	misc_dev->groups = test_dev_groups;

	return 0;
}

static int testdev_open(struct block_device *bdev, fmode_t mode)
{
	return -EINVAL;
}

static blk_qc_t testdev_submit_bio(struct bio *bio)
{
	return BLK_QC_T_NONE;
}

static void testdev_slot_free_notify(struct block_device *bdev,
				     unsigned long index)
{
}

static int testdev_rw_page(struct block_device *bdev, sector_t sector,
			   struct page *page, unsigned int op)
{
	return -EOPNOTSUPP;
}

static const struct block_device_operations sysfs_testdev_ops = {
	.open = testdev_open,
	.submit_bio = testdev_submit_bio,
	.swap_slot_free_notify = testdev_slot_free_notify,
	.rw_page = testdev_rw_page,
	.owner = THIS_MODULE
};

static int sysfs_test_dev_alloc_blockdev(struct sysfs_test_device *test_dev)
{
	struct request_queue *queue;
	int ret = -ENOMEM;

	queue = blk_alloc_queue(NUMA_NO_NODE);
	if (!queue) {
		pr_err("Error allocating disk queue for device %d\n",
			test_dev->dev_idx);
		goto out;
	}
	test_dev->disk = alloc_disk(1);
	if (!test_dev->disk) {
		pr_err("Error allocating disk structure for device %d\n",
		       test_dev->dev_idx);
		blk_cleanup_queue(queue);
		goto out;
	}

	test_dev->disk->major = sysfs_test_major;
	test_dev->disk->first_minor = test_dev->dev_idx + 1;
	test_dev->disk->fops = &sysfs_testdev_ops;
	test_dev->disk->queue = queue;
	test_dev->disk->private_data = test_dev;
	snprintf(test_dev->disk->disk_name, 16, "test_sysfs%d",
		 test_dev->dev_idx);
	set_capacity(test_dev->disk, 0);
	blk_queue_flag_set(QUEUE_FLAG_NONROT, test_dev->disk->queue);
	blk_queue_flag_clear(QUEUE_FLAG_ADD_RANDOM, test_dev->disk->queue);
	blk_queue_physical_block_size(test_dev->disk->queue, PAGE_SIZE);
	blk_queue_max_discard_sectors(test_dev->disk->queue, UINT_MAX);
	blk_queue_flag_set(QUEUE_FLAG_DISCARD, test_dev->disk->queue);

	return 0;
out:
	return ret;
}

static struct sysfs_test_device *alloc_test_dev_sysfs(int idx)
{
	struct sysfs_test_device *test_dev;
	int ret;

	switch (test_devtype) {
	case TESTDEV_TYPE_MISC:
	       fallthrough;
	case TESTDEV_TYPE_BLOCK:
		break;
	default:
		return NULL;
	}

	test_dev = kzalloc(sizeof(struct sysfs_test_device), GFP_KERNEL);
	if (!test_dev)
		goto err_out;

	mutex_init(&test_dev->config_mutex);
	test_dev->dev_idx = idx;
	test_dev->devtype = test_devtype;

	if (test_dev->devtype == TESTDEV_TYPE_MISC) {
		ret = sysfs_test_dev_alloc_miscdev(test_dev);
		if (ret)
			goto err_out_free;
	} else if (test_dev->devtype == TESTDEV_TYPE_BLOCK) {
		ret = sysfs_test_dev_alloc_blockdev(test_dev);
		if (ret)
			goto err_out_free;
	}
	return test_dev;

err_out_free:
	kfree(test_dev);
	test_dev = NULL;
err_out:
	return NULL;
}

static int register_test_dev_sysfs_misc(struct sysfs_test_device *test_dev)
{
	int ret;

	ret = misc_register(&test_dev->misc_dev);
	if (ret)
		return ret;

	test_dev->dev = test_dev->misc_dev.this_device;

	return 0;
}

static int register_test_dev_sysfs_block(struct sysfs_test_device *test_dev)
{
	device_add_disk(NULL, test_dev->disk, test_dev_groups);
	test_dev->dev = disk_to_dev(test_dev->disk);

	return 0;
}

static struct sysfs_test_device *register_test_dev_sysfs(void)
{
	struct sysfs_test_device *test_dev = NULL;
	int ret;

	test_dev = alloc_test_dev_sysfs(0);
	if (!test_dev)
		goto out;

	if (test_dev->devtype == TESTDEV_TYPE_MISC) {
		ret = register_test_dev_sysfs_misc(test_dev);
		if (ret) {
			pr_err("could not register misc device: %d\n", ret);
			goto out_free_dev;
		}
	} else if (test_dev->devtype == TESTDEV_TYPE_BLOCK) {
		ret = register_test_dev_sysfs_block(test_dev);
		if (ret) {
			pr_err("could not register block device: %d\n", ret);
			goto out_free_dev;
		}
	}

	dev_info(test_dev->dev, "interface ready\n");

out:
	return test_dev;
out_free_dev:
	free_test_dev_sysfs(test_dev);
	return NULL;
}

static struct sysfs_test_device *register_test_dev_set_config(void)
{
	struct sysfs_test_device *test_dev;
	struct test_config *config;

	test_dev = register_test_dev_sysfs();
	if (!test_dev)
		return NULL;

	config = &test_dev->config;

	if (enable_lock)
		config->enable_lock = true;
	if (enable_lock_on_rmmod)
		config->enable_lock_on_rmmod = true;
	if (use_rtnl_lock)
		config->use_rtnl_lock = true;
	if (enable_busy_alloc)
		config->enable_busy_alloc = true;

	config->write_delay_msec_y = write_delay_msec_y;
	test_sysfs_reset_vals(test_dev);

	return test_dev;
}

static void unregister_test_dev_sysfs_misc(struct sysfs_test_device *test_dev)
{
	misc_deregister(&test_dev->misc_dev);
}

static void unregister_test_dev_sysfs_block(struct sysfs_test_device *test_dev)
{
	del_gendisk(test_dev->disk);
	blk_cleanup_queue(test_dev->disk->queue);
	put_disk(test_dev->disk);
}

static void unregister_test_dev_sysfs(struct sysfs_test_device *test_dev)
{
	test_dev_config_lock_rmmod(test_dev);

	dev_info(test_dev->dev, "removing interface\n");

	if (test_dev->devtype == TESTDEV_TYPE_MISC)
		unregister_test_dev_sysfs_misc(test_dev);
	else if (test_dev->devtype == TESTDEV_TYPE_BLOCK)
		unregister_test_dev_sysfs_block(test_dev);

	test_dev_config_unlock_rmmod(test_dev);

	free_test_dev_sysfs(test_dev);
}

static struct dentry *debugfs_dir;

/* When read represents how many times we have reset the first_test_dev */
static u8 reset_first_test_dev;

static ssize_t read_reset_first_test_dev(struct file *file,
					 char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	ssize_t len;
	char buf[32];

	reset_first_test_dev++;
	len = sprintf(buf, "%d\n", reset_first_test_dev);
	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t write_reset_first_test_dev(struct file *file,
					  const char __user *user_buf,
					  size_t count, loff_t *ppos)
{
	if (!try_module_get(THIS_MODULE))
		return -ENODEV;

	if (!first_test_dev) {
		module_put(THIS_MODULE);
		return -ENODEV;
	}

	dev_info(first_test_dev->dev, "going to reset first interface ...\n");

	unregister_test_dev_sysfs(first_test_dev);
	first_test_dev = register_test_dev_set_config();

	dev_info(first_test_dev->dev, "first interface reset complete\n");

	module_put(THIS_MODULE);

	return count;
}

static const struct file_operations fops_reset_first_test_dev = {
	.read = read_reset_first_test_dev,
	.write = write_reset_first_test_dev,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static int __init test_sysfs_init(void)
{
	first_test_dev = register_test_dev_set_config();
	if (!first_test_dev)
		return -ENOMEM;

	if (!enable_debugfs)
		return 0;

	debugfs_dir = debugfs_create_dir("test_sysfs", NULL);
	if (!debugfs_dir) {
		unregister_test_dev_sysfs(first_test_dev);
		return -ENOMEM;
	}

	debugfs_create_file("reset_first_test_dev", 0600, debugfs_dir,
			    NULL, &fops_reset_first_test_dev);
	return 0;
}
module_init(test_sysfs_init);

#ifdef CONFIG_FAIL_KERNFS_KNOBS
/* The goal is to race our device removal with a pending kernfs -> store call */
static void test_sysfs_kernfs_send_completion_rmmod(void)
{
	if (!enable_completion_on_rmmod)
		return;
	complete(&kernfs_debug_wait_completion);
}
#else
static inline void test_sysfs_kernfs_send_completion_rmmod(void) {}
#endif

static void __exit test_sysfs_exit(void)
{
	if (enable_debugfs)
		debugfs_remove(debugfs_dir);
	test_sysfs_kernfs_send_completion_rmmod();
	if (delay_rmmod_ms)
		msleep(delay_rmmod_ms);
	unregister_test_dev_sysfs(first_test_dev);
	if (enable_verbose_rmmod)
		pr_info("unregister_test_dev_sysfs() completed\n");
	first_test_dev = NULL;
}
module_exit(test_sysfs_exit);

MODULE_AUTHOR("Luis Chamberlain <mcgrof@kernel.org>");
MODULE_LICENSE("GPL");
