// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * Copyright(c) 2020 - 2021 Intel Corporation.
 */

#include <rdma/uverbs_std_types.h>
#include <rdma/uverbs_ioctl.h>

#include "rv.h"
#include "trace.h"

unsigned int enable_user_mr;

module_param(enable_user_mr, uint, 0444);
MODULE_PARM_DESC(enable_user_mr, "Enable user mode MR caching");

static void rv_handle_user_mrs_put(struct work_struct *work);

static bool rv_cache_mrc_filter(struct rv_mr_cached *mrc, u64 addr,
				u64 len, u32 acc);
static void rv_cache_mrc_get(struct rv_mr_cache *cache,
			     void *arg, struct rv_mr_cached *mrc);
static int rv_cache_mrc_put(struct rv_mr_cache *cache,
			    void *arg, struct rv_mr_cached *mrc);
static int rv_cache_mrc_invalidate(struct rv_mr_cache *cache,
				   void *arg, struct rv_mr_cached *mrc);
static int rv_cache_mrc_evict(struct rv_mr_cache *cache,
			      void *arg, struct rv_mr_cached *mrc,
			      void *evict_arg, bool *stop);

static const struct rv_mr_cache_ops rv_cache_ops = {
	.filter = rv_cache_mrc_filter,
	.get = rv_cache_mrc_get,
	.put = rv_cache_mrc_put,
	.invalidate = rv_cache_mrc_invalidate,
	.evict = rv_cache_mrc_evict
};

/* given an rv, find the proper ib_dev to use when registering user MRs */
static struct ib_device *rv_ib_dev(struct rv_user *rv)
{
	struct rv_device *dev = rv->rdma_mode == RV_RDMA_MODE_USER ? rv->dev :
				rv->jdev->dev;

	return dev->ib_dev;
}

/* caller must hold rv->mutex */
static int rv_drv_api_reg_mem(struct rv_user *rv,
			      struct rv_mem_params_in *minfo,
			      struct mr_info *mr)
{
	struct ib_mr *ib_mr;

	mr->ib_mr = NULL;
	mr->ib_pd = NULL;

	/*
	 * Check if the buffer is for kernel use. It should be noted that
	 * the ibv_pd_handle value "0" is a valid user space pd handle.
	 */
	if (minfo->access & IBV_ACCESS_KERNEL)
		ib_mr = rdma_reg_kernel_mr(minfo->cmd_fd_int, rv->jdev->pd,
					   minfo->addr, minfo->length,
					   minfo->access & ~IBV_ACCESS_KERNEL,
					   minfo->ulen, minfo->udata,
					   &mr->fd);
	else
		ib_mr = rdma_reg_user_mr(rv_ib_dev(rv), minfo->cmd_fd_int,
					 minfo->ibv_pd_handle,
					 minfo->addr, minfo->length,
					 minfo->access, minfo->ulen,
					 minfo->udata, &mr->fd);
	if (IS_ERR(ib_mr)) {
		rv_err(rv->inx, "reg_user_mr failed\n");
		return  PTR_ERR(ib_mr);
	}
	/* A hardware driver may not set the iova field */
	if (!ib_mr->iova)
		ib_mr->iova = minfo->addr;

	trace_rv_mr_info_reg(minfo->addr, minfo->length, minfo->access,
			     ib_mr->lkey, ib_mr->rkey, ib_mr->iova,
			     atomic_read(&ib_mr->pd->usecnt));
	mr->ib_mr = ib_mr;
	mr->ib_pd = ib_mr->pd;

	return 0;
}

int rv_drv_api_dereg_mem(struct mr_info *mr)
{
	int ret;
	struct rv_mr_cached *mrc = container_of(mr, struct rv_mr_cached, mr);

	trace_rv_mr_info_dereg(mrc->addr, mrc->len, mrc->access,
			       mr->ib_mr->lkey, mr->ib_mr->rkey,
			       mr->ib_mr->iova,
			       atomic_read(&mr->ib_pd->usecnt));

	if (mrc->access & IBV_ACCESS_KERNEL)
		ret = rdma_dereg_kernel_mr(mr->ib_mr, &mr->fd);
	else
		ret = rdma_dereg_user_mr(mr->ib_mr, &mr->fd);
	if (!ret) {
		mr->ib_mr = NULL;
		mr->ib_pd = NULL;
	}
	return ret;
}

/* Cannot hold rv->mutex */
struct rv_user_mrs *rv_user_mrs_alloc(struct rv_user *rv, u32 cache_size)
{
	int ret;
	struct rv_user_mrs *umrs;

	umrs = kzalloc(sizeof(*umrs), GFP_KERNEL);
	if (!umrs)
		return ERR_PTR(-ENOMEM);

	umrs->rv_inx = rv->inx;
	ret = rv_mr_cache_init(rv->inx, &umrs->cache, &rv_cache_ops, NULL,
			       current->mm, cache_size);
	if (ret)
		goto bail_free;
	kref_init(&umrs->kref); /* refcount now 1 */
	INIT_WORK(&umrs->put_work, rv_handle_user_mrs_put);
	return umrs;

bail_free:
	kfree(umrs);
	return ERR_PTR(ret);
}

/* called with rv->mutex */
void rv_user_mrs_attach(struct rv_user *rv)
{
	struct rv_user_mrs *umrs = rv->umrs;

	if (rv->rdma_mode == RV_RDMA_MODE_KERNEL) {
		/*
		 * for mode KERNEL the user_mrs object may survive past the
		 * rv_user close, so we need our own jdev reference to dereg
		 * MRs while outstanding send IOs complete.
		 * For mode USER, the MRs are using the user's pd
		 * and rv_user will free all MRs during close
		 *
		 * the jdev->pd we will use for MRs and QP needs ref to jdev
		 */
		rv_job_dev_get(rv->jdev);
		umrs->jdev = rv->jdev;
	}
	trace_rv_user_mrs_attach(umrs->rv_inx, umrs->jdev,
				 umrs->cache.total_size, umrs->cache.max_size,
				 kref_read(&umrs->kref));
}

static void rv_user_mrs_release(struct rv_user_mrs *umrs)
{
	trace_rv_user_mrs_release(umrs->rv_inx, umrs->jdev,
				  umrs->cache.total_size, umrs->cache.max_size,
				  kref_read(&umrs->kref));
	rv_mr_cache_deinit(umrs->rv_inx, &umrs->cache);
	if (umrs->jdev)
		rv_job_dev_put(umrs->jdev);
	kfree(umrs);
}

static void rv_handle_user_mrs_put(struct work_struct *work)
{
	struct rv_user_mrs *umrs = container_of(work, struct rv_user_mrs,
						put_work);

	rv_user_mrs_release(umrs);
}

static void rv_user_mrs_schedule_release(struct kref *kref)
{
	struct rv_user_mrs *umrs = container_of(kref, struct rv_user_mrs, kref);

	/*
	 * Since this function may be called from rv_write_done(),
	 * we can't call rv_user_mrs_release() directly to
	 * destroy it's rc QP and rv_mr_cache_deinit (and wait for completion)
	 * Instead, put the cleanup on a workqueue thread.
	 */
	rv_queue_work(&umrs->put_work);
}

void rv_user_mrs_get(struct rv_user_mrs *umrs)
{
	kref_get(&umrs->kref);
}

void rv_user_mrs_put(struct rv_user_mrs *umrs)
{
	kref_put(&umrs->kref, rv_user_mrs_schedule_release);
}

int doit_reg_mem(struct rv_user *rv, unsigned long arg)
{
	struct rv_mem_params mparams;
	struct rv_mr_cached *mrc;
	int ret;
	struct rv_user_mrs *umrs = rv->umrs;

	if (copy_from_user(&mparams.in, (void __user *)arg,
			   sizeof(mparams.in)))
		return -EFAULT;

	if (!enable_user_mr && !(mparams.in.access & IBV_ACCESS_KERNEL))
		return -EINVAL;

	/*
	 * rv->mutex protects use of umrs QP for REG_MR, also
	 * protects between rb_search and rb_insert vs races with other
	 * doit_reg_mem and doit_dereg_mem calls
	 */
	mutex_lock(&rv->mutex);
	if (!rv->attached) {
		ret = rv->was_attached ? -ENXIO : -EINVAL;
		goto bail_unlock;
	}
	if (rv->rdma_mode != RV_RDMA_MODE_KERNEL &&
	    (mparams.in.access & IBV_ACCESS_KERNEL)) {
		ret = -EINVAL;
		goto bail_unlock;
	}

	trace_rv_mr_reg(rv->rdma_mode, mparams.in.addr,
			mparams.in.length, mparams.in.access);
	/* get reference,  if found update hit stats */
	mrc = rv_mr_cache_search_get(&umrs->cache, mparams.in.addr,
				     mparams.in.length, mparams.in.access,
				     true);
	if (mrc)
		goto cont;

	/* create a new mrc for rb tree */
	mrc = kzalloc(sizeof(*mrc), GFP_KERNEL);
	if (!mrc) {
		ret = -ENOMEM;
		umrs->stats.failed++;
		goto bail_unlock;
	}

	/* register using verbs callback */
	ret = rv_drv_api_reg_mem(rv, &mparams.in, &mrc->mr);
	if (ret) {
		umrs->stats.failed++;
		goto bail_free;
	}
	mrc->addr = mparams.in.addr;
	mrc->len = mparams.in.length;
	mrc->access = mparams.in.access;

	ret = rv_mr_cache_insert(&umrs->cache, mrc);
	if (ret)
		goto bail_dereg;

cont:
	/* return the mr handle, lkey & rkey */
	mparams.out.mr_handle = (uint64_t)mrc;
	mparams.out.iova = mrc->mr.ib_mr->iova;
	mparams.out.lkey = mrc->mr.ib_mr->lkey;
	mparams.out.rkey = mrc->mr.ib_mr->rkey;

	if (copy_to_user((void __user *)arg, &mparams.out,
			 sizeof(mparams.out))) {
		ret = -EFAULT;
		goto bail_put;
	}

	mutex_unlock(&rv->mutex);

	return 0;

bail_dereg:
	if (rv_drv_api_dereg_mem(&mrc->mr))
		rv_err(rv->inx, "dereg_mem failed during cleanup\n");
bail_free:
	kfree(mrc);
bail_unlock:
	mutex_unlock(&rv->mutex);
	return ret;

bail_put:
	rv_mr_cache_put(&umrs->cache, mrc);
	mutex_unlock(&rv->mutex);
	return ret;
}

int doit_dereg_mem(struct rv_user *rv, unsigned long arg)
{
	struct rv_mr_cached *mrc;
	struct rv_dereg_params_in dparams;
	int ret = -EINVAL;

	if (copy_from_user(&dparams, (void __user *)arg, sizeof(dparams)))
		return -EFAULT;

	/* rv->mutex protects possible race with doit_reg_mem */
	mutex_lock(&rv->mutex);
	if (!rv->attached) {
		ret = rv->was_attached ? -ENXIO : -EINVAL;
		goto bail_unlock;
	}

	mrc = rv_mr_cache_search_put(&rv->umrs->cache, dparams.addr,
				     dparams.length, dparams.access);
	if (!mrc)
		goto bail_unlock;

	mutex_unlock(&rv->mutex);
	trace_rv_mr_dereg(rv->rdma_mode, dparams.addr,
			  dparams.length, dparams.access);

	return 0;

bail_unlock:
	mutex_unlock(&rv->mutex);
	return ret;
}

/* called with cache->lock */
static bool rv_cache_mrc_filter(struct rv_mr_cached *mrc, u64 addr,
				u64 len, u32 acc)
{
	return mrc->addr == addr && mrc->len == len && mrc->access == acc;
}

/* called with cache->lock */
static void rv_cache_mrc_get(struct rv_mr_cache *cache,
			     void *arg, struct rv_mr_cached *mrc)
{
	int refcount;

	refcount = atomic_inc_return(&mrc->refcount);
	if (refcount == 1) {
		cache->stats.inuse++;
		cache->stats.inuse_bytes += mrc->len;
	}
	rv_mr_cache_update_stats_max(cache, refcount);
}

/* called with cache->lock */
static int rv_cache_mrc_put(struct rv_mr_cache *cache,
			    void *arg, struct rv_mr_cached *mrc)
{
	int refcount;

	refcount = atomic_dec_return(&mrc->refcount);
	if (!refcount) {
		cache->stats.inuse--;
		cache->stats.inuse_bytes -= mrc->len;
	}
	return refcount;
}

/* called with cache->lock */
static int rv_cache_mrc_invalidate(struct rv_mr_cache *cache,
				   void *arg, struct rv_mr_cached *mrc)
{
	if (!atomic_read(&mrc->refcount))
		return 1;
	return 0;
}

/*
 * Return 1 if the mrc can be evicted from the cache
 *
 * Called with cache->lock
 */
static int rv_cache_mrc_evict(struct rv_mr_cache *cache,
			      void *arg, struct rv_mr_cached *mrc,
			      void *evict_arg, bool *stop)
{
	struct evict_data *evict_data = evict_arg;

	/* is this mrc still being used? */
	if (atomic_read(&mrc->refcount))
		return 0; /* keep this mrc */

	/* this mrc will be evicted, add its size to our count */
	evict_data->cleared += mrc->len;

	/* have enough bytes been cleared? */
	if (evict_data->cleared >= evict_data->target)
		*stop = true;

	return 1; /* remove this mrc */
}
