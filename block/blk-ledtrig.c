// SPDX-License-Identifier: GPL-2.0-only

/*
 *	Block device LED triggers
 *
 *	Copyright 2021 Ian Pilcher <arequipeno@gmail.com>
 */

#include <linux/blk-ledtrig.h>
#include <linux/leds.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/slab.h>


/*
 *
 *	The list of block device LED triggers
 *
 */

struct blk_ledtrig {
	struct led_trigger	trigger;
	struct list_head	list_node;
	struct mutex		refcount_mutex;
	int			refcount;
	char			name[];
};

LIST_HEAD(blk_ledtrig_list);
DEFINE_MUTEX(blk_ledtrig_list_mutex);

static inline
struct blk_ledtrig *blk_ledtrig_from_node(struct list_head *const node)
{
	return container_of(node, struct blk_ledtrig, list_node);
}

// Caller must hold blk_ledtrig_list_mutex
static struct blk_ledtrig *blk_ledtrig_find(const char *const name,
					    const size_t len)
{
	struct blk_ledtrig *t;
	struct list_head *n;

	list_for_each(n, &blk_ledtrig_list) {
		t = blk_ledtrig_from_node(n);
		if (strlen(t->name) == len && memcmp(name, t->name, len) == 0)
			return t;
	}

	return NULL;
}


/*
 *
 *	Create a new trigger
 *
 */

static int __blk_ledtrig_create(const char *const name, const size_t len)
{
	struct blk_ledtrig *t;
	int ret;

	if (len == 0) {
		pr_warn("empty name specified for blockdev LED trigger\n");
		ret = -EINVAL;
		goto create_exit_return;
	}

	ret = mutex_lock_interruptible(&blk_ledtrig_list_mutex);
	if (unlikely(ret != 0))
		goto create_exit_return;

	if (blk_ledtrig_find(name, len) != NULL) {
		pr_warn("blockdev LED trigger named %.*s already exists\n",
			(int)len, name);
		ret = -EEXIST;
		goto create_exit_unlock_list;
	}

	t = kzalloc(sizeof(*t) + len + 1, GFP_KERNEL);
	if (unlikely(t == NULL)) {
		ret = -ENOMEM;
		goto create_exit_unlock_list;
	}

	memcpy(t->name, name, len);
	t->trigger.name = t->name;
	mutex_init(&t->refcount_mutex);

	ret = led_trigger_register(&t->trigger);
	if (ret != 0) {
		if (likely(ret == -EEXIST)) {
			pr_warn("LED trigger named %.*s already exists\n",
				(int)len, name);
		}
		goto create_exit_free;
	}

	list_add(&t->list_node, &blk_ledtrig_list);
	ret = 0;

create_exit_free:
	if (ret != 0)
		kfree(t);
create_exit_unlock_list:
	mutex_unlock(&blk_ledtrig_list_mutex);
create_exit_return:
	return ret;
}

/**
 * blk_ledtrig_create() - creates a new block device LED trigger
 * @name: the name of the new trigger
 *
 * Context: Process context (can sleep).  Takes and releases
 *	    @blk_ledtrig_list_mutex.
 *
 * Return: 0 on success; -@errno on error
 */
int blk_ledtrig_create(const char *const name)
{
	return __blk_ledtrig_create(name, strlen(name));
}
EXPORT_SYMBOL_GPL(blk_ledtrig_create);


/*
 *
 *	Delete a trigger
 *
 */

static int __blk_ledtrig_delete(const char *const name, const size_t len)
{
	struct blk_ledtrig *t;
	int ret;

	if (len == 0) {
		pr_warn("empty name specified for blockdev LED trigger\n");
		ret = -EINVAL;
		goto delete_exit_return;
	}

	ret = mutex_lock_interruptible(&blk_ledtrig_list_mutex);
	if (unlikely(ret != 0))
		goto delete_exit_return;

	t = blk_ledtrig_find(name, len);
	if (t == NULL) {
		pr_warn("blockdev LED trigger named %.*s doesn't exist\n",
			(int)len, name);
		ret = -ENODEV;
		goto delete_exit_unlock_list;
	}

	ret = mutex_lock_interruptible(&t->refcount_mutex);
	if (unlikely(ret != 0))
		goto delete_exit_unlock_list;

	if (WARN_ON(t->refcount < 0)) {
		ret = -EBADFD;
		goto delete_exit_unlock_refcount;
	}

	if (t->refcount > 0) {
		pr_warn("blockdev LED trigger %s still in use\n", t->name);
		ret = -EBUSY;
		goto delete_exit_unlock_refcount;
	}

	led_trigger_unregister(&t->trigger);
	list_del(&t->list_node);

	ret = 0;

delete_exit_unlock_refcount:
	mutex_unlock(&t->refcount_mutex);
	if (ret == 0)
		kfree(t);
delete_exit_unlock_list:
	mutex_unlock(&blk_ledtrig_list_mutex);
delete_exit_return:
	return ret;
}

/**
 * blk_ledtrig_delete() - deletes a block device LED trigger
 * @name: the name of the trigger to be deleted
 *
 * Context: Process context (can sleep).  Takes and releases
 *	    @blk_ledtrig_list_mutex and trigger's @refcount_mutex.
 *
 * Return: 0 on success; -@errno on error
 */
int blk_ledtrig_delete(const char *const name)
{
	return __blk_ledtrig_delete(name, strlen(name));
}
EXPORT_SYMBOL_GPL(blk_ledtrig_delete);
