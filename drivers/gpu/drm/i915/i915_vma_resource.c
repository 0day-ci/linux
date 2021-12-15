// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include <linux/interval_tree_generic.h>
#include <linux/slab.h>

#include "i915_sw_fence.h"
#include "i915_vma_resource.h"

#include "gt/intel_gtt.h"

enum {
	I915_VMA_RESOURCE_IMM = DMA_FENCE_FLAG_USER_BITS,
};

/**
 * DOC:
 * We use a per-vm interval tree to keep track of vma_resources
 * scheduled for unbind but not yet unbound. The tree is protected by
 * the vm mutex, and nodes are removed just after the unbind fence signals.
 * The removal takes the vm mutex from a kernel thread which we need to
 * keep in mind so that we don't grab the mutex and try to wait for all
 * pending unbinds to complete, because that will temporaryily block many
 * of the workqueue threads, and people will get angry.
 *
 * We should consider using a single ordered fence per VM instead but that
 * requires ordering the unbinds and might introduce unnecessary waiting
 * for unrelated unbinds. Amount of code will probably be roughly the same
 * due to the simplicity of using the interval tree interface.
 *
 * Another drawback of this interval tree is that the complexity of insertion
 * and removal of fences increases as O(ln(pending_unbinds)) instead of
 * O(1) for a single fence without interval tree.
 */
#define VMA_RES_START(_node) ((_node)->start)
#define VMA_RES_LAST(_node) ((_node)->start + (_node)->node_size - 1)
INTERVAL_TREE_DEFINE(struct i915_vma_resource, rb,
		     unsigned long, __subtree_last,
		     VMA_RES_START, VMA_RES_LAST, static, vma_res_itree);

/* Callbacks for the unbind dma-fence. */

static const char *get_driver_name(struct dma_fence *fence)
{
	return "vma unbind fence";
}

static const char *get_timeline_name(struct dma_fence *fence)
{
	return "unbound";
}

static void unbind_fence_release(struct dma_fence *fence)
{
	struct i915_vma_resource *vma_res =
		container_of(fence, typeof(*vma_res), unbind_fence);

	i915_sw_fence_fini(&vma_res->chain);

	BUILD_BUG_ON(offsetof(typeof(*vma_res), unbind_fence));
	dma_fence_free(fence);
}

static struct dma_fence_ops unbind_fence_ops = {
	.get_driver_name = get_driver_name,
	.get_timeline_name = get_timeline_name,
	.release = unbind_fence_release,
};

/**
 * i915_vma_resource_alloc - Allocate a vma resource
 *
 * Return: A pointer to a cleared struct i915_vma_resource or
 * a -ENOMEM error pointer if allocation fails.
 */
struct i915_vma_resource *i915_vma_resource_alloc(void)
{
	struct i915_vma_resource *vma_res =
		kzalloc(sizeof(*vma_res), GFP_KERNEL);

	return vma_res ? vma_res : ERR_PTR(-ENOMEM);
}

static void __i915_vma_resource_unhold(struct i915_vma_resource *vma_res)
{
	if (!refcount_dec_and_test(&vma_res->hold_count))
		return;

	dma_fence_signal(&vma_res->unbind_fence);
	vma_res->vm = NULL;
}

/**
 * i915_vma_resource_unhold - Unhold the signaling of the vma resource unbind
 * fence.
 * @vma_res: The vma resource.
 * @lockdep_cookie: The lockdep cookie returned from i915_vma_resource_hold.
 *
 * The function may leave a dma_fence critical section.
 */
void i915_vma_resource_unhold(struct i915_vma_resource *vma_res,
			      bool lockdep_cookie)
{
	dma_fence_end_signalling(lockdep_cookie);

	if (IS_ENABLED(CONFIG_PROVE_LOCKING)) {
		unsigned long irq_flags;

		/* Inefficient open-coded might_lock_irqsave() */
		spin_lock_irqsave(&vma_res->lock, irq_flags);
		spin_unlock_irqrestore(&vma_res->lock, irq_flags);
	}

	__i915_vma_resource_unhold(vma_res);
}

/**
 * i915_vma_resource_hold - Hold the signaling of the vma resource unbind fence.
 * @vma_res: The vma resource.
 * @lockdep_cookie: Pointer to a bool serving as a lockdep cooke that should
 * be given as an argument to the pairing i915_vma_resource_unhold.
 *
 * If returning true, the function enters a dma_fence signalling critical
 * section is not in one already.
 *
 * Return: true if holding successful, false if not.
 */
bool i915_vma_resource_hold(struct i915_vma_resource *vma_res,
			    bool *lockdep_cookie)
{
	bool held = refcount_inc_not_zero(&vma_res->hold_count);

	if (held)
		*lockdep_cookie = dma_fence_begin_signalling();

	return held;
}

static void i915_vma_resource_unbind_work(struct work_struct *work)
{
	struct i915_vma_resource *vma_res =
		container_of(work, typeof(*vma_res), work);
	struct i915_address_space *vm = vma_res->vm;

	vma_res->ops->unbind_vma(vm, vma_res);

	if (!RB_EMPTY_NODE(&vma_res->rb)) {
		mutex_lock(&vm->mutex);
		vma_res_itree_remove(vma_res, &vm->pending_unbind);
		mutex_unlock(&vm->mutex);
	}

	__i915_vma_resource_unhold(vma_res);
	i915_vma_resource_put(vma_res);
}

static int
i915_vma_resource_fence_notify(struct i915_sw_fence *fence,
			       enum i915_sw_fence_notify state)
{
	struct i915_vma_resource *vma_res =
		container_of(fence, typeof(*vma_res), chain);
	struct dma_fence *unbind_fence =
		&vma_res->unbind_fence;

	switch (state) {
	case FENCE_COMPLETE:
		dma_fence_get(unbind_fence);
		if (vma_res->immediate_unbind) {
			i915_vma_resource_unbind_work(&vma_res->work);
		} else {
			INIT_WORK(&vma_res->work, i915_vma_resource_unbind_work);
			queue_work(system_unbound_wq, &vma_res->work);
		}
		break;
	case FENCE_FREE:
		i915_vma_resource_put(vma_res);
		break;
	}

	return NOTIFY_DONE;
}

/**
 * i915_vma_resource_unbind - Unbind a vma resource
 * @vma_res: The vma resource to unbind.
 *
 * At this point this function does little more than publish a fence that
 * signals immediately unless signaling is held back.
 *
 * Return: A refcounted pointer to a dma-fence that signals when unbinding is
 * complete.
 */
struct dma_fence *i915_vma_resource_unbind(struct i915_vma_resource *vma_res)
{
	/* Reference for the returned fence. */
	i915_vma_resource_get(vma_res);
	/* Reference for the sw fence */
	i915_vma_resource_get(vma_res);

	if (atomic_read(&vma_res->chain.pending) <= 1) {
		RB_CLEAR_NODE(&vma_res->rb);
		vma_res->immediate_unbind = 1;
	} else {
		vma_res_itree_insert(vma_res, &vma_res->vm->pending_unbind);
	}

	i915_sw_fence_commit(&vma_res->chain);

	return &vma_res->unbind_fence;
}

/**
 * __i915_vma_resource_init - Initialize a vma resource.
 * @vma_res: The vma resource to initialize
 *
 * Initializes the private members of a vma resource.
 */
void __i915_vma_resource_init(struct i915_vma_resource *vma_res)
{
	spin_lock_init(&vma_res->lock);
	dma_fence_init(&vma_res->unbind_fence, &unbind_fence_ops,
		       &vma_res->lock, 0, 0);
	refcount_set(&vma_res->hold_count, 1);
	i915_sw_fence_init(&vma_res->chain, i915_vma_resource_fence_notify);
}

/**
 * i915_vma_resource_bind_dep_sync - Wait for / sync all unbinds touching a
 * certain vm range.
 * @vm: The vm to look at.
 * @offset: The range start.
 * @size: The range size.
 * @intr: Whether to wait interrubtible.
 *
 * The function needs to be called with the vm lock held.
 *
 * Return: Zero on success, -ERESTARTSYS if interrupted and @intr==true
 */
int i915_vma_resource_bind_dep_sync(struct i915_address_space *vm,
				    unsigned long offset,
				    unsigned long size,
				    bool intr)
{
	struct i915_vma_resource *node;
	unsigned long last = offset + size - 1;

	lockdep_assert_held(&vm->mutex);
	might_sleep();

	node = vma_res_itree_iter_first(&vm->pending_unbind, offset, last);
	while (node) {
		int ret = dma_fence_wait(&node->unbind_fence, intr);

		if (ret)
			return ret;

		node = vma_res_itree_iter_next(node, offset, last);
	}

	return 0;
}

/**
 * i915_vma_resource_bind_dep_sync_all - Wait for / sync all unbinds of a vm,
 * releasing the vm lock while waiting.
 * @vm: The vm to look at.
 *
 * The function may not be called with the vm lock held.
 * Typically this is called at vm destruction to finish any pending
 * unbind operations. The vm mutex is released while waiting to avoid
 * stalling kernel workqueues trying to grab the mutex.
 */
void i915_vma_resource_bind_dep_sync_all(struct i915_address_space *vm)
{
	struct i915_vma_resource *node;
	struct dma_fence *fence;

	do {
		fence = NULL;
		mutex_lock(&vm->mutex);
		node = vma_res_itree_iter_first(&vm->pending_unbind, 0,
						ULONG_MAX);
		if (node)
			fence = dma_fence_get_rcu(&node->unbind_fence);
		mutex_unlock(&vm->mutex);

		if (fence) {
			/*
			 * The wait makes sure the node eventually removes
			 * itself from the tree.
			 */
			dma_fence_wait(fence, false);
			dma_fence_put(fence);
		}
	} while (node);
}

/**
 * i915_vma_resource_bind_dep_await - Have a struct i915_sw_fence await all
 * pending unbinds in a certain range of a vm.
 * @vm: The vm to look at.
 * @sw_fence: The struct i915_sw_fence that will be awaiting the unbinds.
 * @offset: The range start.
 * @size: The range size.
 * @intr: Whether to wait interrubtible.
 * @gfp: Allocation mode for memory allocations.
 *
 * The function makes @sw_fence await all pending unbinds in a certain
 * vm range before calling the complete notifier. To be able to await
 * each individual unbind, the function needs to allocate memory using
 * the @gpf allocation mode. If that fails, the function will instead
 * wait for the unbind fence to signal, using @intr to judge whether to
 * wait interruptible or not. Note that @gfp should ideally be selected so
 * as to avoid any expensive memory allocation stalls and rather fail and
 * synchronize itself. For now the vm mutex is required when calling this
 * function with means that @gfp can't call into direct reclaim. In reality
 * this means that during heavy memory pressure, we will sync in this
 * function.
 *
 * Return: Zero on success, -ERESTARTSYS if interrupted and @intr==true
 */
int i915_vma_resource_bind_dep_await(struct i915_address_space *vm,
				     struct i915_sw_fence *sw_fence,
				     unsigned long offset,
				     unsigned long size,
				     bool intr,
				     gfp_t gfp)
{
	struct i915_vma_resource *node;
	unsigned long last = offset + size - 1;

	lockdep_assert_held(&vm->mutex);
	might_alloc(gfp);
	might_sleep();

	node = vma_res_itree_iter_first(&vm->pending_unbind, offset, last);
	while (node) {
		int ret;

		ret = i915_sw_fence_await_dma_fence(sw_fence,
						    &node->unbind_fence,
						    MAX_SCHEDULE_TIMEOUT, gfp);
		if (ret)
			ret = dma_fence_wait(&node->unbind_fence, intr);
		if (ret)
			return ret;

		node = vma_res_itree_iter_next(node, offset, last);
	}

	return 0;
}
