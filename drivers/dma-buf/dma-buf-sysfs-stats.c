// SPDX-License-Identifier: GPL-2.0-only
/*
 * DMA-BUF sysfs statistics.
 *
 * Copyright (C) 2021 Google LLC.
 */

#include <linux/dma-buf.h>
#include <linux/dma-resv.h>
#include <linux/freezer.h>
#include <linux/kobject.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/printk.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/sysfs.h>

#include "dma-buf-sysfs-stats.h"

struct dmabuf_kobj_work {
	struct list_head list;
	struct dma_buf_sysfs_entry *sysfs_entry;
	struct dma_buf_sysfs_entry_metadata *sysfs_metadata;
	unsigned long uid;
};

/* Both kobject setup and teardown work gets queued on the list. */
static LIST_HEAD(dmabuf_kobj_work_list);

/* dmabuf_kobj_list_lock protects dmabuf_kobj_work_list. */
static DEFINE_SPINLOCK(dmabuf_kobj_list_lock);

/*
 * dmabuf_sysfs_show_lock prevents a race between a DMA-BUF sysfs file being
 * read and the DMA-BUF being freed by protecting sysfs_entry->dmabuf.
 */
static DEFINE_SPINLOCK(dmabuf_sysfs_show_lock);

static struct task_struct *dmabuf_kobject_task;
static wait_queue_head_t dmabuf_kobject_waitqueue;

#define to_dma_buf_entry_from_kobj(x) container_of(x, struct dma_buf_sysfs_entry, kobj)

/**
 * DOC: overview
 *
 * ``/sys/kernel/debug/dma_buf/bufinfo`` provides an overview of every DMA-BUF
 * in the system. However, since debugfs is not safe to be mounted in
 * production, procfs and sysfs can be used to gather DMA-BUF statistics on
 * production systems.
 *
 * The ``/proc/<pid>/fdinfo/<fd>`` files in procfs can be used to gather
 * information about DMA-BUF fds. Detailed documentation about the interface
 * is present in Documentation/filesystems/proc.rst.
 *
 * Unfortunately, the existing procfs interfaces can only provide information
 * about the DMA-BUFs for which processes hold fds or have the buffers mmapped
 * into their address space. This necessitated the creation of the DMA-BUF sysfs
 * statistics interface to provide per-buffer information on production systems.
 *
 * The interface at ``/sys/kernel/dma-buf/buffers`` exposes information about
 * every DMA-BUF when ``CONFIG_DMABUF_SYSFS_STATS`` is enabled.
 *
 * The following stats are exposed by the interface:
 *
 * * ``/sys/kernel/dmabuf/buffers/<inode_number>/exporter_name``
 * * ``/sys/kernel/dmabuf/buffers/<inode_number>/size``
 *
 * The information in the interface can also be used to derive per-exporter
 * statistics. The data from the interface can be gathered on error conditions
 * or other important events to provide a snapshot of DMA-BUF usage.
 * It can also be collected periodically by telemetry to monitor various metrics.
 *
 * Detailed documentation about the interface is present in
 * Documentation/ABI/testing/sysfs-kernel-dmabuf-buffers.
 */

struct dma_buf_stats_attribute {
	struct attribute attr;
	ssize_t (*show)(struct dma_buf *dmabuf,
			struct dma_buf_stats_attribute *attr, char *buf);
};
#define to_dma_buf_stats_attr(x) container_of(x, struct dma_buf_stats_attribute, attr)

static ssize_t dma_buf_stats_attribute_show(struct kobject *kobj,
					    struct attribute *attr,
					    char *buf)
{
	struct dma_buf_stats_attribute *attribute;
	struct dma_buf_sysfs_entry *sysfs_entry;
	struct dma_buf *dmabuf;
	int ret;

	attribute = to_dma_buf_stats_attr(attr);
	sysfs_entry = to_dma_buf_entry_from_kobj(kobj);

	/*
	 * acquire dmabuf_sysfs_show_lock to prevent a race with the DMA-BUF
	 * being freed while sysfs_entry->dmabuf is being accessed.
	 */
	spin_lock(&dmabuf_sysfs_show_lock);
	dmabuf = sysfs_entry->dmabuf;

	if (!dmabuf || !attribute->show) {
		spin_unlock(&dmabuf_sysfs_show_lock);
		return -EIO;
	}

	ret = attribute->show(dmabuf, attribute, buf);
	spin_unlock(&dmabuf_sysfs_show_lock);
	return ret;
}

static const struct sysfs_ops dma_buf_stats_sysfs_ops = {
	.show = dma_buf_stats_attribute_show,
};

static ssize_t exporter_name_show(struct dma_buf *dmabuf,
				  struct dma_buf_stats_attribute *attr,
				  char *buf)
{
	return sysfs_emit(buf, "%s\n", dmabuf->exp_name);
}

static ssize_t size_show(struct dma_buf *dmabuf,
			 struct dma_buf_stats_attribute *attr,
			 char *buf)
{
	return sysfs_emit(buf, "%zu\n", dmabuf->size);
}

static struct dma_buf_stats_attribute exporter_name_attribute =
	__ATTR_RO(exporter_name);
static struct dma_buf_stats_attribute size_attribute = __ATTR_RO(size);

static struct attribute *dma_buf_stats_default_attrs[] = {
	&exporter_name_attribute.attr,
	&size_attribute.attr,
	NULL,
};
ATTRIBUTE_GROUPS(dma_buf_stats_default);

static void dma_buf_sysfs_release(struct kobject *kobj)
{
	struct dma_buf_sysfs_entry *sysfs_entry;

	sysfs_entry = to_dma_buf_entry_from_kobj(kobj);
	kfree(sysfs_entry);
}

static struct kobj_type dma_buf_ktype = {
	.sysfs_ops = &dma_buf_stats_sysfs_ops,
	.release = dma_buf_sysfs_release,
	.default_groups = dma_buf_stats_default_groups,
};

/* Statistics files do not need to send uevents. */
static int dmabuf_sysfs_uevent_filter(struct kset *kset, struct kobject *kobj)
{
	return 0;
}

static const struct kset_uevent_ops dmabuf_sysfs_no_uevent_ops = {
	.filter = dmabuf_sysfs_uevent_filter,
};

/* setup of sysfs entries done asynchronously in the worker thread. */
static void dma_buf_sysfs_stats_setup_work(struct dmabuf_kobj_work *kobject_work)
{
	struct dma_buf_sysfs_entry *sysfs_entry = kobject_work->sysfs_entry;
	struct dma_buf_sysfs_entry_metadata *sysfs_metadata =
			kobject_work->sysfs_metadata;
	bool free_metadata = false;

	int ret = kobject_init_and_add(&sysfs_entry->kobj, &dma_buf_ktype, NULL,
				       "%lu", kobject_work->uid);
	if (ret) {
		kobject_put(&sysfs_entry->kobj);

		spin_lock(&sysfs_metadata->sysfs_entry_lock);
		if (sysfs_metadata->status == SYSFS_ENTRY_INIT_ABORTED) {
			/*
			 * SYSFS_ENTRY_INIT_ABORTED means that the DMA-BUF has already
			 * been freed. At this point, its safe to free the memory for
			 * the sysfs_metadata;
			 */
			free_metadata = true;
		} else {
			/*
			 * The DMA-BUF has not yet been freed, set the status to
			 * sysfs_entry_error so that when the DMA-BUF gets
			 * freed, we know there is no need to teardown the sysfs
			 * entry.
			 */
			sysfs_metadata->status = SYSFS_ENTRY_ERROR;
		}
		goto unlock;
	}

	/*
	 * If the DMA-BUF has not yet been released, status would still be
	 * SYSFS_ENTRY_INIT_IN_PROGRESS. We set the status as initialized.
	 */
	spin_lock(&sysfs_metadata->sysfs_entry_lock);
	if (sysfs_metadata->status == SYSFS_ENTRY_INIT_IN_PROGRESS) {
		sysfs_metadata->status = SYSFS_ENTRY_INITIALIZED;
		goto unlock;
	}

	/*
	 * At this point the status is SYSFS_ENTRY_INIT_ABORTED which means
	 * that the DMA-BUF has already been freed. Hence, we cleanup the
	 * sysfs_entry and its metadata since neither of them are needed
	 * anymore.
	 */
	free_metadata = true;
	kobject_del(&sysfs_entry->kobj);
	kobject_put(&sysfs_entry->kobj);

unlock:
	spin_unlock(&sysfs_metadata->sysfs_entry_lock);
	if (free_metadata) {
		kfree(kobject_work->sysfs_metadata);
		kobject_work->sysfs_metadata = NULL;
	}
}

/* teardown of sysfs entries done asynchronously in the worker thread. */
static void dma_buf_sysfs_stats_teardown_work(struct dmabuf_kobj_work *kobject_work)
{
	struct dma_buf_sysfs_entry *sysfs_entry = kobject_work->sysfs_entry;

	kobject_del(&sysfs_entry->kobj);
	kobject_put(&sysfs_entry->kobj);

	kfree(kobject_work->sysfs_metadata);
	kobject_work->sysfs_metadata = NULL;
}

/* do setup or teardown of sysfs entries as required */
static void do_kobject_work(struct dmabuf_kobj_work *kobject_work)
{
	struct dma_buf_sysfs_entry_metadata *sysfs_metadata;
	bool setup_needed = false;
	bool teardown_needed = false;

	sysfs_metadata = kobject_work->sysfs_metadata;
	spin_lock(&sysfs_metadata->sysfs_entry_lock);
	if (sysfs_metadata->status == SYSFS_ENTRY_UNINITIALIZED) {
		setup_needed = true;
		sysfs_metadata->status = SYSFS_ENTRY_INIT_IN_PROGRESS;
	} else if (sysfs_metadata->status == SYSFS_ENTRY_INITIALIZED) {
		teardown_needed = true;
	}

	/*
	 * It is ok to release the sysfs_entry_lock here.
	 *
	 * If setup_needed is true, we check the status again after the kobject
	 * initialization to see if it has been set to SYSFS_ENTRY_INIT_ABORTED
	 * and if so teardown the kobject.
	 *
	 * If teardown_needed is true, there are no more changes expected to the
	 * status.
	 *
	 * If neither setup_needed nor teardown needed are true, it
	 * means the DMA-BUF was freed before we got around to setting up the
	 * sysfs entry and hence we just need to release the metadata and
	 * return.
	 */
	spin_unlock(&kobject_work->sysfs_metadata->sysfs_entry_lock);

	if (setup_needed)
		dma_buf_sysfs_stats_setup_work(kobject_work);
	else if (teardown_needed)
		dma_buf_sysfs_stats_teardown_work(kobject_work);
	else
		kfree(kobject_work->sysfs_metadata);

	kfree(kobject_work);
}

static struct dmabuf_kobj_work *get_next_kobj_work(void)
{
	struct dmabuf_kobj_work *kobject_work;

	spin_lock(&dmabuf_kobj_list_lock);
	kobject_work = list_first_entry_or_null(&dmabuf_kobj_work_list,
						struct dmabuf_kobj_work, list);
	if (kobject_work)
		list_del(&kobject_work->list);
	spin_unlock(&dmabuf_kobj_list_lock);
	return kobject_work;
}

static int kobject_work_thread(void *data)
{
	struct dmabuf_kobj_work *kobject_work;

	while (1) {
		wait_event_freezable(dmabuf_kobject_waitqueue,
				     (kobject_work = get_next_kobj_work()));
		do_kobject_work(kobject_work);
	}

	return 0;
}

static int kobject_worklist_init(void)
{
	init_waitqueue_head(&dmabuf_kobject_waitqueue);
	dmabuf_kobject_task = kthread_run(kobject_work_thread, NULL,
					  "%s", "dmabuf-kobject-worker");
	if (IS_ERR(dmabuf_kobject_task)) {
		pr_err("Creating thread for deferred sysfs entry creation/deletion failed\n");
		return PTR_ERR(dmabuf_kobject_task);
	}
	sched_set_normal(dmabuf_kobject_task, MAX_NICE);

	return 0;
}

static void deferred_kobject_create(struct dmabuf_kobj_work *kobject_work)
{
	INIT_LIST_HEAD(&kobject_work->list);

	spin_lock(&dmabuf_kobj_list_lock);

	list_add_tail(&kobject_work->list, &dmabuf_kobj_work_list);

	spin_unlock(&dmabuf_kobj_list_lock);

	wake_up(&dmabuf_kobject_waitqueue);
}


void dma_buf_stats_teardown(struct dma_buf *dmabuf)
{
	struct dma_buf_sysfs_entry *sysfs_entry;
	struct dma_buf_sysfs_entry_metadata *sysfs_metadata;
	struct dmabuf_kobj_work *kobj_work;

	sysfs_entry = dmabuf->sysfs_entry;
	if (!sysfs_entry)
		return;

	sysfs_metadata = dmabuf->sysfs_entry_metadata;
	if (!sysfs_metadata)
		return;

	spin_lock(&sysfs_metadata->sysfs_entry_lock);

	if (sysfs_metadata->status == SYSFS_ENTRY_UNINITIALIZED ||
	    sysfs_metadata->status == SYSFS_ENTRY_INIT_IN_PROGRESS) {
		/*
		 * The sysfs entry for this buffer has not yet been initialized,
		 * we set the status to SYSFS_ENTRY_INIT_ABORTED to abort the
		 * initialization.
		 */
		sysfs_metadata->status = SYSFS_ENTRY_INIT_ABORTED;
		spin_unlock(&sysfs_metadata->sysfs_entry_lock);

		/*
		 * In case kobject initialization completes right as we release
		 * the sysfs_entry_lock, disable show() for the sysfs entry by
		 * setting sysfs_entry->dmabuf to NULL to prevent a race.
		 */
		spin_lock(&dmabuf_sysfs_show_lock);
		sysfs_entry->dmabuf = NULL;
		spin_unlock(&dmabuf_sysfs_show_lock);

		return;
	}

	if (sysfs_metadata->status == SYSFS_ENTRY_INITIALIZED) {
		/*
		 * queue teardown work only if sysfs_entry is fully inititalized.
		 * It is ok to release the sysfs_entry_lock here since the
		 * status can no longer change.
		 */
		spin_unlock(&sysfs_metadata->sysfs_entry_lock);

		/*
		 * Meanwhile disable show() for the sysfs entry to avoid a race
		 * between teardown and show().
		 */
		spin_lock(&dmabuf_sysfs_show_lock);
		sysfs_entry->dmabuf = NULL;
		spin_unlock(&dmabuf_sysfs_show_lock);

		kobj_work = kzalloc(sizeof(struct dmabuf_kobj_work), GFP_KERNEL);
		if (!kobj_work) {
			/* do the teardown immediately. */
			kobject_del(&sysfs_entry->kobj);
			kobject_put(&sysfs_entry->kobj);
			kfree(sysfs_metadata);
		} else {
			/* queue teardown work. */
			kobj_work->sysfs_entry = dmabuf->sysfs_entry;
			kobj_work->sysfs_metadata = dmabuf->sysfs_entry_metadata;
			deferred_kobject_create(kobj_work);
		}

		return;
	}

	/*
	 * status is SYSFS_ENTRY_INIT_ERROR so we only need to free the
	 * metadata.
	 */
	spin_unlock(&sysfs_metadata->sysfs_entry_lock);
	kfree(dmabuf->sysfs_entry_metadata);
	dmabuf->sysfs_entry_metadata = NULL;
}

static struct kset *dma_buf_stats_kset;
static struct kset *dma_buf_per_buffer_stats_kset;
int dma_buf_init_sysfs_statistics(void)
{
	int ret;

	ret = kobject_worklist_init();
	if (ret)
		return ret;

	dma_buf_stats_kset = kset_create_and_add("dmabuf",
						 &dmabuf_sysfs_no_uevent_ops,
						 kernel_kobj);
	if (!dma_buf_stats_kset)
		return -ENOMEM;

	dma_buf_per_buffer_stats_kset = kset_create_and_add("buffers",
							    &dmabuf_sysfs_no_uevent_ops,
							    &dma_buf_stats_kset->kobj);
	if (!dma_buf_per_buffer_stats_kset) {
		kset_unregister(dma_buf_stats_kset);
		return -ENOMEM;
	}

	return 0;
}

void dma_buf_uninit_sysfs_statistics(void)
{
	kset_unregister(dma_buf_per_buffer_stats_kset);
	kset_unregister(dma_buf_stats_kset);
}

int dma_buf_stats_setup(struct dma_buf *dmabuf)
{
	struct dma_buf_sysfs_entry *sysfs_entry;
	struct dma_buf_sysfs_entry_metadata *sysfs_metadata;
	struct dmabuf_kobj_work *kobj_work;

	if (!dmabuf || !dmabuf->file)
		return -EINVAL;

	if (!dmabuf->exp_name) {
		pr_err("exporter name must not be empty if stats needed\n");
		return -EINVAL;
	}

	sysfs_entry = kzalloc(sizeof(struct dma_buf_sysfs_entry), GFP_KERNEL);
	if (!sysfs_entry)
		return -ENOMEM;

	sysfs_entry->kobj.kset = dma_buf_per_buffer_stats_kset;
	sysfs_entry->dmabuf = dmabuf;

	sysfs_metadata = kzalloc(sizeof(struct dma_buf_sysfs_entry_metadata),
				 GFP_KERNEL);
	if (!sysfs_metadata) {
		kfree(sysfs_entry);
		return -ENOMEM;
	}

	dmabuf->sysfs_entry = sysfs_entry;

	sysfs_metadata->status = SYSFS_ENTRY_UNINITIALIZED;
	spin_lock_init(&sysfs_metadata->sysfs_entry_lock);

	dmabuf->sysfs_entry_metadata = sysfs_metadata;

	kobj_work = kzalloc(sizeof(struct dmabuf_kobj_work), GFP_KERNEL);
	if (!kobj_work) {
		kfree(sysfs_entry);
		kfree(sysfs_metadata);
		return -ENOMEM;
	}

	kobj_work->sysfs_entry = dmabuf->sysfs_entry;
	kobj_work->sysfs_metadata = dmabuf->sysfs_entry_metadata;
	/*
	 * stash the inode number in struct dmabuf_kobj_work since setup
	 * might race with DMA-BUF teardown.
	 */
	kobj_work->uid = file_inode(dmabuf->file)->i_ino;

	deferred_kobject_create(kobj_work);
	return 0;
}
