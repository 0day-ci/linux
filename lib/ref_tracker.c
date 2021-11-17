// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/export.h>
#include <linux/ref_tracker.h>
#include <linux/slab.h>
#include <linux/stacktrace.h>

#define REF_TRACKER_STACK_ENTRIES 16

void ref_tracker_dir_exit(struct ref_tracker_dir *dir)
{
	struct ref_tracker *tracker, *n;
	unsigned long flags;

	spin_lock_irqsave(&dir->lock, flags);
	list_for_each_entry_safe(tracker, n, &dir->quarantine, head) {
		list_del(&tracker->head);
		kfree(tracker);
		dir->quarantine_avail++;
	}
	list_for_each_entry_safe(tracker, n, &dir->list, head) {
		pr_err("leaked reference.\n");
		if (tracker->alloc_stack_handle)
			stack_depot_print(tracker->alloc_stack_handle);
		list_del(&tracker->head);
		kfree(tracker);
	}
	spin_unlock_irqrestore(&dir->lock, flags);
}
EXPORT_SYMBOL(ref_tracker_dir_exit);

void ref_tracker_dir_print(struct ref_tracker_dir *dir,
			   unsigned int display_limit)
{
	struct ref_tracker *tracker;
	unsigned long flags;
	unsigned int i = 0;

	spin_lock_irqsave(&dir->lock, flags);
	list_for_each_entry(tracker, &dir->list, head) {
		tracker->dead = true;
		if (i < display_limit) {
			pr_err("leaked reference.\n");
			if (tracker->alloc_stack_handle)
				stack_depot_print(tracker->alloc_stack_handle);
		}
		i++;
	}
	spin_unlock_irqrestore(&dir->lock, flags);
}
EXPORT_SYMBOL(ref_tracker_dir_print);

int ref_tracker_alloc(struct ref_tracker_dir *dir,
		      struct ref_tracker **trackerp,
		      gfp_t gfp)
{
	unsigned long entries[REF_TRACKER_STACK_ENTRIES];
	struct ref_tracker *tracker;
	unsigned int nr_entries;
	unsigned long flags;

	*trackerp = tracker = kzalloc(sizeof(*tracker), gfp);
	if (!tracker) {
		pr_err_once("memory allocation failure, unreliable refcount tracker.\n");
		return -ENOMEM;
	}
	nr_entries = stack_trace_save(entries, ARRAY_SIZE(entries), 1);
	tracker->alloc_stack_handle = stack_depot_save(entries, nr_entries, gfp);

	spin_lock_irqsave(&dir->lock, flags);
	list_add(&tracker->head, &dir->list);
	spin_unlock_irqrestore(&dir->lock, flags);
	return 0;
}
EXPORT_SYMBOL_GPL(ref_tracker_alloc);

int ref_tracker_free(struct ref_tracker_dir *dir,
		     struct ref_tracker **trackerp)
{
	unsigned long entries[REF_TRACKER_STACK_ENTRIES];
	struct ref_tracker *tracker = *trackerp;
	unsigned int nr_entries;
	unsigned long flags;

	if (!tracker)
		return -EEXIST;
	spin_lock_irqsave(&dir->lock, flags);
	if (tracker->dead) {
		pr_err("reference already released.\n");
		if (tracker->alloc_stack_handle) {
			pr_err("allocated in:\n");
			stack_depot_print(tracker->alloc_stack_handle);
		}
		if (tracker->free_stack_handle) {
			pr_err("freed in:\n");
			stack_depot_print(tracker->free_stack_handle);
		}
		spin_unlock_irqrestore(&dir->lock, flags);
		return -EINVAL;
	}
	tracker->dead = true;

	nr_entries = stack_trace_save(entries, ARRAY_SIZE(entries), 1);
	tracker->free_stack_handle = stack_depot_save(entries, nr_entries, GFP_ATOMIC);

	list_move_tail(&tracker->head, &dir->quarantine);
	if (!dir->quarantine_avail) {
		tracker = list_first_entry(&dir->quarantine, struct ref_tracker, head);
		list_del(&tracker->head);
		kfree(tracker);
	} else {
		dir->quarantine_avail--;
	}
	spin_unlock_irqrestore(&dir->lock, flags);
	return 0;
}
EXPORT_SYMBOL_GPL(ref_tracker_free);
