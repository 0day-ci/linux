// SPDX-License-Identifier: GPL-2.0
#include <linux/types.h>
#include <linux/fsnotify.h>
#include <linux/memcontrol.h>

#define INVALID_RING_SLOT -1

#define FSNOTIFY_RING_PAGES 16

#define NEXT_SLOT(cur, len, ring_size) ((cur + len) & (ring_size-1))
#define NEXT_PAGE(cur, ring_size) (round_up(cur, PAGE_SIZE) & (ring_size-1))

bool fsnotify_ring_notify_queue_is_empty(struct fsnotify_group *group)
{
	assert_spin_locked(&group->notification_lock);

	if (group->ring_buffer.tail == group->ring_buffer.head)
		return true;
	return false;
}

struct fsnotify_event *fsnotify_ring_peek_first_event(struct fsnotify_group *group)
{
	u64 ring_size = group->ring_buffer.nr_pages << PAGE_SHIFT;
	struct fsnotify_event *fsn;
	char *kaddr;
	u64 tail;

	assert_spin_locked(&group->notification_lock);

again:
	tail = group->ring_buffer.tail;

	if ((PAGE_SIZE - (tail & (PAGE_SIZE-1))) < sizeof(struct fsnotify_event)) {
		group->ring_buffer.tail = NEXT_PAGE(tail, ring_size);
		goto again;
	}

	kaddr = kmap_atomic(group->ring_buffer.pages[tail / PAGE_SIZE]);
	if (!kaddr)
		return NULL;
	fsn = (struct fsnotify_event *) (kaddr + (tail & (PAGE_SIZE-1)));

	if (fsn->slot_len == INVALID_RING_SLOT) {
		group->ring_buffer.tail = NEXT_PAGE(tail, ring_size);
		kunmap_atomic(kaddr);
		goto again;
	}

	/* will be unmapped when entry is consumed. */
	return fsn;
}

void fsnotify_ring_buffer_consume_event(struct fsnotify_group *group,
					struct fsnotify_event *event)
{
	u64 ring_size = group->ring_buffer.nr_pages << PAGE_SHIFT;
	u64 new_tail = NEXT_SLOT(group->ring_buffer.tail, event->slot_len, ring_size);

	kunmap_atomic(event);

	pr_debug("%s: group=%p tail=%llx->%llx ring_size=%llu\n", __func__,
		 group, group->ring_buffer.tail, new_tail, ring_size);

	WRITE_ONCE(group->ring_buffer.tail, new_tail);
}

struct fsnotify_event *fsnotify_ring_alloc_event_slot(struct fsnotify_group *group,
						      size_t size)
	__acquires(&group->notification_lock)
{
	struct fsnotify_event *fsn;
	u64 head, tail;
	u64 ring_size = group->ring_buffer.nr_pages << PAGE_SHIFT;
	u64 new_head;
	void *kaddr;

	if (WARN_ON(!(group->flags & FSN_SUBMISSION_RING_BUFFER) || size > PAGE_SIZE))
		return ERR_PTR(-EINVAL);

	pr_debug("%s: start group=%p ring_size=%llu, requested=%lu\n", __func__, group,
		 ring_size, size);

	spin_lock(&group->notification_lock);
again:
	head = group->ring_buffer.head;
	tail = group->ring_buffer.tail;
	new_head = NEXT_SLOT(head, size, ring_size);

	/* head would catch up to tail, corrupting an entry. */
	if ((head < tail && new_head > tail) || (head > new_head && new_head > tail)) {
		fsn = ERR_PTR(-ENOMEM);
		goto err;
	}

	/*
	 * Not event a skip message fits in the page. We can detect the
	 * lack of space. Move on to the next page.
	 */
	if ((PAGE_SIZE - (head & (PAGE_SIZE-1))) < sizeof(struct fsnotify_event)) {
		/* Start again on next page */
		group->ring_buffer.head = NEXT_PAGE(head, ring_size);
		goto again;
	}

	kaddr = kmap_atomic(group->ring_buffer.pages[head / PAGE_SIZE]);
	if (!kaddr) {
		fsn = ERR_PTR(-EFAULT);
		goto err;
	}

	fsn = (struct fsnotify_event *) (kaddr + (head & (PAGE_SIZE-1)));

	if ((head >> PAGE_SHIFT) != (new_head >> PAGE_SHIFT)) {
		/*
		 * No room in the current page.  Add a fake entry
		 * consuming the end the page to avoid splitting event
		 * structure.
		 */
		fsn->slot_len = INVALID_RING_SLOT;
		kunmap_atomic(kaddr);
		/* Start again on the next page */
		group->ring_buffer.head = NEXT_PAGE(head, ring_size);

		goto again;
	}
	fsn->slot_len = size;

	return fsn;

err:
	spin_unlock(&group->notification_lock);
	return fsn;
}

void fsnotify_ring_commit_slot(struct fsnotify_group *group, struct fsnotify_event *fsn)
	__releases(&group->notification_lock)
{
	u64 ring_size = group->ring_buffer.nr_pages << PAGE_SHIFT;
	u64 head = group->ring_buffer.head;
	u64 new_head = NEXT_SLOT(head, fsn->slot_len, ring_size);

	pr_debug("%s: group=%p head=%llx->%llx ring_size=%llu\n", __func__,
		 group, head, new_head, ring_size);

	kunmap_atomic(fsn);
	group->ring_buffer.head = new_head;

	spin_unlock(&group->notification_lock);

	wake_up(&group->notification_waitq);
	kill_fasync(&group->fsn_fa, SIGIO, POLL_IN);

}

void fsnotify_free_ring_buffer(struct fsnotify_group *group)
{
	int i;

	for (i = 0; i < group->ring_buffer.nr_pages; i++)
		__free_page(group->ring_buffer.pages[i]);
	kfree(group->ring_buffer.pages);
	group->ring_buffer.nr_pages = 0;
}

int fsnotify_create_ring_buffer(struct fsnotify_group *group)
{
	int nr_pages = FSNOTIFY_RING_PAGES;
	int i;

	pr_debug("%s: group=%p pages=%d\n", __func__, group, nr_pages);

	group->ring_buffer.pages = kmalloc_array(nr_pages, sizeof(struct pages *),
						 GFP_KERNEL);
	if (!group->ring_buffer.pages)
		return -ENOMEM;

	group->ring_buffer.head = 0;
	group->ring_buffer.tail = 0;

	for (i = 0; i < nr_pages; i++) {
		group->ring_buffer.pages[i] = alloc_pages(GFP_KERNEL, 1);
		if (!group->ring_buffer.pages)
			goto err_dealloc;
	}

	group->ring_buffer.nr_pages = nr_pages;

	return 0;

err_dealloc:
	for (--i; i >= 0; i--)
		__free_page(group->ring_buffer.pages[i]);
	kfree(group->ring_buffer.pages);
	group->ring_buffer.nr_pages = 0;
	return -ENOMEM;
}


