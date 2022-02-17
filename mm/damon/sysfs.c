// SPDX-License-Identifier: GPL-2.0
/*
 * DAMON sysfs Interface
 *
 * Copyright (c) 2022 SeongJae Park <sj@kernel.org>
 */

#include <linux/damon.h>
#include <linux/kobject.h>
#include <linux/pid.h>
#include <linux/sched.h>
#include <linux/slab.h>

static DEFINE_MUTEX(damon_sysfs_lock);

/*
 * unsigned long range directory
 */

struct damon_sysfs_ul_range {
	struct kobject kobj;
	unsigned long min;
	unsigned long max;
};

static struct damon_sysfs_ul_range *damon_sysfs_ul_range_alloc(
		unsigned long min,
		unsigned long max)
{
	struct damon_sysfs_ul_range *range = kmalloc(sizeof(*range),
			GFP_KERNEL);

	if (!range)
		return NULL;
	range->kobj = (struct kobject){};
	range->min = min;
	range->max = max;

	return range;
}

static ssize_t damon_sysfs_ul_range_min_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_ul_range *range = container_of(kobj,
			struct damon_sysfs_ul_range, kobj);

	return sysfs_emit(buf, "%lu\n", range->min);
}

static ssize_t damon_sysfs_ul_range_min_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_ul_range *range = container_of(kobj,
			struct damon_sysfs_ul_range, kobj);
	unsigned long min;
	int err;

	err = kstrtoul(buf, 0, &min);
	if (err)
		return -EINVAL;

	range->min = min;
	return count;
}

static ssize_t damon_sysfs_ul_range_max_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_ul_range *range = container_of(kobj,
			struct damon_sysfs_ul_range, kobj);

	return sysfs_emit(buf, "%lu\n", range->max);
}

static ssize_t damon_sysfs_ul_range_max_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_ul_range *range = container_of(kobj,
			struct damon_sysfs_ul_range, kobj);
	unsigned long max;
	int err;

	err = kstrtoul(buf, 0, &max);
	if (err)
		return -EINVAL;

	range->max = max;
	return count;
}

static void damon_sysfs_ul_range_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_ul_range, kobj));
}

static struct kobj_attribute damon_sysfs_ul_range_min_attr =
		__ATTR(min, 0600, damon_sysfs_ul_range_min_show,
				damon_sysfs_ul_range_min_store);

static struct kobj_attribute damon_sysfs_ul_range_max_attr =
		__ATTR(max, 0600, damon_sysfs_ul_range_max_show,
				damon_sysfs_ul_range_max_store);

static struct attribute *damon_sysfs_ul_range_attrs[] = {
	&damon_sysfs_ul_range_min_attr.attr,
	&damon_sysfs_ul_range_max_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_ul_range);

static struct kobj_type damon_sysfs_ul_range_ktype = {
	.release = damon_sysfs_ul_range_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_ul_range_groups,
};

/*
 * target directory
 */

struct damon_sysfs_target {
	struct kobject kobj;
	int pid;
};

static struct damon_sysfs_target *damon_sysfs_target_alloc(void)
{
	return kzalloc(sizeof(struct damon_sysfs_target), GFP_KERNEL);
}

static ssize_t damon_sysfs_target_pid_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_target *target = container_of(kobj,
			struct damon_sysfs_target, kobj);

	return sysfs_emit(buf, "%d\n", target->pid);
}

static ssize_t damon_sysfs_target_pid_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_target *target = container_of(kobj,
			struct damon_sysfs_target, kobj);
	int err = kstrtoint(buf, 0, &target->pid);

	if (err)
		return -EINVAL;
	return count;
}

static void damon_sysfs_target_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_target, kobj));
}

static struct kobj_attribute damon_sysfs_target_pid_attr = __ATTR(pid, 0600,
		damon_sysfs_target_pid_show, damon_sysfs_target_pid_store);

static struct attribute *damon_sysfs_target_attrs[] = {
	&damon_sysfs_target_pid_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_target);

static struct kobj_type damon_sysfs_target_ktype = {
	.release = damon_sysfs_target_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_target_groups,
};

/*
 * targets directory
 */

struct damon_sysfs_targets {
	struct kobject kobj;
	struct damon_sysfs_target **targets_arr;
	int nr_targets;
};

static struct damon_sysfs_targets *damon_sysfs_targets_alloc(void)
{
	return kzalloc(sizeof(struct damon_sysfs_targets), GFP_KERNEL);
}

static void damon_sysfs_targets_rm_dirs(struct damon_sysfs_targets *targets)
{
	struct damon_sysfs_target **targets_arr = targets->targets_arr;
	int i;

	for (i = 0; i < targets->nr_targets; i++)
		kobject_put(&targets_arr[i]->kobj);
	kfree(targets_arr);
	targets->targets_arr = NULL;
	targets->nr_targets = 0;
}

static int damon_sysfs_targets_add_dirs(struct damon_sysfs_targets *targets,
		int nr_targets)
{
	struct damon_sysfs_target **targets_arr, *target;
	int err, i;

	damon_sysfs_targets_rm_dirs(targets);
	if (!nr_targets)
		return 0;

	targets_arr = kmalloc_array(nr_targets, sizeof(*targets_arr),
			GFP_KERNEL);
	if (!targets_arr)
		return -ENOMEM;
	targets->targets_arr = targets_arr;

	for (i = 0; i < nr_targets; i++) {
		target = damon_sysfs_target_alloc();
		if (!target) {
			damon_sysfs_targets_rm_dirs(targets);
			return -ENOMEM;
		}

		err = kobject_init_and_add(&target->kobj,
				&damon_sysfs_target_ktype, &targets->kobj,
				"%d", i);
		if (err) {
			kfree(target);
			damon_sysfs_targets_rm_dirs(targets);
			return err;
		}

		targets_arr[i] = target;
		targets->nr_targets++;
	}
	return 0;
}

static ssize_t damon_sysfs_targets_nr_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_targets *targets = container_of(kobj,
			struct damon_sysfs_targets, kobj);

	return sysfs_emit(buf, "%d\n", targets->nr_targets);
}

static ssize_t damon_sysfs_targets_nr_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_targets *targets = container_of(kobj,
			struct damon_sysfs_targets, kobj);
	int nr;
	int err = kstrtoint(buf, 0, &nr);

	if (err)
		return err;
	if (nr < 0)
		return -EINVAL;

	if (!mutex_trylock(&damon_sysfs_lock))
		return -EBUSY;
	err = damon_sysfs_targets_add_dirs(targets, nr);
	mutex_unlock(&damon_sysfs_lock);
	if (err)
		return err;

	return count;
}

static void damon_sysfs_targets_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_targets, kobj));
}

static struct kobj_attribute damon_sysfs_targets_nr_attr = __ATTR(nr, 0600,
		damon_sysfs_targets_nr_show, damon_sysfs_targets_nr_store);

static struct attribute *damon_sysfs_targets_attrs[] = {
	&damon_sysfs_targets_nr_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_targets);

static struct kobj_type damon_sysfs_targets_ktype = {
	.release = damon_sysfs_targets_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_targets_groups,
};

/*
 * intervals directory
 */

struct damon_sysfs_intervals {
	struct kobject kobj;
	unsigned long sample_us;
	unsigned long aggr_us;
	unsigned long update_us;
};

static struct damon_sysfs_intervals *damon_sysfs_intervals_alloc(
		unsigned long sample_us, unsigned long aggr_us,
		unsigned long update_us)
{
	struct damon_sysfs_intervals *intervals = kmalloc(sizeof(*intervals),
			GFP_KERNEL);

	if (!intervals)
		return NULL;

	intervals->kobj = (struct kobject){};
	intervals->sample_us = sample_us;
	intervals->aggr_us = aggr_us;
	intervals->update_us = update_us;
	return intervals;
}

static ssize_t damon_sysfs_intervals_sample_us_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_intervals *intervals = container_of(kobj,
			struct damon_sysfs_intervals, kobj);

	return sysfs_emit(buf, "%lu\n", intervals->sample_us);
}

static ssize_t damon_sysfs_intervals_sample_us_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_intervals *intervals = container_of(kobj,
			struct damon_sysfs_intervals, kobj);
	unsigned long us;
	int err = kstrtoul(buf, 0, &us);

	if (err)
		return -EINVAL;

	intervals->sample_us = us;
	return count;
}

static ssize_t damon_sysfs_intervals_aggr_us_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_intervals *intervals = container_of(kobj,
			struct damon_sysfs_intervals, kobj);

	return sysfs_emit(buf, "%lu\n", intervals->aggr_us);
}

static ssize_t damon_sysfs_intervals_aggr_us_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_intervals *intervals = container_of(kobj,
			struct damon_sysfs_intervals, kobj);
	unsigned long us;
	int err = kstrtoul(buf, 0, &us);

	if (err)
		return -EINVAL;

	intervals->aggr_us = us;
	return count;
}

static ssize_t damon_sysfs_intervals_update_us_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_intervals *intervals = container_of(kobj,
			struct damon_sysfs_intervals, kobj);

	return sysfs_emit(buf, "%lu\n", intervals->update_us);
}

static ssize_t damon_sysfs_intervals_update_us_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_intervals *intervals = container_of(kobj,
			struct damon_sysfs_intervals, kobj);
	unsigned long us;
	int err = kstrtoul(buf, 0, &us);

	if (err)
		return -EINVAL;

	intervals->update_us = us;
	return count;
}

static void damon_sysfs_intervals_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_intervals, kobj));
}

static struct kobj_attribute damon_sysfs_intervals_sample_us_attr =
		__ATTR(sample_us, 0600,
				damon_sysfs_intervals_sample_us_show,
				damon_sysfs_intervals_sample_us_store);

static struct kobj_attribute damon_sysfs_intervals_aggr_us_attr =
		__ATTR(aggr_us, 0600,
				damon_sysfs_intervals_aggr_us_show,
				damon_sysfs_intervals_aggr_us_store);

static struct kobj_attribute damon_sysfs_intervals_update_us_attr =
		__ATTR(update_us, 0600,
				damon_sysfs_intervals_update_us_show,
				damon_sysfs_intervals_update_us_store);

static struct attribute *damon_sysfs_intervals_attrs[] = {
	&damon_sysfs_intervals_sample_us_attr.attr,
	&damon_sysfs_intervals_aggr_us_attr.attr,
	&damon_sysfs_intervals_update_us_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_intervals);

static struct kobj_type damon_sysfs_intervals_ktype = {
	.release = damon_sysfs_intervals_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_intervals_groups,
};

/*
 * monitoring_attrs directory
 */

struct damon_sysfs_attrs {
	struct kobject kobj;
	struct damon_sysfs_intervals *intervals;
	struct damon_sysfs_ul_range *nr_regions;
};

static struct damon_sysfs_attrs *damon_sysfs_attrs_alloc(void)
{
	struct damon_sysfs_attrs *attrs = kmalloc(sizeof(*attrs), GFP_KERNEL);

	if (!attrs)
		return NULL;
	attrs->kobj = (struct kobject){};
	return attrs;
}

static int damon_sysfs_attrs_add_dirs(struct damon_sysfs_attrs *attrs)
{
	struct damon_sysfs_intervals *intervals;
	struct damon_sysfs_ul_range *nr_regions_range;
	int err;

	intervals = damon_sysfs_intervals_alloc(5000, 100000, 60000000);
	if (!intervals)
		return -ENOMEM;

	err = kobject_init_and_add(&intervals->kobj,
			&damon_sysfs_intervals_ktype, &attrs->kobj,
			"intervals");
	if (err) {
		kfree(intervals);
		return err;
	}
	attrs->intervals = intervals;

	nr_regions_range = damon_sysfs_ul_range_alloc(10, 1000);
	if (!nr_regions_range)
		return -ENOMEM;

	err = kobject_init_and_add(&nr_regions_range->kobj,
			&damon_sysfs_ul_range_ktype, &attrs->kobj,
			"nr_regions");
	if (err) {
		kfree(nr_regions_range);
		kobject_put(&intervals->kobj);
		return err;
	}
	attrs->nr_regions = nr_regions_range;

	return 0;
}

static void damon_sysfs_attrs_rm_dirs(struct damon_sysfs_attrs *attrs)
{
	kobject_put(&attrs->nr_regions->kobj);
	kobject_put(&attrs->intervals->kobj);
}

static void damon_sysfs_attrs_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_attrs, kobj));
}

static struct attribute *damon_sysfs_attrs_attrs[] = {
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_attrs);

static struct kobj_type damon_sysfs_attrs_ktype = {
	.release = damon_sysfs_attrs_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_attrs_groups,
};

/*
 * context directory
 */

static const char * const damon_sysfs_ops_strs[] = {
	"vaddr\n",
	"paddr\n",
};

struct damon_sysfs_context {
	struct kobject kobj;
	enum damon_ops_id ops_id;
	struct damon_sysfs_attrs *attrs;
	struct damon_sysfs_targets *targets;
};

static struct damon_sysfs_context *damon_sysfs_context_alloc(
		enum damon_ops_id ops_id)
{
	struct damon_sysfs_context *context = kmalloc(sizeof(*context),
				GFP_KERNEL);

	if (!context)
		return NULL;
	context->kobj = (struct kobject){};
	context->ops_id = ops_id;
	return context;
}

static int damon_sysfs_context_add_dirs(struct damon_sysfs_context *context)
{
	struct damon_sysfs_attrs *attrs;
	struct damon_sysfs_targets *targets;
	int err;

	/* add monitoring_attrs directory */
	attrs = damon_sysfs_attrs_alloc();
	if (!attrs)
		return -ENOMEM;
	err = kobject_init_and_add(&attrs->kobj, &damon_sysfs_attrs_ktype,
			&context->kobj, "monitoring_attrs");
	if (err) {
		kfree(attrs);
		return err;
	}
	err = damon_sysfs_attrs_add_dirs(attrs);
	if (err)
		goto put_attrs_out;

	/* add targets directory */
	targets = damon_sysfs_targets_alloc();
	if (!targets) {
		err = -ENOMEM;
		goto put_attrs_out;
	}
	err = kobject_init_and_add(&targets->kobj, &damon_sysfs_targets_ktype,
			&context->kobj, "targets");
	if (err) {
		kfree(targets);
		goto put_attrs_out;
	}

	context->attrs = attrs;
	context->targets = targets;
	return err;

put_attrs_out:
	kobject_put(&attrs->kobj);
	return err;
}

static void damon_sysfs_context_rm_dirs(struct damon_sysfs_context *context)
{
	damon_sysfs_attrs_rm_dirs(context->attrs);
	kobject_put(&context->attrs->kobj);
	damon_sysfs_targets_rm_dirs(context->targets);
	kobject_put(&context->targets->kobj);
}

static ssize_t damon_sysfs_context_operations_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_context *context = container_of(kobj,
			struct damon_sysfs_context, kobj);

	return sysfs_emit(buf, "%s\n", damon_sysfs_ops_strs[context->ops_id]);
}

static ssize_t damon_sysfs_context_operations_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_context *context = container_of(kobj,
			struct damon_sysfs_context, kobj);

	if (!strncmp(buf, damon_sysfs_ops_strs[DAMON_OPS_VADDR], count))
		context->ops_id = DAMON_OPS_VADDR;
	else if (!strncmp(buf, damon_sysfs_ops_strs[DAMON_OPS_PADDR], count))
		context->ops_id = DAMON_OPS_PADDR;
	else
		return -EINVAL;

	return count;
}

static void damon_sysfs_context_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_context, kobj));
}

static struct kobj_attribute damon_sysfs_context_operations_attr = __ATTR(
		operations, 0600, damon_sysfs_context_operations_show,
		damon_sysfs_context_operations_store);

static struct attribute *damon_sysfs_context_attrs[] = {
	&damon_sysfs_context_operations_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_context);

static struct kobj_type damon_sysfs_context_ktype = {
	.release = damon_sysfs_context_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_context_groups,
};

/*
 * contexts directory
 */

struct damon_sysfs_contexts {
	struct kobject kobj;
	struct damon_sysfs_context **contexts_arr;
	int nr;
};

static struct damon_sysfs_contexts *damon_sysfs_contexts_alloc(void)
{
	return kzalloc(sizeof(struct damon_sysfs_contexts), GFP_KERNEL);
}

static void damon_sysfs_contexts_rm_dirs(struct damon_sysfs_contexts *contexts)
{
	struct damon_sysfs_context **contexts_arr = contexts->contexts_arr;
	int i;

	for (i = 0; i < contexts->nr; i++) {
		damon_sysfs_context_rm_dirs(contexts_arr[i]);
		kobject_put(&contexts_arr[i]->kobj);
	}
	kfree(contexts_arr);
	contexts->contexts_arr = NULL;
	contexts->nr = 0;
}

static int damon_sysfs_contexts_add_dirs(struct damon_sysfs_contexts *contexts,
		int nr_contexts)
{
	struct damon_sysfs_context **contexts_arr, *context;
	int err, i;

	damon_sysfs_contexts_rm_dirs(contexts);
	if (!nr_contexts)
		return 0;

	contexts_arr = kmalloc_array(nr_contexts, sizeof(*contexts_arr),
			GFP_KERNEL);
	if (!contexts_arr)
		return -ENOMEM;
	contexts->contexts_arr = contexts_arr;

	for (i = 0; i < nr_contexts; i++) {
		context = damon_sysfs_context_alloc(DAMON_OPS_VADDR);
		if (!context) {
			damon_sysfs_contexts_rm_dirs(contexts);
			return -ENOMEM;
		}

		err = kobject_init_and_add(&context->kobj,
				&damon_sysfs_context_ktype, &contexts->kobj,
				"%d", i);
		if (err) {
			kfree(context);
			damon_sysfs_contexts_rm_dirs(contexts);
			return err;
		}

		err = damon_sysfs_context_add_dirs(context);
		if (err) {
			kobject_put(&context->kobj);
			damon_sysfs_contexts_rm_dirs(contexts);
			return err;
		}

		contexts_arr[i] = context;
		contexts->nr++;
	}
	return 0;
}

static ssize_t damon_sysfs_contexts_nr_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_contexts *contexts = container_of(kobj,
			struct damon_sysfs_contexts, kobj);

	return sysfs_emit(buf, "%d\n", contexts->nr);
}

static ssize_t damon_sysfs_contexts_nr_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_contexts *contexts = container_of(kobj,
			struct damon_sysfs_contexts, kobj);
	int nr, err;

	err = kstrtoint(buf, 0, &nr);
	if (err)
		return err;
	if (nr < 0 || 1 < nr)
		return -EINVAL;

	if (!mutex_trylock(&damon_sysfs_lock))
		return -EBUSY;
	err = damon_sysfs_contexts_add_dirs(contexts, nr);
	mutex_unlock(&damon_sysfs_lock);
	if (err)
		return err;

	return count;
}

static void damon_sysfs_contexts_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_contexts, kobj));
}

static struct kobj_attribute damon_sysfs_contexts_nr_attr = __ATTR(nr, 0600,
		damon_sysfs_contexts_nr_show, damon_sysfs_contexts_nr_store);

static struct attribute *damon_sysfs_contexts_attrs[] = {
	&damon_sysfs_contexts_nr_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_contexts);

static struct kobj_type damon_sysfs_contexts_ktype = {
	.release = damon_sysfs_contexts_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_contexts_groups,
};

/*
 * kdamond directory
 */

struct damon_sysfs_kdamond {
	struct kobject kobj;
	struct damon_sysfs_contexts *contexts;
	int pid;
	struct damon_ctx *damon_ctx;
};

static struct damon_sysfs_kdamond *damon_sysfs_kdamond_alloc(void)
{
	return kzalloc(sizeof(struct damon_sysfs_kdamond), GFP_KERNEL);
}

static int damon_sysfs_kdamond_add_dirs(struct damon_sysfs_kdamond *kdamond)
{
	struct damon_sysfs_contexts *contexts;
	int err;

	contexts = damon_sysfs_contexts_alloc();
	if (!contexts)
		return -ENOMEM;

	err = kobject_init_and_add(&contexts->kobj,
			&damon_sysfs_contexts_ktype, &kdamond->kobj,
			"contexts");
	if (err) {
		kfree(contexts);
		return err;
	}
	kdamond->contexts = contexts;

	return err;
}

static void damon_sysfs_kdamond_rm_dirs(struct damon_sysfs_kdamond *kdamond)
{
	damon_sysfs_contexts_rm_dirs(kdamond->contexts);
	kobject_put(&kdamond->contexts->kobj);
}

static bool damon_sysfs_ctx_running(struct damon_ctx *ctx)
{
	bool running;

	mutex_lock(&ctx->kdamond_lock);
	running = ctx->kdamond != NULL;
	mutex_unlock(&ctx->kdamond_lock);
	return running;
}

static ssize_t damon_sysfs_kdamond_state_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_kdamond *kdamond = container_of(kobj,
			struct damon_sysfs_kdamond, kobj);
	struct damon_ctx *ctx = kdamond->damon_ctx;
	bool running;

	if (!ctx)
		running = false;
	else
		running = damon_sysfs_ctx_running(ctx);

	return sysfs_emit(buf, "%s\n", running ? "on" : "off");
}

static void damon_sysfs_destroy_targets(struct damon_ctx *ctx)
{
	struct damon_target *t, *next;

	damon_for_each_target_safe(t, next, ctx) {
		if (ctx->ops.id == DAMON_OPS_VADDR)
			put_pid(t->pid);
		damon_destroy_target(t);
	}
}

static int damon_sysfs_set_targets(struct damon_ctx *ctx,
		struct damon_sysfs_context *sysfs_ctx)
{
	struct damon_sysfs_targets *targets = sysfs_ctx->targets;
	int i;

	for (i = 0; i < targets->nr_targets; i++) {
		struct damon_target *t;

		t = damon_new_target();
		if (!t) {
			damon_sysfs_destroy_targets(ctx);
			return -ENOMEM;
		}
		if (ctx->ops.id == DAMON_OPS_VADDR) {
			t->pid = find_get_pid(targets->targets_arr[i]->pid);
			if (!t->pid) {
				damon_sysfs_destroy_targets(ctx);
				return -EINVAL;
			}
		}
		damon_add_target(ctx, t);
	}
	return 0;
}

static inline bool target_has_pid(const struct damon_ctx *ctx)
{
	return ctx->ops.id == DAMON_OPS_VADDR;
}

static void damon_sysfs_before_terminate(struct damon_ctx *ctx)
{
	struct damon_target *t, *next;

	if (!target_has_pid(ctx))
		return;

	mutex_lock(&ctx->kdamond_lock);
	damon_for_each_target_safe(t, next, ctx) {
		put_pid(t->pid);
		damon_destroy_target(t);
	}
	mutex_unlock(&ctx->kdamond_lock);
}

static struct damon_ctx *damon_sysfs_build_ctx(
		struct damon_sysfs_context *sys_ctx)
{
	struct damon_ctx *ctx = damon_new_ctx();
	struct damon_sysfs_attrs *sys_attrs = sys_ctx->attrs;
	struct damon_sysfs_ul_range *sys_nr_regions = sys_attrs->nr_regions;
	struct damon_sysfs_intervals *sys_intervals = sys_attrs->intervals;
	int err;

	if (!ctx)
		return ERR_PTR(-ENOMEM);

	err = damon_select_ops(ctx, sys_ctx->ops_id);
	if (err)
		goto out;

	err = damon_set_attrs(ctx, sys_intervals->sample_us,
			sys_intervals->aggr_us, sys_intervals->update_us,
			sys_nr_regions->min, sys_nr_regions->max);
	if (err)
		goto out;
	err = damon_sysfs_set_targets(ctx, sys_ctx);
	if (err)
		goto out;
	ctx->callback.before_terminate = damon_sysfs_before_terminate;
	return ctx;

out:
	damon_destroy_ctx(ctx);
	return ERR_PTR(err);
}

static ssize_t damon_sysfs_kdamond_state_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_kdamond *kdamond = container_of(kobj,
			struct damon_sysfs_kdamond, kobj);
	struct damon_ctx *ctx;
	ssize_t ret;

	if (!mutex_trylock(&damon_sysfs_lock))
		return -EBUSY;
	if (!strncmp(buf, "on\n", count)) {
		if (kdamond->damon_ctx &&
				damon_sysfs_ctx_running(kdamond->damon_ctx)) {
			ret = -EBUSY;
			goto out;
		}
		if (kdamond->contexts->nr != 1) {
			ret = -EINVAL;
			goto out;
		}

		if (kdamond->damon_ctx)
			damon_destroy_ctx(kdamond->damon_ctx);
		kdamond->damon_ctx = NULL;

		ctx = damon_sysfs_build_ctx(
				kdamond->contexts->contexts_arr[0]);
		if (IS_ERR(ctx)) {
			ret = PTR_ERR(ctx);
			goto out;
		}
		ret = damon_start(&ctx, 1, false);
		if (ret) {
			damon_destroy_ctx(ctx);
			goto out;
		}
		kdamond->damon_ctx = ctx;
	} else if (!strncmp(buf, "off\n", count)) {
		if (!kdamond->damon_ctx) {
			ret = -EINVAL;
			goto out;
		}
		ret = damon_stop(&kdamond->damon_ctx, 1);
		/*
		 * kdamond->damon_ctx will be freed in next on, or
		 * kdamonds_nr_store()
		 */
	} else {
		ret = -EINVAL;
	}
out:
	mutex_unlock(&damon_sysfs_lock);
	if (!ret)
		ret = count;
	return ret;
}

static ssize_t damon_sysfs_kdamond_pid_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_kdamond *kdamond = container_of(kobj,
			struct damon_sysfs_kdamond, kobj);
	struct damon_ctx *ctx;
	int pid;

	if (!mutex_trylock(&damon_sysfs_lock))
		return -EBUSY;
	ctx = kdamond->damon_ctx;
	if (!ctx) {
		pid = -1;
		goto out;
	}
	mutex_lock(&ctx->kdamond_lock);
	if (!ctx->kdamond) {
		pid = -1;
		goto unlock_kdamond_lock_out;
	}
	pid = ctx->kdamond->pid;

unlock_kdamond_lock_out:
	mutex_unlock(&ctx->kdamond_lock);
out:
	mutex_unlock(&damon_sysfs_lock);
	return sysfs_emit(buf, "%d\n", pid);
}

static void damon_sysfs_kdamond_release(struct kobject *kobj)
{
	struct damon_sysfs_kdamond *kdamond = container_of(kobj,
			struct damon_sysfs_kdamond, kobj);

	if (kdamond->damon_ctx)
		damon_destroy_ctx(kdamond->damon_ctx);
	kfree(container_of(kobj, struct damon_sysfs_kdamond, kobj));
}

static struct kobj_attribute damon_sysfs_kdamond_state_attr =
	__ATTR(state, 0600, damon_sysfs_kdamond_state_show,
		damon_sysfs_kdamond_state_store);

static struct kobj_attribute damon_sysfs_kdamond_pid_attr = __ATTR(pid, 0400,
		damon_sysfs_kdamond_pid_show, NULL);

static struct attribute *damon_sysfs_kdamond_attrs[] = {
	&damon_sysfs_kdamond_state_attr.attr,
	&damon_sysfs_kdamond_pid_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_kdamond);

static struct kobj_type damon_sysfs_kdamond_ktype = {
	.release = damon_sysfs_kdamond_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_kdamond_groups,
};

/*
 * kdamonds directory
 */

struct damon_sysfs_kdamonds {
	struct kobject kobj;
	struct damon_sysfs_kdamond **kdamonds_arr;
	int nr;
};

static struct damon_sysfs_kdamonds *damon_sysfs_kdamonds_alloc(void)
{
	return kzalloc(sizeof(struct damon_sysfs_kdamonds), GFP_KERNEL);
}

static void damon_sysfs_kdamonds_rm_dirs(struct damon_sysfs_kdamonds *kdamonds)
{
	struct damon_sysfs_kdamond **kdamonds_arr = kdamonds->kdamonds_arr;
	int i;

	for (i = 0; i < kdamonds->nr; i++) {
		damon_sysfs_kdamond_rm_dirs(kdamonds_arr[i]);
		kobject_put(&kdamonds_arr[i]->kobj);
	}
	kfree(kdamonds_arr);
	kdamonds->kdamonds_arr = NULL;
	kdamonds->nr = 0;
}

int damon_sysfs_nr_running_ctxs(struct damon_sysfs_kdamond **kdamonds,
		int nr_kdamonds)
{
	int nr_running_ctxs = 0;
	int i;

	for (i = 0; i < nr_kdamonds; i++) {
		struct damon_ctx *ctx;

		ctx = kdamonds[i]->damon_ctx;
		if (!ctx)
			continue;
		mutex_lock(&ctx->kdamond_lock);
		if (ctx->kdamond)
			nr_running_ctxs++;
		mutex_unlock(&ctx->kdamond_lock);
	}
	return nr_running_ctxs;
}

static int damon_sysfs_kdamonds_add_dirs(struct damon_sysfs_kdamonds *kdamonds,
		int nr_kdamonds)
{
	struct damon_sysfs_kdamond **kdamonds_arr, *kdamond;
	int err, i;

	if (damon_sysfs_nr_running_ctxs(kdamonds->kdamonds_arr, kdamonds->nr))
		return -EBUSY;

	damon_sysfs_kdamonds_rm_dirs(kdamonds);
	if (!nr_kdamonds)
		return 0;

	kdamonds_arr = kmalloc_array(nr_kdamonds, sizeof(*kdamonds_arr),
			GFP_KERNEL);
	if (!kdamonds_arr)
		return -ENOMEM;
	kdamonds->kdamonds_arr = kdamonds_arr;

	for (i = 0; i < nr_kdamonds; i++) {
		kdamond = damon_sysfs_kdamond_alloc();
		if (!kdamond) {
			damon_sysfs_kdamonds_rm_dirs(kdamonds);
			return -ENOMEM;
		}

		err = kobject_init_and_add(&kdamond->kobj,
				&damon_sysfs_kdamond_ktype, &kdamonds->kobj,
				"%d", i);
		if (err) {
			damon_sysfs_kdamonds_rm_dirs(kdamonds);
			kfree(kdamond);
			return err;
		}

		err = damon_sysfs_kdamond_add_dirs(kdamond);
		if (err) {
			kobject_put(&kdamond->kobj);
			damon_sysfs_kdamonds_rm_dirs(kdamonds);
			return err;
		}

		kdamonds_arr[i] = kdamond;
		kdamonds->nr++;
	}
	return 0;
}

static ssize_t damon_sysfs_kdamonds_nr_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct damon_sysfs_kdamonds *kdamonds = container_of(kobj,
			struct damon_sysfs_kdamonds, kobj);

	return sysfs_emit(buf, "%d\n", kdamonds->nr);
}

static ssize_t damon_sysfs_kdamonds_nr_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	struct damon_sysfs_kdamonds *kdamonds = container_of(kobj,
			struct damon_sysfs_kdamonds, kobj);
	int nr, err;

	err = kstrtoint(buf, 0, &nr);
	if (err)
		return err;
	if (nr < 0)
		return -EINVAL;

	if (!mutex_trylock(&damon_sysfs_lock))
		return -EBUSY;
	err = damon_sysfs_kdamonds_add_dirs(kdamonds, nr);
	mutex_unlock(&damon_sysfs_lock);
	if (err)
		return err;

	return count;
}

static void damon_sysfs_kdamonds_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_kdamonds, kobj));
}

static struct kobj_attribute damon_sysfs_kdamonds_nr_attr = __ATTR(nr, 0600,
		damon_sysfs_kdamonds_nr_show, damon_sysfs_kdamonds_nr_store);

static struct attribute *damon_sysfs_kdamonds_attrs[] = {
	&damon_sysfs_kdamonds_nr_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_kdamonds);

static struct kobj_type damon_sysfs_kdamonds_ktype = {
	.release = damon_sysfs_kdamonds_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_kdamonds_groups,
};

/*
 * damon user interface directory
 */

struct damon_sysfs_ui_dir {
	struct kobject kobj;
	struct damon_sysfs_kdamonds *kdamonds;
};

static struct damon_sysfs_ui_dir *damon_sysfs_ui_dir_alloc(void)
{
	return kzalloc(sizeof(struct damon_sysfs_ui_dir), GFP_KERNEL);
}

static int damon_sysfs_ui_dir_add_dirs(struct damon_sysfs_ui_dir *ui_dir)
{
	struct damon_sysfs_kdamonds *kdamonds;
	int err;

	kdamonds = damon_sysfs_kdamonds_alloc();
	if (!kdamonds)
		return -ENOMEM;

	err = kobject_init_and_add(&kdamonds->kobj,
			&damon_sysfs_kdamonds_ktype, &ui_dir->kobj,
			"kdamonds");
	if (err) {
		kfree(kdamonds);
		return err;
	}
	ui_dir->kdamonds = kdamonds;
	return err;
}

static void damon_sysfs_ui_dir_release(struct kobject *kobj)
{
	kfree(container_of(kobj, struct damon_sysfs_ui_dir, kobj));
}

static struct attribute *damon_sysfs_ui_dir_attrs[] = {
	NULL,
};
ATTRIBUTE_GROUPS(damon_sysfs_ui_dir);

static struct kobj_type damon_sysfs_ui_dir_ktype = {
	.release = damon_sysfs_ui_dir_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = damon_sysfs_ui_dir_groups,
};

static int __init damon_sysfs_init(void)
{
	struct kobject *damon_sysfs_root;
	struct damon_sysfs_ui_dir *admin;
	int err;

	damon_sysfs_root = kobject_create_and_add("damon", mm_kobj);
	if (!damon_sysfs_root)
		return -ENOMEM;

	admin = damon_sysfs_ui_dir_alloc();
	if (!admin) {
		kobject_put(damon_sysfs_root);
		return -ENOMEM;
	}
	err = kobject_init_and_add(&admin->kobj, &damon_sysfs_ui_dir_ktype,
			damon_sysfs_root, "admin");
	if (err) {
		kobject_put(&admin->kobj);
		kobject_put(damon_sysfs_root);
		return err;
	}

	err = damon_sysfs_ui_dir_add_dirs(admin);
	if (err) {
		kobject_put(&admin->kobj);
		kobject_put(damon_sysfs_root);
		return err;
	}

	return 0;
}
subsys_initcall(damon_sysfs_init);
