/* SPDX-License-Identifier: MIT */

/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef I915_SW_FENCE_WORK_H
#define I915_SW_FENCE_WORK_H

#include <linux/dma-fence.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

#include "i915_sw_fence.h"

struct dma_fence_work;
struct dma_fence_work_timeline;

/**
 * struct dma_fence_work_timeline_ops - Timeline operations struct
 * @name: Timeline ops name. This field is used if the timeline itself has
 * a NULL name. Can be set to NULL in which case a default name is used.
 *
 * The struct dma_fence_work_timeline is intended to be embeddable.
 * We use the ops to get and put the parent structure.
 */
struct dma_fence_work_timeline_ops {
	/**
	 * Timeline ops name. Used if the timeline itself has no name.
	 */
	const char *name;

	/**
	 * put() - Put the structure embedding the timeline
	 * @tl: The timeline
	 */
	void (*put)(struct dma_fence_work_timeline *tl);

	/**
	 * get() - Get the structure embedding the timeline
	 * @tl: The timeline
	 */
	void (*get)(struct dma_fence_work_timeline *tl);
};

/**
 * struct dma_fence_work_timeline - Simple timeline struct for dma_fence_work
 * @name: The name of the timeline. May be set to NULL. Immutable
 * @lock: Protects mutable members of the structure.
 * @context: The timeline fence context. Immutable.
 * @seqno: The previous seqno used. Protected by @lock.
 * @last_fence : The previous fence of the timeline. Protected by @lock.
 * @ops: The timeline operations struct. Immutable.
 */
struct dma_fence_work_timeline {
	const char *name;
	/** Protects mutable members of the structure */
	spinlock_t lock;
	u64 context;
	u64 seqno;
	struct dma_fence *last_fence;
	const struct dma_fence_work_timeline_ops *ops;
};

struct dma_fence_work_ops {
	const char *name;
	void (*work)(struct dma_fence_work *f);
	void (*release)(struct dma_fence_work *f);
};

struct dma_fence_work {
	struct dma_fence dma;
	spinlock_t lock;
	int error;

	struct i915_sw_fence chain;
	struct i915_sw_dma_fence_cb cb;

	struct work_struct work;

	struct dma_fence_work_timeline *tl;

	const struct dma_fence_work_ops *ops;
};

enum {
	DMA_FENCE_WORK_IMM = DMA_FENCE_FLAG_USER_BITS,
};

void dma_fence_work_init(struct dma_fence_work *f,
			 const struct dma_fence_work_ops *ops);
int dma_fence_work_chain(struct dma_fence_work *f, struct dma_fence *signal);

static inline void dma_fence_work_commit(struct dma_fence_work *f)
{
	i915_sw_fence_commit(&f->chain);
}

/**
 * dma_fence_work_commit_imm: Commit the fence, and if possible execute locally.
 * @f: the fenced worker
 *
 * Instead of always scheduling a worker to execute the callback (see
 * dma_fence_work_commit()), we try to execute the callback immediately in
 * the local context. It is required that the fence be committed before it
 * is published, and that no other threads try to tamper with the number
 * of asynchronous waits on the fence (or else the callback will be
 * executed in the wrong context, i.e. not the callers).
 */
static inline void dma_fence_work_commit_imm(struct dma_fence_work *f)
{
	if (atomic_read(&f->chain.pending) <= 1)
		__set_bit(DMA_FENCE_WORK_IMM, &f->dma.flags);

	dma_fence_work_commit(f);
}

void dma_fence_work_timeline_attach(struct dma_fence_work_timeline *tl,
				    struct dma_fence_work *f,
				    struct i915_sw_dma_fence_cb *tl_cb);

void dma_fence_work_timeline_init(struct dma_fence_work_timeline *tl,
				  const char *name,
				  const struct dma_fence_work_timeline_ops *ops);

#endif /* I915_SW_FENCE_WORK_H */
