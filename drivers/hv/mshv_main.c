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

static int mshv_vp_release(struct inode *inode, struct file *filp);
static long mshv_vp_ioctl(struct file *filp, unsigned int ioctl, unsigned long arg);
static struct mshv_partition *mshv_partition_get(struct mshv_partition *partition);
static void mshv_partition_put(struct mshv_partition *partition);
static int mshv_partition_release(struct inode *inode, struct file *filp);
static long mshv_partition_ioctl(struct file *filp, unsigned int ioctl, unsigned long arg);
static int mshv_dev_open(struct inode *inode, struct file *filp);
static int mshv_dev_release(struct inode *inode, struct file *filp);
static long mshv_dev_ioctl(struct file *filp, unsigned int ioctl, unsigned long arg);

static const struct file_operations mshv_vp_fops = {
	.release = mshv_vp_release,
	.unlocked_ioctl = mshv_vp_ioctl,
	.llseek = noop_llseek,
};

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
mshv_vp_ioctl(struct file *filp, unsigned int ioctl, unsigned long arg)
{
	return -ENOTTY;
}

static int
mshv_vp_release(struct inode *inode, struct file *filp)
{
	struct mshv_vp *vp = filp->private_data;
	mshv_partition_put(vp->partition);

	/* Rest of VP cleanup happens in destroy_partition() */
	return 0;
}

static long
mshv_partition_ioctl_create_vp(struct mshv_partition *partition,
			       void __user *arg)
{
	struct mshv_create_vp args;
	struct mshv_vp *vp;
	struct file *file;
	int fd;
	long ret;

	if (copy_from_user(&args, arg, sizeof(args)))
		return -EFAULT;

	if (args.vp_index >= MSHV_MAX_VPS)
		return -EINVAL;

	if (partition->vps.array[args.vp_index])
		return -EEXIST;

	vp = kzalloc(sizeof(*vp), GFP_KERNEL);

	if (!vp)
		return -ENOMEM;

	vp->index = args.vp_index;
	vp->partition = mshv_partition_get(partition);
	if (!vp->partition) {
		ret = -EBADF;
		goto free_vp;
	}

	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0) {
		ret = fd;
		goto put_partition;
	}

	file = anon_inode_getfile("mshv_vp", &mshv_vp_fops, vp, O_RDWR);
	if (IS_ERR(file)) {
		ret = PTR_ERR(file);
		goto put_fd;
	}

	ret = hv_call_create_vp(
			NUMA_NO_NODE,
			partition->id,
			args.vp_index,
			0 /* Only valid for root partition VPs */
			);
	if (ret)
		goto release_file;

	/* already exclusive with the partition mutex for all ioctls */
	partition->vps.count++;
	partition->vps.array[args.vp_index] = vp;

	fd_install(fd, file);

	return fd;

release_file:
	file->f_op->release(file->f_inode, file);
put_fd:
	put_unused_fd(fd);
put_partition:
	mshv_partition_put(partition);
free_vp:
	kfree(vp);

	return ret;
}

static long
mshv_partition_ioctl_map_memory(struct mshv_partition *partition,
				struct mshv_user_mem_region __user *user_mem)
{
	struct mshv_user_mem_region mem;
	struct mshv_mem_region *region;
	int completed;
	unsigned long remaining, batch_size;
	int i;
	struct page **pages;
	u64 page_count, user_start, user_end, gpfn_start, gpfn_end;
	u64 region_page_count, region_user_start, region_user_end;
	u64 region_gpfn_start, region_gpfn_end;
	long ret = 0;

	/* Check we have enough slots*/
	if (partition->regions.count == MSHV_MAX_MEM_REGIONS) {
		pr_err("%s: not enough memory region slots\n", __func__);
		return -ENOSPC;
	}

	if (copy_from_user(&mem, user_mem, sizeof(mem)))
		return -EFAULT;

	if (!mem.size ||
	    !PAGE_ALIGNED(mem.size) ||
	    !PAGE_ALIGNED(mem.userspace_addr) ||
	    !access_ok(mem.userspace_addr, mem.size))
		return -EINVAL;

	/* Reject overlapping regions */
	page_count = mem.size >> HV_HYP_PAGE_SHIFT;
	user_start = mem.userspace_addr;
	user_end = mem.userspace_addr + mem.size;
	gpfn_start = mem.guest_pfn;
	gpfn_end = mem.guest_pfn + page_count;
	for (i = 0; i < MSHV_MAX_MEM_REGIONS; ++i) {
		region = &partition->regions.slots[i];
		if (!region->size)
			continue;
		region_page_count = region->size >> HV_HYP_PAGE_SHIFT;
		region_user_start = region->userspace_addr;
		region_user_end = region->userspace_addr + region->size;
		region_gpfn_start = region->guest_pfn;
		region_gpfn_end = region->guest_pfn + region_page_count;

		if (!(user_end <= region_user_start) &&
		    !(region_user_end <= user_start)) {
			return -EEXIST;
		}
		if (!(gpfn_end <= region_gpfn_start) &&
		    !(region_gpfn_end <= gpfn_start)) {
			return -EEXIST;
		}
	}

	/* Pin the userspace pages */
	pages = vzalloc(sizeof(struct page *) * page_count);
	if (!pages)
		return -ENOMEM;

	remaining = page_count;
	while (remaining) {
		/*
		 * We need to batch this, as pin_user_pages_fast with the
		 * FOLL_LONGTERM flag does a big temporary allocation
		 * of contiguous memory
		 */
		batch_size = min(remaining, PIN_PAGES_BATCH_SIZE);
		completed = pin_user_pages_fast(
				mem.userspace_addr + (page_count - remaining) * HV_HYP_PAGE_SIZE,
				batch_size,
				FOLL_WRITE | FOLL_LONGTERM,
				&pages[page_count - remaining]);
		if (completed < 0) {
			pr_err("%s: failed to pin user pages error %i\n",
			       __func__,
			       completed);
			ret = completed;
			goto err_unpin_pages;
		}
		remaining -= completed;
	}

	/* Map the pages to GPA pages */
	ret = hv_call_map_gpa_pages(partition->id, mem.guest_pfn,
				    page_count, mem.flags, pages);
	if (ret)
		goto err_unpin_pages;

	/* Install the new region */
	for (i = 0; i < MSHV_MAX_MEM_REGIONS; ++i) {
		if (!partition->regions.slots[i].size) {
			region = &partition->regions.slots[i];
			break;
		}
	}
	region->pages = pages;
	region->size = mem.size;
	region->guest_pfn = mem.guest_pfn;
	region->userspace_addr = mem.userspace_addr;

	partition->regions.count++;

	return 0;

err_unpin_pages:
	unpin_user_pages(pages, page_count - remaining);
	vfree(pages);

	return ret;
}

static long
mshv_partition_ioctl_unmap_memory(struct mshv_partition *partition,
				  struct mshv_user_mem_region __user *user_mem)
{
	struct mshv_user_mem_region mem;
	struct mshv_mem_region *region_ptr;
	int i;
	u64 page_count;
	long ret;

	if (!partition->regions.count)
		return -EINVAL;

	if (copy_from_user(&mem, user_mem, sizeof(mem)))
		return -EFAULT;

	/* Find matching region */
	for (i = 0; i < MSHV_MAX_MEM_REGIONS; ++i) {
		if (!partition->regions.slots[i].size)
			continue;
		region_ptr = &partition->regions.slots[i];
		if (region_ptr->userspace_addr == mem.userspace_addr &&
		    region_ptr->size == mem.size &&
		    region_ptr->guest_pfn == mem.guest_pfn)
			break;
	}

	if (i == MSHV_MAX_MEM_REGIONS)
		return -EINVAL;

	page_count = region_ptr->size >> HV_HYP_PAGE_SHIFT;
	ret = hv_call_unmap_gpa_pages(partition->id, region_ptr->guest_pfn,
				      page_count, 0);
	if (ret)
		return ret;

	unpin_user_pages(region_ptr->pages, page_count);
	vfree(region_ptr->pages);
	memset(region_ptr, 0, sizeof(*region_ptr));
	partition->regions.count--;

	return 0;
}

static long
mshv_partition_ioctl(struct file *filp, unsigned int ioctl, unsigned long arg)
{
	struct mshv_partition *partition = filp->private_data;
	long ret;

	if (mutex_lock_killable(&partition->mutex))
		return -EINTR;

	switch (ioctl) {
	case MSHV_MAP_GUEST_MEMORY:
		ret = mshv_partition_ioctl_map_memory(partition,
							(void __user *)arg);
		break;
	case MSHV_UNMAP_GUEST_MEMORY:
		ret = mshv_partition_ioctl_unmap_memory(partition,
							(void __user *)arg);
		break;
	case MSHV_CREATE_VP:
		ret = mshv_partition_ioctl_create_vp(partition,
							(void __user *)arg);
		break;
	default:
		ret = -ENOTTY;
	}

	mutex_unlock(&partition->mutex);
	return ret;
}

static void
destroy_partition(struct mshv_partition *partition)
{
	unsigned long flags, page_count;
	struct mshv_vp *vp;
	struct mshv_mem_region *region;
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
	
	/* Remove vps */
	for (i = 0; i < MSHV_MAX_VPS; ++i) {
		vp = partition->vps.array[i];
		if (!vp)
			continue;
		kfree(vp);
	}

	/* Remove regions and unpin the pages */
	for (i = 0; i < MSHV_MAX_MEM_REGIONS; ++i) {
		region = &partition->regions.slots[i];
		if (!region->size)
			continue;
		page_count = region->size >> HV_HYP_PAGE_SHIFT;
		unpin_user_pages(region->pages, page_count);
		vfree(region->pages);
	}

	kfree(partition);
}

static struct
mshv_partition *mshv_partition_get(struct mshv_partition *partition)
{
	if (refcount_inc_not_zero(&partition->ref_count))
		return partition;
	return NULL;
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

	mutex_init(&partition->mutex);

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
