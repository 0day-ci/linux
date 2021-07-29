// SPDX-License-Identifier: GPL-2.0-only

/*
 *	Block device LED triggers
 *
 *	Copyright 2021 Ian Pilcher <arequipeno@gmail.com>
 */

#include <linux/blk-ledtrig.h>
#include <linux/ctype.h>
#include <linux/genhd.h>
#include <linux/leds.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/slab.h>

#include "blk-ledtrig.h"


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


/*
 *
 *	Class attributes to manage the trigger list
 *
 */

static ssize_t blk_ledtrig_attr_store(struct class *, struct class_attribute *,
				      const char *, const size_t);
static ssize_t blk_ledtrig_list_show(struct class *, struct class_attribute *,
				     char *);

static struct class_attribute blk_ledtrig_attr_new =
	__ATTR(led_trigger_new, 0200, 0, blk_ledtrig_attr_store);

static struct class_attribute blk_ledtrig_attr_del =
	__ATTR(led_trigger_del, 0200, 0, blk_ledtrig_attr_store);

static struct class_attribute blk_ledtrig_attr_list =
	__ATTR(led_trigger_list, 0444, blk_ledtrig_list_show, 0);

// Returns a pointer to the first non-whitespace character in s (or a pointer
// to the terminating nul).
static const char *blk_ledtrig_skip_whitespace(const char *s)
{
	while (*s != 0 && isspace(*s))
		++s;

	return s;
}

// Returns a pointer to the first whitespace character in s (or a pointer to
// the terminating nul), which is effectively a pointer to the position *after*
// the last character in the non-whitespace token at the beginning of s.  (s is
// expected to be the result of a previous call to blk_ledtrig_skip_whitespace.)
static const char *blk_ledtrig_find_whitespace(const char *s)
{
	while (*s != 0 && !isspace(*s))
		++s;

	return s;
}

static ssize_t blk_ledtrig_attr_store(struct class *const class,
				      struct class_attribute *const attr,
				      const char *const buf,
				      const size_t count)
{
	const char *const name = blk_ledtrig_skip_whitespace(buf);
	const char *const endp = blk_ledtrig_find_whitespace(name);
	const ptrdiff_t name_len = endp - name;		// always >= 0
	int ret;

	if (attr == &blk_ledtrig_attr_new)
		ret = __blk_ledtrig_create(name, name_len);
	else	// attr == &blk_ledtrig_attr_del
		ret = __blk_ledtrig_delete(name, name_len);

	if (ret < 0)
		return ret;

	// Avoid potential "empty name" error by skipping whitespace
	// to next token or terminating nul
	return blk_ledtrig_skip_whitespace(endp) - buf;
}

static ssize_t blk_ledtrig_list_show(struct class *const class,
				     struct class_attribute *const attr,
				     char *const buf)
{
	struct list_head *n;
	int ret, c = 0;

	ret = mutex_lock_interruptible(&blk_ledtrig_list_mutex);
	if (unlikely(ret != 0))
		goto list_exit_return;

	list_for_each(n, &blk_ledtrig_list) {

		struct blk_ledtrig *const t = blk_ledtrig_from_node(n);
		int refcount;

		ret = mutex_lock_interruptible(&t->refcount_mutex);
		if (unlikely(ret != 0))
			goto list_exit_unlock_list;

		refcount = t->refcount;

		mutex_unlock(&t->refcount_mutex);

		ret = snprintf(buf + c, PAGE_SIZE - c, "%s: %d\n",
			       t->name, refcount);
		if (unlikely(ret < 0))
			goto list_exit_unlock_list;

		c += ret;
		if (unlikely(c >= PAGE_SIZE)) {
			ret = -EOVERFLOW;
			goto list_exit_unlock_list;
		}
	}

	ret = c;

list_exit_unlock_list:
	mutex_unlock(&blk_ledtrig_list_mutex);
list_exit_return:
	return ret;
}


/*
 *
 *	Initialization - create the class attributes
 *
 */

void __init blk_ledtrig_init(void)
{
	int ret;

	ret = class_create_file(&block_class, &blk_ledtrig_attr_new);
	if (unlikely(ret != 0))
		goto init_error_new;

	ret = class_create_file(&block_class, &blk_ledtrig_attr_del);
	if (unlikely(ret != 0))
		goto init_error_del;

	ret = class_create_file(&block_class, &blk_ledtrig_attr_list);
	if (unlikely(ret != 0))
		goto init_error_list;

	return;

init_error_list:
	class_remove_file(&block_class, &blk_ledtrig_attr_del);
init_error_del:
	class_remove_file(&block_class, &blk_ledtrig_attr_new);
init_error_new:
	pr_err("failed to initialize blkdev LED triggers (%d)\n", ret);
}


/*
 *
 *	Set a device trigger
 *
 */

static int __blk_ledtrig_set(struct gendisk *const gd, const char *const name,
			     const size_t name_len)
{
	struct blk_ledtrig *t;
	bool already_set;
	int ret;

	ret = mutex_lock_interruptible(&blk_ledtrig_list_mutex);
	if (unlikely(ret != 0))
		goto set_exit_return;

	t = blk_ledtrig_find(name, name_len);
	if (t == NULL) {
		pr_warn("blockdev LED trigger named %.*s doesn't exist\n",
			(int)name_len, name);
		ret = -ENODEV;
		goto set_exit_unlock_list;
	}

	ret = mutex_lock_interruptible(&t->refcount_mutex);
	if (unlikely(ret != 0))
		goto set_exit_unlock_list;

	// Holding the refcount mutex blocks __blk_ledtrig_delete, so we don't
	// actually need to hold the list mutex anymore, but it makes the flow
	// much simpler to do so

	if (WARN_ON_ONCE(t->refcount == INT_MAX)) {
		ret = -ERANGE;
		goto set_exit_unlock_refcount;
	}

	ret = mutex_lock_interruptible(&gd->ledtrig_mutex);
	if (unlikely(ret != 0))
		goto set_exit_unlock_refcount;

	if (gd->ledtrig == NULL) {
		already_set = false;
		gd->ledtrig = t;
	} else {
		already_set = true;
	}

	mutex_unlock(&gd->ledtrig_mutex);

	if (already_set) {
		pr_warn("blockdev trigger for %s already set\n",
			gd->disk_name);
		ret = -EBUSY;
		goto set_exit_unlock_refcount;
	}

	++(t->refcount);
	ret = 0;

set_exit_unlock_refcount:
	mutex_unlock(&t->refcount_mutex);
set_exit_unlock_list:
	mutex_unlock(&blk_ledtrig_list_mutex);
set_exit_return:
	return ret;
}

/**
 * blk_ledtrig_set() - set the LED trigger for a block device
 * @gd: the block device
 * @name: the name of the LED trigger
 *
 * Context: Process context (can sleep).  Takes and releases
 *	    @blk_ledtrig_list_mutex, trigger's @refcount_mutex,
 *	    and @gd->ledtrig_mutex.
 *
 * Return: 0 on success; -@errno on error
 */
int blk_ledtrig_set(struct gendisk *const gd, const char *const name)
{
	return __blk_ledtrig_set(gd, name, strlen(name));
}
EXPORT_SYMBOL_GPL(blk_ledtrig_set);


/*
 *
 *	Clear a device trigger
 *
 */

/**
 * blk_ledtrig_clear() - clear the LED trigger of a block device
 * @gd: the block device
 *
 * Context: Process context (can sleep).  Takes and releases
 *	    @gd->ledtrig_mutex and @gd->ledtrig->refcount_mutex.
 *
 * Return: @true if the trigger was actually cleared; @false if it wasn't set
 */
bool blk_ledtrig_clear(struct gendisk *const gd)
{
	struct blk_ledtrig *t;
	bool changed;
	int new_refcount;

	mutex_lock(&gd->ledtrig_mutex);

	t = gd->ledtrig;
	if (t == NULL) {
		changed = false;
		goto clear_exit_unlock_ledtrig;
	}

	mutex_lock(&t->refcount_mutex);
	new_refcount = --(t->refcount);
	mutex_unlock(&t->refcount_mutex);

	gd->ledtrig = NULL;
	changed = true;

clear_exit_unlock_ledtrig:
	mutex_unlock(&gd->ledtrig_mutex);
	WARN_ON(changed && (new_refcount < 0));
	return changed;
}
EXPORT_SYMBOL_GPL(blk_ledtrig_clear);


/*
 *
 *	Set, clear & show LED triggers via sysfs device attributes
 *
 *	(See dev_attr_led_trigger and disk_attrs in genhd.c)
 *
 */

ssize_t blk_ledtrig_devattr_store(struct device *const dev,
				  struct device_attribute *const attr,
				  const char *const buf, const size_t count)
{
	struct gendisk *const gd = dev_to_disk(dev);
	const char *const name = blk_ledtrig_skip_whitespace(buf);
	const char *const endp = blk_ledtrig_find_whitespace(name);
	const ptrdiff_t name_len = endp - name;		// always >= 0
	int ret;

	if (name_len == 0)
		ret = blk_ledtrig_clear(gd);
	else
		ret = __blk_ledtrig_set(gd, name, name_len);

	if (ret < 0)
		return ret;

	return blk_ledtrig_skip_whitespace(endp) - buf;
}

ssize_t blk_ledtrig_devattr_show(struct device *const dev,
				 struct device_attribute *const attr,
				 char *const buf)
{
	struct gendisk *const gd = dev_to_disk(dev);
	const struct blk_ledtrig *t;
	size_t name_len;
	int ret;

	ret = mutex_lock_interruptible(&gd->ledtrig_mutex);
	if (unlikely(ret != 0))
		return ret;

	t = gd->ledtrig;

	if (t != NULL) {
		name_len = strlen(t->name);
		if (likely(name_len < PAGE_SIZE - 1))
			memcpy(buf, t->name, name_len);
	}

	mutex_unlock(&gd->ledtrig_mutex);

	if (t == NULL)
		return sprintf(buf, "(none)\n");

	if (unlikely(name_len >= PAGE_SIZE - 1))
		return -EOVERFLOW;

	buf[name_len] = '\n';
	buf[name_len + 1] = 0;

	return (ssize_t)(name_len + 1);
}
