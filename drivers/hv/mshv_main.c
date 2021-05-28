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
#include <linux/io.h>
#include <linux/cpuhotplug.h>
#include <linux/random.h>
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
mshv_vp_ioctl_run_vp(struct mshv_vp *vp, void __user *ret_message)
{
	long ret;
	u32 msg_type;
	struct hv_register_assoc suspend_registers[2] = {
		{ .name = HV_REGISTER_EXPLICIT_SUSPEND },
		{ .name = HV_REGISTER_INTERCEPT_SUSPEND }
	};
	/* Pointers to values for convenience */
	union hv_explicit_suspend_register *explicit_suspend =
				&suspend_registers[0].value.explicit_suspend;
	union hv_intercept_suspend_register *intercept_suspend =
				&suspend_registers[1].value.intercept_suspend;

	/* Check that the VP is suspended */
	ret = hv_call_get_vp_registers(
			vp->index,
			vp->partition->id,
			2,
			suspend_registers);
	if (ret)
		return ret;

	if (!explicit_suspend->suspended &&
	    !intercept_suspend->suspended) {
		pr_err("%s: vp not suspended!\n", __func__);
		return -EBADFD;
	}

	/*
	 * If intercept_suspend is set, we missed a message and need to
	 * wait for mshv_isr to complete
	 */
	if (intercept_suspend->suspended) {
		if (down_interruptible(&vp->run.sem))
			return -EINTR;
		if (copy_to_user(ret_message, vp->run.intercept_message,
				 sizeof(struct hv_message)))
			return -EFAULT;
		return 0;
	}

	/*
	 * At this point the semaphore ensures that mshv_isr is done,
	 * and the mutex ensures that no other threads are touching this vp
	 */
	vp->run.task = current;
	set_current_state(TASK_INTERRUPTIBLE);

	/* Now actually start the vp running */
	explicit_suspend->suspended = 0;
	intercept_suspend->suspended = 0;
	ret = hv_call_set_vp_registers(
			vp->index,
			vp->partition->id,
			2,
			suspend_registers);
	if (ret) {
		pr_err("%s: failed to clear suspend bits\n", __func__);
		set_current_state(TASK_RUNNING);
		vp->run.task = NULL;
		return ret;
	}

	schedule();

	/* Explicitly suspend the vp to make sure it's stopped */
	explicit_suspend->suspended = 1;
	ret = hv_call_set_vp_registers(
		vp->index,
		vp->partition->id,
		1,
		&suspend_registers[0]);
	if (ret) {
		pr_err("%s: failed to set explicit suspend bit\n", __func__);
		return -EBADFD;
	}

	/*
	 * Check if woken up by a signal
	 * Note that if the signal came after being woken by mshv_isr(),
	 * we will still get the message correctly on re-entry
	 */
	if (signal_pending(current)) {
		pr_debug("%s: woke up, received signal\n", __func__);
		return -EINTR;
	}

	/*
	 * No signal pending, so we were woken by hv_host_isr()
	 * The isr can't be running now, and the intercept_suspend bit is set
	 * We use it as a flag to tell if we missed a message due to a signal,
	 * so we must clear it here and reset the semaphore
	 */
	intercept_suspend->suspended = 0;
	ret = hv_call_set_vp_registers(
		vp->index,
		vp->partition->id,
		1,
		&suspend_registers[1]);
	if (ret) {
		pr_err("%s: failed to clear intercept suspend bit\n", __func__);
		return -EBADFD;
	}
	if (down_trylock(&vp->run.sem)) {
		pr_err("%s: semaphore in unexpected state\n", __func__);
		return -EBADFD;
	}

	msg_type = vp->run.intercept_message->header.message_type;

	if (msg_type == HVMSG_NONE) {
		pr_err("%s: woke up, but no message\n", __func__);
		return -ENOMSG;
	}

	if (copy_to_user(ret_message, vp->run.intercept_message,
			 sizeof(struct hv_message)))
		return -EFAULT;

	return 0;
}

static long
mshv_vp_ioctl_get_regs(struct mshv_vp *vp, void __user *user_args)
{
	struct mshv_vp_registers args;
	struct hv_register_assoc *registers;
	long ret;

	if (copy_from_user(&args, user_args, sizeof(args)))
		return -EFAULT;

	if (args.count > MSHV_VP_MAX_REGISTERS)
		return -EINVAL;

	registers = kmalloc_array(args.count,
				  sizeof(*registers),
				  GFP_KERNEL);
	if (!registers)
		return -ENOMEM;

	if (copy_from_user(registers, args.regs,
			   sizeof(*registers) * args.count)) {
		ret = -EFAULT;
		goto free_return;
	}

	ret = hv_call_get_vp_registers(vp->index, vp->partition->id,
				       args.count, registers);
	if (ret)
		goto free_return;

	if (copy_to_user(args.regs, registers,
			 sizeof(*registers) * args.count)) {
		ret = -EFAULT;
	}

free_return:
	kfree(registers);
	return ret;
}

static long
mshv_vp_ioctl_set_regs(struct mshv_vp *vp, void __user *user_args)
{
	struct mshv_vp_registers args;
	struct hv_register_assoc *registers;
	long ret;
	int i;

	if (copy_from_user(&args, user_args, sizeof(args)))
		return -EFAULT;

	if (args.count > MSHV_VP_MAX_REGISTERS)
		return -EINVAL;

	registers = kmalloc_array(args.count,
				  sizeof(*registers),
				  GFP_KERNEL);
	if (!registers) {
		return -ENOMEM;
	}

	if (copy_from_user(registers, args.regs,
			   sizeof(*registers) * args.count)) {
		ret = -EFAULT;
		goto free_return;
	}

	for (i = 0; i < args.count; i++) {
		/*
		 * Disallow setting suspend registers to ensure run vp state
		 * is consistent
		 */
		if (registers[i].name == HV_REGISTER_EXPLICIT_SUSPEND ||
		    registers[i].name == HV_REGISTER_INTERCEPT_SUSPEND) {
			pr_err("%s: not allowed to set suspend registers\n",
			       __func__);
			ret = -EINVAL;
			goto free_return;
		}
	}

	ret = hv_call_set_vp_registers(vp->index, vp->partition->id,
				       args.count, registers);

free_return:
	kfree(registers);
	return ret;
}

static long
mshv_vp_ioctl_get_set_state_pfn(struct mshv_vp *vp,
				struct mshv_vp_state *args,
				bool is_set)
{
	u64 page_count, remaining;
	int completed;
	struct page **pages;
	long ret;
	unsigned long u_buf;

	/* Buffer must be page aligned */
	if (!PAGE_ALIGNED(args->buf_size) ||
	    !PAGE_ALIGNED(args->buf.bytes))
		return -EINVAL;

	if (!access_ok(args->buf.bytes, args->buf_size))
		return -EFAULT;

	/* Pin user pages so hypervisor can copy directly to them */
	page_count = args->buf_size >> HV_HYP_PAGE_SHIFT;
	pages = kcalloc(page_count, sizeof(struct page *), GFP_KERNEL);
	if (!pages)
		return -ENOMEM;

	remaining = page_count;
	u_buf = (unsigned long)args->buf.bytes;
	while (remaining) {
		completed = pin_user_pages_fast(
				u_buf,
				remaining,
				FOLL_WRITE,
				&pages[page_count - remaining]);
		if (completed < 0) {
			pr_err("%s: failed to pin user pages error %i\n",
			       __func__, completed);
			ret = completed;
			goto unpin_pages;
		}
		remaining -= completed;
		u_buf += completed * HV_HYP_PAGE_SIZE;
	}

	if (is_set)
		ret = hv_call_set_vp_state(vp->index,
					   vp->partition->id,
					   args->type, args->xsave,
					   page_count, pages,
					   0, NULL);
	else
		ret = hv_call_get_vp_state(vp->index,
					   vp->partition->id,
					   args->type, args->xsave,
					   page_count, pages,
					   NULL);

unpin_pages:
	unpin_user_pages(pages, page_count - remaining);
	kfree(pages);
	return ret;
}

static long
mshv_vp_ioctl_get_set_state(struct mshv_vp *vp, void __user *user_args, bool is_set)
{
	struct mshv_vp_state args;
	long ret = 0;
	union hv_get_vp_state_out vp_state;

	if (copy_from_user(&args, user_args, sizeof(args)))
		return -EFAULT;

	/* For now just support these */
	if (args.type != HV_GET_SET_VP_STATE_LOCAL_INTERRUPT_CONTROLLER_STATE &&
	    args.type != HV_GET_SET_VP_STATE_XSAVE)
		return -EINVAL;

	/* If we need to pin pfns, delegate to helper */
	if (args.type & HV_GET_SET_VP_STATE_TYPE_PFN)
		return mshv_vp_ioctl_get_set_state_pfn(vp, &args, is_set);

	if (args.buf_size < sizeof(vp_state))
		return -EINVAL;

	if (is_set) {
		if (copy_from_user(
				&vp_state,
				args.buf.lapic,
				sizeof(vp_state)))
			return -EFAULT;

		return hv_call_set_vp_state(vp->index,
					    vp->partition->id,
					    args.type, args.xsave,
					    0, NULL,
					    sizeof(vp_state),
					    (u8 *)&vp_state);
	}

	ret = hv_call_get_vp_state(vp->index,
				   vp->partition->id,
				   args.type, args.xsave,
				   0, NULL,
				   &vp_state);

	if (ret)
		return ret;

	if (copy_to_user(args.buf.lapic,
			 &vp_state.interrupt_controller_state,
			 sizeof(vp_state.interrupt_controller_state)))
		return -EFAULT;

	return 0;
}

static long
mshv_vp_ioctl(struct file *filp, unsigned int ioctl, unsigned long arg)
{
	struct mshv_vp *vp = filp->private_data;
	long r = 0;

	if (mutex_lock_killable(&vp->mutex))
		return -EINTR;

	switch (ioctl) {
	case MSHV_RUN_VP:
		r = mshv_vp_ioctl_run_vp(vp, (void __user *)arg);
		break;
	case MSHV_GET_VP_REGISTERS:
		r = mshv_vp_ioctl_get_regs(vp, (void __user *)arg);
		break;
	case MSHV_SET_VP_REGISTERS:
		r = mshv_vp_ioctl_set_regs(vp, (void __user *)arg);
		break;
	case MSHV_GET_VP_STATE:
		r = mshv_vp_ioctl_get_set_state(vp, (void __user *)arg, false);
		break;
	case MSHV_SET_VP_STATE:
		r = mshv_vp_ioctl_get_set_state(vp, (void __user *)arg, true);
		break;
	default:
		r = -ENOTTY;
		break;
	}
	mutex_unlock(&vp->mutex);

	return r;
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

	mutex_init(&vp->mutex);
	sema_init(&vp->run.sem, 0);

	vp->run.intercept_message =
		(struct hv_message *)get_zeroed_page(GFP_KERNEL);
	if (!vp->run.intercept_message) {
		ret = -ENOMEM;
		goto free_vp;
	}

	vp->index = args.vp_index;
	vp->partition = mshv_partition_get(partition);
	if (!vp->partition) {
		ret = -EBADF;
		goto free_message;
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
free_message:
	free_page((unsigned long)vp->run.intercept_message);
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
mshv_partition_ioctl_install_intercept(struct mshv_partition *partition,
				       void __user *user_args)
{
	struct mshv_install_intercept args;

	if (copy_from_user(&args, user_args, sizeof(args)))
		return -EFAULT;

	return hv_call_install_intercept(
			partition->id,
			args.access_type_mask,
			args.intercept_type,
			args.intercept_parameter);
}

static long
mshv_partition_ioctl_assert_interrupt(struct mshv_partition *partition,
				      void __user *user_args)
{
	struct mshv_assert_interrupt args;

	if (copy_from_user(&args, user_args, sizeof(args)))
		return -EFAULT;

	return hv_call_assert_virtual_interrupt(
			partition->id,
			args.vector,
			args.dest_addr,
			args.control);
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
	case MSHV_INSTALL_INTERCEPT:
		ret = mshv_partition_ioctl_install_intercept(partition,
							(void __user *)arg);
		break;
	case MSHV_ASSERT_INTERRUPT:
		ret = mshv_partition_ioctl_assert_interrupt(partition,
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

	if (!mshv.partitions.count)
		hv_remove_mshv_irq();

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
		free_page((unsigned long)vp->run.intercept_message);
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

	if (mshv.partitions.count == 1)
		hv_setup_mshv_irq(mshv_isr);

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

static int mshv_cpuhp_online;

static int
__init mshv_init(void)
{
	int ret;

	ret = misc_register(&mshv_dev);
	if (ret) {
		pr_err("%s: misc device register failed\n", __func__);
		return ret;
	}

	mshv.synic_message_page = alloc_percpu(struct hv_message_page *);
	if (!mshv.synic_message_page) {
		pr_err("%s: failed to allocate percpu synic page\n", __func__);
		misc_deregister(&mshv_dev);
		return -ENOMEM;
	}

	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "mshv_synic",
				mshv_synic_init,
				mshv_synic_cleanup);
	if (ret < 0) {
		pr_err("%s: failed to setup cpu hotplug state: %i\n",
		       __func__, ret);
		return ret;
	}

	mshv_cpuhp_online = ret;
	spin_lock_init(&mshv.partitions.lock);

	return 0;
}

static void
__exit mshv_exit(void)
{
	cpuhp_remove_state(mshv_cpuhp_online);
	free_percpu(mshv.synic_message_page);

	misc_deregister(&mshv_dev);
}

module_init(mshv_init);
module_exit(mshv_exit);
