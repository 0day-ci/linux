// SPDX-License-Identifier: GPL-2.0
/*
 *  Block device concurrent positioning ranges.
 *
 *  Copyright (C) 2021 Western Digital Corporation or its Affiliates.
 */
#include <linux/kernel.h>
#include <linux/blkdev.h>
#include <linux/slab.h>
#include <linux/init.h>

#include "blk.h"

static ssize_t blk_crange_sector_show(struct blk_crange *cr, char *page)
{
	return sprintf(page, "%llu\n", cr->sector);
}

static ssize_t blk_crange_nr_sectors_show(struct blk_crange *cr, char *page)
{
	return sprintf(page, "%llu\n", cr->nr_sectors);
}

struct blk_crange_sysfs_entry {
	struct attribute attr;
	ssize_t (*show)(struct blk_crange *cr, char *page);
};

static struct blk_crange_sysfs_entry blk_crange_sector_entry = {
	.attr = { .name = "sector", .mode = 0444 },
	.show = blk_crange_sector_show,
};

static struct blk_crange_sysfs_entry blk_crange_nr_sectors_entry = {
	.attr = { .name = "nr_sectors", .mode = 0444 },
	.show = blk_crange_nr_sectors_show,
};

static struct attribute *blk_crange_attrs[] = {
	&blk_crange_sector_entry.attr,
	&blk_crange_nr_sectors_entry.attr,
	NULL,
};
ATTRIBUTE_GROUPS(blk_crange);

static ssize_t blk_crange_sysfs_show(struct kobject *kobj,
				     struct attribute *attr, char *page)
{
	struct blk_crange_sysfs_entry *entry =
		container_of(attr, struct blk_crange_sysfs_entry, attr);
	struct blk_crange *cr = container_of(kobj, struct blk_crange, kobj);
	ssize_t ret;

	mutex_lock(&cr->queue->sysfs_lock);
	ret = entry->show(cr, page);
	mutex_unlock(&cr->queue->sysfs_lock);

	return ret;
}

static const struct sysfs_ops blk_crange_sysfs_ops = {
	.show	= blk_crange_sysfs_show,
};

/*
 * crange entries are not freed individually, but alltogether with the
 * struct blk_cranges and its array of range entries. since kobject_add()
 * takes a reference on the parent struct blk_cranges kobj, the array of
 * crange entries cannot be freed until kobject_del() is called for all entries.
 * So we do not need to do anything here, but still need this nop release
 * operation to avoid complaints from the kobject code.
 */
static void blk_crange_sysfs_nop_release(struct kobject *kobj)
{
}

static struct kobj_type blk_crange_ktype = {
	.sysfs_ops	= &blk_crange_sysfs_ops,
	.default_groups	= blk_crange_groups,
	.release	= blk_crange_sysfs_nop_release,
};

/*
 * This will be executed only after all range entries are removed
 * with kobject_del(), at which point, it is safe to free everything,
 * including the array of range entries.
 */
static void blk_cranges_sysfs_release(struct kobject *kobj)
{
	struct blk_cranges *cranges =
		container_of(kobj, struct blk_cranges, kobj);

	kfree(cranges);
}

static struct kobj_type blk_cranges_ktype = {
	.release	= blk_cranges_sysfs_release,
};

/**
 * blk_register_cranges - register with sysfs a set of concurrent ranges
 * @disk:		Target disk
 * @new_cranges:	New set of concurrent ranges
 *
 * Register with sysfs a set of concurrent ranges for @disk. If @new_cranges
 * is not NULL, this set of concurrent ranges is registered and the
 * old set specified by q->cranges is unregistered. Otherwise, q->cranges
 * is registered if it is not already.
 */
int disk_register_cranges(struct gendisk *disk, struct blk_cranges *new_cranges)
{
	struct request_queue *q = disk->queue;
	struct blk_cranges *cranges;
	int i, ret;

	lockdep_assert_held(&q->sysfs_dir_lock);
	lockdep_assert_held(&q->sysfs_lock);

	/* If a new range set is specified, unregister the old one */
	if (new_cranges) {
		if (q->cranges)
			disk_unregister_cranges(disk);
		q->cranges = new_cranges;
	}

	cranges = q->cranges;
	if (!cranges)
		return 0;

	/*
	 * At this point, cranges is the new set of sector ranges that needs
	 * to be registered with sysfs.
	 */
	WARN_ON(cranges->sysfs_registered);
	ret = kobject_init_and_add(&cranges->kobj, &blk_cranges_ktype,
				   &q->kobj, "%s", "cranges");
	if (ret) {
		q->cranges = NULL;
		kfree(cranges);
		return ret;
	}

	for (i = 0; i < cranges->nr_ranges; i++) {
		cranges->ranges[i].queue = q;
		ret = kobject_init_and_add(&cranges->ranges[i].kobj,
					   &blk_crange_ktype, &cranges->kobj,
					   "%d", i);
		if (ret) {
			while (--i >= 0)
				kobject_del(&cranges->ranges[i].kobj);
			kobject_del(&cranges->kobj);
			kobject_put(&cranges->kobj);
			return ret;
		}
	}

	cranges->sysfs_registered = true;

	return 0;
}

void disk_unregister_cranges(struct gendisk *disk)
{
	struct request_queue *q = disk->queue;
	struct blk_cranges *cranges = q->cranges;
	int i;

	lockdep_assert_held(&q->sysfs_dir_lock);
	lockdep_assert_held(&q->sysfs_lock);

	if (!cranges)
		return;

	if (cranges->sysfs_registered) {
		for (i = 0; i < cranges->nr_ranges; i++)
			kobject_del(&cranges->ranges[i].kobj);
		kobject_del(&cranges->kobj);
		kobject_put(&cranges->kobj);
	} else {
		kfree(cranges);
	}

	q->cranges = NULL;
}

static bool disk_check_ranges(struct gendisk *disk, struct blk_cranges *cr)
{
	sector_t capacity = get_capacity(disk);
	sector_t min_sector = (sector_t)-1;
	sector_t max_sector = 0;
	int i;

	/*
	 * Sector ranges may overlap but should overall contain all sectors
	 * within the disk capacity.
	 */
	for (i = 0; i < cr->nr_ranges; i++) {
		min_sector = min(min_sector, cr->ranges[i].sector);
		max_sector = max(max_sector, cr->ranges[i].sector +
					     cr->ranges[i].nr_sectors);
	}

	if (min_sector != 0 || max_sector < capacity) {
		pr_warn("Invalid concurrent ranges: missing sectors\n");
		return false;
	}

	if (max_sector > capacity) {
		pr_warn("Invalid concurrent ranges: beyond capacity\n");
		return false;
	}

	return true;
}

static bool disk_cranges_changed(struct gendisk *disk, struct blk_cranges *new)
{
	struct blk_cranges *old = disk->queue->cranges;
	int i;

	if (!old)
		return true;

	if (old->nr_ranges != new->nr_ranges)
		return true;

	for (i = 0; i < old->nr_ranges; i++) {
		if (new->ranges[i].sector != old->ranges[i].sector ||
		    new->ranges[i].nr_sectors != old->ranges[i].nr_sectors)
			return true;
	}

	return false;
}

/**
 * disk_alloc_cranges - Allocate a concurrent positioning range structure
 * @disk:	target disk
 * @nr_ranges:	Number of concurrent ranges
 *
 * Allocate a struct blk_cranges structure with @nr_ranges range descriptors.
 */
struct blk_cranges *disk_alloc_cranges(struct gendisk *disk, int nr_ranges)
{
	struct blk_cranges *cr;

	cr = kzalloc_node(struct_size(cr, ranges, nr_ranges), GFP_KERNEL,
			  disk->queue->node);
	if (cr)
		cr->nr_ranges = nr_ranges;
	return cr;
}
EXPORT_SYMBOL_GPL(disk_alloc_cranges);

/**
 * disk_set_cranges - Set a disk concurrent positioning ranges
 * @disk:	target disk
 * @cr:		concurrent ranges structure
 *
 * Set the concurrant positioning ranges information of the request queue
 * of @disk to @cr. If @cr is NULL and the concurrent ranges structure
 * already set, if any, is cleared. If there are no differences between
 * @cr and the concurrent ranges structure already set, @cr is freed.
 */
void disk_set_cranges(struct gendisk *disk, struct blk_cranges *cr)
{
	struct request_queue *q = disk->queue;

	if (WARN_ON_ONCE(cr && !cr->nr_ranges)) {
		kfree(cr);
		cr = NULL;
	}

	mutex_lock(&q->sysfs_dir_lock);
	mutex_lock(&q->sysfs_lock);

	if (cr) {
		if (!disk_check_ranges(disk, cr)) {
			kfree(cr);
			cr = NULL;
			goto reg;
		}

		if (!disk_cranges_changed(disk, cr)) {
			kfree(cr);
			goto unlock;
		}
	}

	/*
	 * This may be called for a registered queue. E.g. during a device
	 * revalidation. If that is the case, we need to unregister the old
	 * set of concurrent ranges and register the new set. If the queue
	 * is not registered, the device request queue registration will
	 * register the ranges, so only swap in the new set and free the
	 * old one.
	 */
reg:
	if (blk_queue_registered(q)) {
		disk_register_cranges(disk, cr);
	} else {
		swap(q->cranges, cr);
		kfree(cr);
	}

unlock:
	mutex_unlock(&q->sysfs_lock);
	mutex_unlock(&q->sysfs_dir_lock);
}
EXPORT_SYMBOL_GPL(disk_set_cranges);
