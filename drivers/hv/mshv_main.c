// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020, Microsoft Corporation.
 *
 * Authors:
 *   Nuno Das Neves <nudasnev@microsoft.com>
 *   Lillian Grassin-Drake <ligrassi@microsoft.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/anon_inodes.h>
#include <linux/mm.h>
#include <linux/mshv.h>
#include <asm/mshyperv.h>

#include "mshv.h"

MODULE_AUTHOR("Microsoft");
MODULE_LICENSE("GPL");

struct mshv mshv = {};

static void mshv_partition_put(struct mshv_partition *partition);
static int mshv_partition_release(struct inode *inode, struct file *filp);
static long mshv_partition_ioctl(struct file *filp, unsigned int ioctl, unsigned long arg);
static int mshv_dev_open(struct inode *inode, struct file *filp);
static int mshv_dev_release(struct inode *inode, struct file *filp);
static long mshv_dev_ioctl(struct file *filp, unsigned int ioctl, unsigned long arg);

static const struct file_operations mshv_partition_fops = {
	.release = mshv_partition_release,
	.unlocked_ioctl = mshv_partition_ioctl,
	.llseek = noop_llseek,
};

static const struct file_operations mshv_dev_fops = {
	.owner = THIS_MODULE,
	.open = mshv_dev_open,
	.release = mshv_dev_release,
	.unlocked_ioctl = mshv_dev_ioctl,
	.llseek = noop_llseek,
};

static struct miscdevice mshv_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "mshv",
	.fops = &mshv_dev_fops,
	.mode = 600,
};

static long
mshv_partition_ioctl(struct file *filp, unsigned int ioctl, unsigned long arg)
{
	return -ENOTTY;
}

static void
destroy_partition(struct mshv_partition *partition)
{
	unsigned long flags;
	int i;

	/* Remove from list of partitions */
	spin_lock_irqsave(&mshv.partitions.lock, flags);

	for (i = 0; i < MSHV_MAX_PARTITIONS; ++i) {
		if (mshv.partitions.array[i] == partition)
			break;
	}

	if (i == MSHV_MAX_PARTITIONS) {
		pr_err("%s: failed to locate partition in array\n", __func__);
	} else {
		mshv.partitions.count--;
		mshv.partitions.array[i] = NULL;
	}

	spin_unlock_irqrestore(&mshv.partitions.lock, flags);

	/*
	 * There are no remaining references to the partition,
	 * so the remaining cleanup can be lockless
	 */

	/* Deallocates and unmaps everything including vcpus, GPA mappings etc */
	hv_call_finalize_partition(partition->id);
	/* Withdraw and free all pages we deposited */
	hv_call_withdraw_memory(U64_MAX, NUMA_NO_NODE, partition->id);

	hv_call_delete_partition(partition->id);

	kfree(partition);
}

static void
mshv_partition_put(struct mshv_partition *partition)
{
	if (refcount_dec_and_test(&partition->ref_count))
		destroy_partition(partition);
}

static int
mshv_partition_release(struct inode *inode, struct file *filp)
{
	struct mshv_partition *partition = filp->private_data;

	mshv_partition_put(partition);

	return 0;
}

static int
add_partition(struct mshv_partition *partition)
{
	unsigned long flags;
	int i, ret = 0;

	spin_lock_irqsave(&mshv.partitions.lock, flags);

	if (mshv.partitions.count >= MSHV_MAX_PARTITIONS) {
		pr_err("%s: too many partitions\n", __func__);
		ret = -ENOSPC;
		goto out_unlock;
	}

	for (i = 0; i < MSHV_MAX_PARTITIONS; ++i) {
		if (!mshv.partitions.array[i])
			break;
	}

	mshv.partitions.count++;
	mshv.partitions.array[i] = partition;

out_unlock:
	spin_unlock_irqrestore(&mshv.partitions.lock, flags);

	return ret;
}

static long
mshv_ioctl_create_partition(void __user *user_arg)
{
	struct mshv_create_partition args;
	struct mshv_partition *partition;
	struct file *file;
	int fd;
	long ret;

	if (copy_from_user(&args, user_arg, sizeof(args)))
		return -EFAULT;

	/* Only support EXO partitions */
	args.flags |= HV_PARTITION_CREATION_FLAG_EXO_PARTITION;

	partition = kzalloc(sizeof(*partition), GFP_KERNEL);
	if (!partition)
		return -ENOMEM;

	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0) {
		ret = fd;
		goto free_partition;
	}

	ret = hv_call_create_partition(args.flags,
				       args.partition_creation_properties,
				       &partition->id);
	if (ret)
		goto put_fd;

	ret = hv_call_initialize_partition(partition->id);
	if (ret)
		goto delete_partition;

	file = anon_inode_getfile("mshv_partition", &mshv_partition_fops,
				  partition, O_RDWR);
	if (IS_ERR(file)) {
		ret = PTR_ERR(file);
		goto finalize_partition;
	}
	refcount_set(&partition->ref_count, 1);

	ret = add_partition(partition);
	if (ret)
		goto release_file;

	fd_install(fd, file);

	return fd;

release_file:
	file->f_op->release(file->f_inode, file);
finalize_partition:
	hv_call_finalize_partition(partition->id);
delete_partition:
	hv_call_delete_partition(partition->id);
put_fd:
	put_unused_fd(fd);
free_partition:
	kfree(partition);
	return ret;
}

static long
mshv_ioctl_check_extension(void __user *user_arg)
{
	u32 arg;

	if (copy_from_user(&arg, user_arg, sizeof(arg)))
		return -EFAULT;

	switch (arg) {
	case MSHV_CAP_CORE_API_STABLE:
		return 0;
	}

	return -ENOTSUPP;
}

static long
mshv_dev_ioctl(struct file *filp, unsigned int ioctl, unsigned long arg)
{
	switch (ioctl) {
	case MSHV_CHECK_EXTENSION:
		return mshv_ioctl_check_extension((void __user *)arg);
	case MSHV_CREATE_PARTITION:
		return mshv_ioctl_create_partition((void __user *)arg);
	}

	return -ENOTTY;
}

static int
mshv_dev_open(struct inode *inode, struct file *filp)
{
	return 0;
}

static int
mshv_dev_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static int
__init mshv_init(void)
{
	int ret;

	ret = misc_register(&mshv_dev);
	if (ret)
		pr_err("%s: misc device register failed\n", __func__);

	spin_lock_init(&mshv.partitions.lock);

	return ret;
}

static void
__exit mshv_exit(void)
{
	misc_deregister(&mshv_dev);
}

module_init(mshv_init);
module_exit(mshv_exit);
