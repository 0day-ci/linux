// SPDX-License-Identifier: MIT

/*
 * Copyright © 2019 Intel Corporation
 */

#include "i915_sw_fence_work.h"
#include "i915_utils.h"

/**
 * dma_fence_work_timeline_attach - Attach a struct dma_fence_work to a
 * timeline.
 * @tl: The timeline to attach to.
 * @f: The struct dma_fence_work.
 * @tl_cb: The i915_sw_dma_fence_cb needed to attach to the
 * timeline. This is typically embedded into the structure that also
 * embeds the struct dma_fence_work.
 *
 * This function takes a timeline reference and associates it with the
 * struct dma_fence_work. That reference is given up when the fence
 * signals. Furthermore it assigns a fence context and a seqno to the
 * dma-fence, and then chains upon the previous fence of the timeline
 * if any, to make sure that the fence signals after that fence. The
 * @tl_cb callback structure is needed for that chaining. Finally
 * the registered last fence of the timeline is replaced by this fence, and
 * the timeline takes a reference on the fence, which is released when
 * the fence signals.
 */
void dma_fence_work_timeline_attach(struct dma_fence_work_timeline *tl,
				    struct dma_fence_work *f,
				    struct i915_sw_dma_fence_cb *tl_cb)
{
	struct dma_fence *await;

	might_sleep();
	if (tl->ops->get)
		tl->ops->get(tl);

	spin_lock_irq(&tl->lock);
	await = tl->last_fence;
	tl->last_fence = dma_fence_get(&f->dma);
	f->dma.seqno = tl->seqno++;
	f->dma.context = tl->context;
	f->tl = tl;
	spin_unlock_irq(&tl->lock);

	if (await) {
		__i915_sw_fence_await_dma_fence(&f->chain, await, tl_cb);
		dma_fence_put(await);
	}
}

static void dma_fence_work_timeline_detach(struct dma_fence_work *f)
{
	struct dma_fence_work_timeline *tl = f->tl;
	bool put = false;
	unsigned long irq_flags;

	spin_lock_irqsave(&tl->lock, irq_flags);
	if (tl->last_fence == &f->dma) {
		put = true;
		tl->last_fence = NULL;
	}
	spin_unlock_irqrestore(&tl->lock, irq_flags);
	if (tl->ops->put)
		tl->ops->put(tl);
	if (put)
		dma_fence_put(&f->dma);
}

static void dma_fence_work_complete(struct dma_fence_work *f)
{
	if (f->ops->release)
		f->ops->release(f);

	if (f->tl)
		dma_fence_work_timeline_detach(f);

	dma_fence_put(&f->dma);
}

static void dma_fence_work_irq_work(struct irq_work *irq_work)
{
	struct dma_fence_work *f = container_of(irq_work, typeof(*f), irq_work);

	dma_fence_signal(&f->dma);
	if (f->ops->release)
		/* Note we take the signaled path in dma_fence_work_work() */
		queue_work(system_unbound_wq, &f->work);
	else
		dma_fence_work_complete(f);
}

static void dma_fence_work_work(struct work_struct *work)
{
	struct dma_fence_work *f = container_of(work, typeof(*f), work);

	if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &f->dma.flags)) {
		dma_fence_work_complete(f);
		return;
	}

	if (f->ops->work)
		f->ops->work(f);

	dma_fence_signal(&f->dma);

	dma_fence_work_complete(f);
}

static int __i915_sw_fence_call
fence_notify(struct i915_sw_fence *fence, enum i915_sw_fence_notify state)
{
	struct dma_fence_work *f = container_of(fence, typeof(*f), chain);

	switch (state) {
	case FENCE_COMPLETE:
		if (fence->error)
			cmpxchg(&f->error, 0, fence->error);

		dma_fence_get(&f->dma);
		if (test_bit(DMA_FENCE_WORK_IMM, &f->dma.flags))
			dma_fence_work_work(&f->work);
		else if (f->ops->work)
			queue_work(system_unbound_wq, &f->work);
		else
			irq_work_queue(&f->irq_work);
		break;

	case FENCE_FREE:
		dma_fence_put(&f->dma);
		break;
	}

	return NOTIFY_DONE;
}

static const char *get_driver_name(struct dma_fence *fence)
{
	struct dma_fence_work *f = container_of(fence, typeof(*f), dma);

	return (f->tl && f->tl->ops->name) ? f->tl->ops->name : "dma-fence";
}

static const char *get_timeline_name(struct dma_fence *fence)
{
	struct dma_fence_work *f = container_of(fence, typeof(*f), dma);

	return (f->tl && f->tl->name) ? f->tl->name :
		f->ops->name ?: "work";
}

static void fence_release(struct dma_fence *fence)
{
	struct dma_fence_work *f = container_of(fence, typeof(*f), dma);

	i915_sw_fence_fini(&f->chain);

	BUILD_BUG_ON(offsetof(typeof(*f), dma));
	dma_fence_free(&f->dma);
}

static const struct dma_fence_ops fence_ops = {
	.get_driver_name = get_driver_name,
	.get_timeline_name = get_timeline_name,
	.release = fence_release,
};

void dma_fence_work_init(struct dma_fence_work *f,
			 const struct dma_fence_work_ops *ops)
{
	f->ops = ops;
	f->error = 0;
	f->tl = NULL;
	spin_lock_init(&f->lock);
	dma_fence_init(&f->dma, &fence_ops, &f->lock, 0, 0);
	i915_sw_fence_init(&f->chain, fence_notify);
	INIT_WORK(&f->work, dma_fence_work_work);
	init_irq_work(&f->irq_work, dma_fence_work_irq_work);
}

int dma_fence_work_chain(struct dma_fence_work *f, struct dma_fence *signal)
{
	if (!signal)
		return 0;

	return __i915_sw_fence_await_dma_fence(&f->chain, signal, &f->cb);
}

/**
 * dma_fence_work_timeline_init - Initialize a dma_fence_work timeline
 * @tl: The timeline to initialize,
 * @name: The name of the timeline,
 * @ops: The timeline operations.
 */
void dma_fence_work_timeline_init(struct dma_fence_work_timeline *tl,
				  const char *name,
				  const struct dma_fence_work_timeline_ops *ops)
{
	tl->name = name;
	spin_lock_init(&tl->lock);
	tl->context = dma_fence_context_alloc(1);
	tl->seqno = 0;
	tl->last_fence = NULL;
	tl->ops = ops;
}
