// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Facebook
 * Copyright 2020 Google LLC.
 */

#include <linux/pid.h>
#include <linux/sched.h>
#include <linux/rculist.h>
#include <linux/list.h>
#include <linux/hash.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/bpf.h>
#include <linux/bpf_local_storage.h>
#include <linux/filter.h>
#include <uapi/linux/btf.h>
#include <linux/btf_ids.h>
#include <linux/fdtable.h>
#include <linux/sched/threadgroup_rwsem.h>

DEFINE_BPF_STORAGE_CACHE(task_cache);

static DEFINE_PER_CPU(int, bpf_task_storage_busy);

/* Protected by threadgroup_rwsem. */
static LIST_HEAD(prealloc_smaps);

static void bpf_task_storage_lock(void)
{
	migrate_disable();
	__this_cpu_inc(bpf_task_storage_busy);
}

static void bpf_task_storage_unlock(void)
{
	__this_cpu_dec(bpf_task_storage_busy);
	migrate_enable();
}

static bool bpf_task_storage_trylock(void)
{
	migrate_disable();
	if (unlikely(__this_cpu_inc_return(bpf_task_storage_busy) != 1)) {
		__this_cpu_dec(bpf_task_storage_busy);
		migrate_enable();
		return false;
	}
	return true;
}

static struct bpf_local_storage __rcu **task_storage_ptr(void *owner)
{
	struct task_struct *task = owner;

	return &task->bpf_storage;
}

static struct bpf_local_storage_data *
task_storage_lookup(struct task_struct *task, struct bpf_map *map,
		    bool cacheit_lockit)
{
	struct bpf_local_storage *task_storage;
	struct bpf_local_storage_map *smap;

	task_storage = rcu_dereference(task->bpf_storage);
	if (!task_storage)
		return NULL;

	smap = (struct bpf_local_storage_map *)map;
	return bpf_local_storage_lookup(task_storage, smap, cacheit_lockit);
}

void bpf_task_storage_free(struct task_struct *task)
{
	struct bpf_local_storage_elem *selem;
	struct bpf_local_storage *local_storage;
	bool free_task_storage = false;
	struct hlist_node *n;
	unsigned long flags;

	rcu_read_lock();

	local_storage = rcu_dereference(task->bpf_storage);
	if (!local_storage) {
		rcu_read_unlock();
		return;
	}

	/* Neither the bpf_prog nor the bpf-map's syscall
	 * could be modifying the local_storage->list now.
	 * Thus, no elem can be added-to or deleted-from the
	 * local_storage->list by the bpf_prog or by the bpf-map's syscall.
	 *
	 * It is racing with bpf_local_storage_map_free() alone
	 * when unlinking elem from the local_storage->list and
	 * the map's bucket->list.
	 */
	bpf_task_storage_lock();
	raw_spin_lock_irqsave(&local_storage->lock, flags);
	hlist_for_each_entry_safe(selem, n, &local_storage->list, snode) {
		/* Always unlink from map before unlinking from
		 * local_storage.
		 */
		bpf_selem_unlink_map(selem);
		free_task_storage = bpf_selem_unlink_storage_nolock(
			local_storage, selem, false);
	}
	raw_spin_unlock_irqrestore(&local_storage->lock, flags);
	bpf_task_storage_unlock();
	rcu_read_unlock();

	/* free_task_storage should always be true as long as
	 * local_storage->list was non-empty.
	 */
	if (free_task_storage)
		kfree_rcu(local_storage, rcu);
}

static void *bpf_pid_task_storage_lookup_elem(struct bpf_map *map, void *key)
{
	struct bpf_local_storage_data *sdata;
	struct task_struct *task;
	unsigned int f_flags;
	struct pid *pid;
	int fd, err;

	fd = *(int *)key;
	pid = pidfd_get_pid(fd, &f_flags);
	if (IS_ERR(pid))
		return ERR_CAST(pid);

	/* We should be in an RCU read side critical section, it should be safe
	 * to call pid_task.
	 */
	WARN_ON_ONCE(!rcu_read_lock_held());
	task = pid_task(pid, PIDTYPE_PID);
	if (!task) {
		err = -ENOENT;
		goto out;
	}

	bpf_task_storage_lock();
	sdata = task_storage_lookup(task, map, true);
	bpf_task_storage_unlock();
	put_pid(pid);
	return sdata ? sdata->data : NULL;
out:
	put_pid(pid);
	return ERR_PTR(err);
}

static int bpf_pid_task_storage_update_elem(struct bpf_map *map, void *key,
					    void *value, u64 map_flags)
{
	struct bpf_local_storage_data *sdata;
	struct task_struct *task;
	unsigned int f_flags;
	struct pid *pid;
	int fd, err;

	fd = *(int *)key;
	pid = pidfd_get_pid(fd, &f_flags);
	if (IS_ERR(pid))
		return PTR_ERR(pid);

	/* We should be in an RCU read side critical section, it should be safe
	 * to call pid_task.
	 */
	WARN_ON_ONCE(!rcu_read_lock_held());
	task = pid_task(pid, PIDTYPE_PID);
	if (!task) {
		err = -ENOENT;
		goto out;
	}

	bpf_task_storage_lock();
	sdata = bpf_local_storage_update(
		task, (struct bpf_local_storage_map *)map, value, map_flags);
	bpf_task_storage_unlock();

	err = PTR_ERR_OR_ZERO(sdata);
out:
	put_pid(pid);
	return err;
}

static int task_storage_delete(struct task_struct *task, struct bpf_map *map)
{
	struct bpf_local_storage_data *sdata;

	sdata = task_storage_lookup(task, map, false);
	if (!sdata)
		return -ENOENT;

	bpf_selem_unlink(SELEM(sdata));

	return 0;
}

static int bpf_pid_task_storage_delete_elem(struct bpf_map *map, void *key)
{
	struct task_struct *task;
	unsigned int f_flags;
	struct pid *pid;
	int fd, err;

	fd = *(int *)key;
	pid = pidfd_get_pid(fd, &f_flags);
	if (IS_ERR(pid))
		return PTR_ERR(pid);

	/* We should be in an RCU read side critical section, it should be safe
	 * to call pid_task.
	 */
	WARN_ON_ONCE(!rcu_read_lock_held());
	task = pid_task(pid, PIDTYPE_PID);
	if (!task) {
		err = -ENOENT;
		goto out;
	}

	bpf_task_storage_lock();
	err = task_storage_delete(task, map);
	bpf_task_storage_unlock();
out:
	put_pid(pid);
	return err;
}

BPF_CALL_4(bpf_task_storage_get, struct bpf_map *, map, struct task_struct *,
	   task, void *, value, u64, flags)
{
	struct bpf_local_storage_data *sdata;

	if (flags & ~(BPF_LOCAL_STORAGE_GET_F_CREATE))
		return (unsigned long)NULL;

	if (!task)
		return (unsigned long)NULL;

	if (!bpf_task_storage_trylock())
		return (unsigned long)NULL;

	sdata = task_storage_lookup(task, map, true);
	if (sdata)
		goto unlock;

	/* only allocate new storage, when the task is refcounted */
	if (refcount_read(&task->usage) &&
	    (flags & BPF_LOCAL_STORAGE_GET_F_CREATE))
		sdata = bpf_local_storage_update(
			task, (struct bpf_local_storage_map *)map, value,
			BPF_NOEXIST);

unlock:
	bpf_task_storage_unlock();
	return IS_ERR_OR_NULL(sdata) ? (unsigned long)NULL :
		(unsigned long)sdata->data;
}

BPF_CALL_2(bpf_task_storage_delete, struct bpf_map *, map, struct task_struct *,
	   task)
{
	int ret;

	if (!task)
		return -EINVAL;

	if (!bpf_task_storage_trylock())
		return -EBUSY;

	/* This helper must only be called from places where the lifetime of the task
	 * is guaranteed. Either by being refcounted or by being protected
	 * by an RCU read-side critical section.
	 */
	ret = task_storage_delete(task, map);
	bpf_task_storage_unlock();
	return ret;
}

static int notsupp_get_next_key(struct bpf_map *map, void *key, void *next_key)
{
	return -ENOTSUPP;
}

static int task_storage_map_populate(struct bpf_local_storage_map *smap)
{
	struct bpf_local_storage *storage = NULL;
	struct bpf_local_storage_elem *selem = NULL;
	struct task_struct *p, *g;
	int err = 0;

	lockdep_assert_held(&threadgroup_rwsem);
retry:
	if (!storage)
		storage = bpf_map_kzalloc(&smap->map, sizeof(*storage),
					  GFP_USER);
	if (!selem)
		selem = bpf_map_kzalloc(&smap->map, smap->elem_size, GFP_USER);
	if (!storage || !selem) {
		err = -ENOMEM;
		goto out_free;
	}

	rcu_read_lock();
	bpf_task_storage_lock();

	for_each_process_thread(g, p) {
		struct bpf_local_storage_data *sdata;

		/* Try inserting with atomic allocations. On failure, retry with
		 * the preallocated ones.
		 */
		sdata = bpf_local_storage_update(p, smap, NULL, BPF_NOEXIST);

		if (PTR_ERR(sdata) == -ENOMEM && storage && selem) {
			sdata = __bpf_local_storage_update(p, smap, NULL,
							   BPF_NOEXIST,
							   &storage, &selem);
		}

		/* Check -EEXIST before need_resched() to guarantee forward
		 * progress.
		 */
		if (PTR_ERR(sdata) == -EEXIST)
			continue;

		/* If requested or alloc failed, take a breather and loop back
		 * to preallocate.
		 */
		if (need_resched() ||
		    PTR_ERR(sdata) == -EAGAIN || PTR_ERR(sdata) == -ENOMEM) {
			bpf_task_storage_unlock();
			rcu_read_unlock();
			cond_resched();
			goto retry;
		}

		if (IS_ERR(sdata)) {
			err = PTR_ERR(sdata);
			goto out_unlock;
		}
	}
out_unlock:
	bpf_task_storage_unlock();
	rcu_read_unlock();
out_free:
	if (storage)
		kfree(storage);
	if (selem)
		kfree(selem);
	return err;
}

static struct bpf_map *task_storage_map_alloc(union bpf_attr *attr)
{
	struct bpf_local_storage_map *smap;
	int err;

	smap = bpf_local_storage_map_alloc(attr);
	if (IS_ERR(smap))
		return ERR_CAST(smap);

	if (!(attr->map_flags & BPF_F_NO_PREALLOC)) {
		/* We're going to exercise the regular update path to populate
		 * the map for the existing tasks, which will call into map ops
		 * which is normally initialized after this function returns.
		 * Initialize it early here.
		 */
		smap->map.ops = &task_storage_map_ops;

		percpu_down_write(&threadgroup_rwsem);
		list_add_tail(&smap->prealloc_node, &prealloc_smaps);
		err = task_storage_map_populate(smap);
		percpu_up_write(&threadgroup_rwsem);
		if (err) {
			bpf_local_storage_map_free(smap,
						   &bpf_task_storage_busy);
			return ERR_PTR(err);
		}
	}

	smap->cache_idx = bpf_local_storage_cache_idx_get(&task_cache);
	return &smap->map;
}

static void task_storage_map_free(struct bpf_map *map)
{
	struct bpf_local_storage_map *smap;

	smap = (struct bpf_local_storage_map *)map;
	bpf_local_storage_cache_idx_free(&task_cache, smap->cache_idx);

	if (!list_empty(&smap->prealloc_node)) {
		percpu_down_write(&threadgroup_rwsem);
		list_del_init(&smap->prealloc_node);
		percpu_up_write(&threadgroup_rwsem);
	}

	bpf_local_storage_map_free(smap, &bpf_task_storage_busy);
}

static int task_storage_map_btf_id;
const struct bpf_map_ops task_storage_map_ops = {
	.map_meta_equal = bpf_map_meta_equal,
	.map_alloc_check = bpf_local_storage_prealloc_map_alloc_check,
	.map_alloc = task_storage_map_alloc,
	.map_free = task_storage_map_free,
	.map_get_next_key = notsupp_get_next_key,
	.map_lookup_elem = bpf_pid_task_storage_lookup_elem,
	.map_update_elem = bpf_pid_task_storage_update_elem,
	.map_delete_elem = bpf_pid_task_storage_delete_elem,
	.map_check_btf = bpf_local_storage_map_check_btf,
	.map_btf_name = "bpf_local_storage_map",
	.map_btf_id = &task_storage_map_btf_id,
	.map_owner_storage_ptr = task_storage_ptr,
};

int bpf_task_storage_fork(struct task_struct *task)
{
	struct bpf_local_storage_map *smap;

	percpu_rwsem_assert_held(&threadgroup_rwsem);

	list_for_each_entry(smap, &prealloc_smaps, prealloc_node) {
		struct bpf_local_storage *storage;
		struct bpf_local_storage_elem *selem;
		struct bpf_local_storage_data *sdata;

		storage = bpf_map_kzalloc(&smap->map, sizeof(*storage),
					  GFP_USER);
		selem = bpf_map_kzalloc(&smap->map, smap->elem_size, GFP_USER);

		rcu_read_lock();
		bpf_task_storage_lock();
		sdata = __bpf_local_storage_update(task, smap, NULL, BPF_NOEXIST,
						   &storage, &selem);
		bpf_task_storage_unlock();
		rcu_read_unlock();

		if (storage)
			kfree(storage);
		if (selem)
			kfree(selem);

		if (IS_ERR(sdata)) {
			bpf_task_storage_free(task);
			return PTR_ERR(sdata);
		}
	}

	return 0;
}

const struct bpf_func_proto bpf_task_storage_get_proto = {
	.func = bpf_task_storage_get,
	.gpl_only = false,
	.ret_type = RET_PTR_TO_MAP_VALUE_OR_NULL,
	.arg1_type = ARG_CONST_MAP_PTR,
	.arg2_type = ARG_PTR_TO_BTF_ID,
	.arg2_btf_id = &btf_task_struct_ids[0],
	.arg3_type = ARG_PTR_TO_MAP_VALUE_OR_NULL,
	.arg4_type = ARG_ANYTHING,
};

const struct bpf_func_proto bpf_task_storage_delete_proto = {
	.func = bpf_task_storage_delete,
	.gpl_only = false,
	.ret_type = RET_INTEGER,
	.arg1_type = ARG_CONST_MAP_PTR,
	.arg2_type = ARG_PTR_TO_BTF_ID,
	.arg2_btf_id = &btf_task_struct_ids[0],
};
