// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * virtio-net xsk
 */

#include "virtio_net.h"

static struct virtio_net_hdr_mrg_rxbuf xsk_hdr;

static struct virtnet_xsk_ctx *virtnet_xsk_ctx_get(struct virtnet_xsk_ctx_head *head)
{
	struct virtnet_xsk_ctx *ctx;

	ctx = head->ctx;
	head->ctx = ctx->next;

	++head->ref;

	return ctx;
}

#define virtnet_xsk_ctx_tx_get(head) ((struct virtnet_xsk_ctx_tx *)virtnet_xsk_ctx_get(head))

static void virtnet_xsk_check_queue(struct send_queue *sq)
{
	struct virtnet_info *vi = sq->vq->vdev->priv;
	struct net_device *dev = vi->dev;
	int qnum = sq - vi->sq;

	/* If it is a raw buffer queue, it does not check whether the status
	 * of the queue is stopped when sending. So there is no need to check
	 * the situation of the raw buffer queue.
	 */
	if (is_xdp_raw_buffer_queue(vi, qnum))
		return;

	/* If this sq is not the exclusive queue of the current cpu,
	 * then it may be called by start_xmit, so check it running out
	 * of space.
	 *
	 * Stop the queue to avoid getting packets that we are
	 * then unable to transmit. Then wait the tx interrupt.
	 */
	if (sq->vq->num_free < 2 + MAX_SKB_FRAGS)
		netif_stop_subqueue(dev, qnum);
}

void virtnet_xsk_complete(struct send_queue *sq, u32 num)
{
	struct xsk_buff_pool *pool;

	rcu_read_lock();
	pool = rcu_dereference(sq->xsk.pool);
	if (!pool) {
		rcu_read_unlock();
		return;
	}
	xsk_tx_completed(pool, num);
	rcu_read_unlock();

	if (sq->xsk.need_wakeup) {
		sq->xsk.need_wakeup = false;
		virtqueue_napi_schedule(&sq->napi, sq->vq);
	}
}

static int virtnet_xsk_xmit(struct send_queue *sq, struct xsk_buff_pool *pool,
			    struct xdp_desc *desc)
{
	struct virtnet_xsk_ctx_tx *ctx;
	struct virtnet_info *vi;
	u32 offset, n, len;
	struct page *page;
	void *data;

	vi = sq->vq->vdev->priv;

	data = xsk_buff_raw_get_data(pool, desc->addr);
	offset = offset_in_page(data);

	ctx = virtnet_xsk_ctx_tx_get(sq->xsk.ctx_head);

	/* xsk unaligned mode, desc may use two pages */
	if (desc->len > PAGE_SIZE - offset)
		n = 3;
	else
		n = 2;

	sg_init_table(sq->sg, n);
	sg_set_buf(sq->sg, &xsk_hdr, vi->hdr_len);

	/* handle for xsk first page */
	len = min_t(int, desc->len, PAGE_SIZE - offset);
	page = vmalloc_to_page(data);
	sg_set_page(sq->sg + 1, page, len, offset);

	/* ctx is used to record and reference this page to prevent xsk from
	 * being released before this xmit is recycled
	 */
	ctx->ctx.page = page;
	get_page(page);

	/* xsk unaligned mode, handle for the second page */
	if (len < desc->len) {
		page = vmalloc_to_page(data + len);
		len = min_t(int, desc->len - len, PAGE_SIZE);
		sg_set_page(sq->sg + 2, page, len, 0);

		ctx->ctx.page_unaligned = page;
		get_page(page);
	} else {
		ctx->ctx.page_unaligned = NULL;
	}

	return virtqueue_add_outbuf(sq->vq, sq->sg, n,
				   xsk_to_ptr(ctx), GFP_ATOMIC);
}

static int virtnet_xsk_xmit_batch(struct send_queue *sq,
				  struct xsk_buff_pool *pool,
				  unsigned int budget,
				  bool in_napi, int *done,
				  struct virtnet_sq_stats *stats)
{
	struct xdp_desc desc;
	int err, packet = 0;
	int ret = -EAGAIN;

	while (budget-- > 0) {
		if (sq->vq->num_free < 2 + MAX_SKB_FRAGS) {
			ret = -EBUSY;
			break;
		}

		if (!xsk_tx_peek_desc(pool, &desc)) {
			/* done */
			ret = 0;
			break;
		}

		err = virtnet_xsk_xmit(sq, pool, &desc);
		if (unlikely(err)) {
			ret = -EBUSY;
			break;
		}

		++packet;
	}

	if (packet) {
		if (virtqueue_kick_prepare(sq->vq) && virtqueue_notify(sq->vq))
			++stats->kicks;

		*done += packet;
		stats->xdp_tx += packet;

		xsk_tx_release(pool);
	}

	return ret;
}

static int virtnet_xsk_run(struct send_queue *sq, struct xsk_buff_pool *pool,
			   int budget, bool in_napi)
{
	struct virtnet_sq_stats stats = {};
	int done = 0;
	int err;

	sq->xsk.need_wakeup = false;
	__free_old_xmit(sq, in_napi, &stats);

	/* return err:
	 * -EAGAIN: done == budget
	 * -EBUSY:  done < budget
	 *  0    :  done < budget
	 */
xmit:
	err = virtnet_xsk_xmit_batch(sq, pool, budget - done, in_napi,
				     &done, &stats);
	if (err == -EBUSY) {
		__free_old_xmit(sq, in_napi, &stats);

		/* If the space is enough, let napi run again. */
		if (sq->vq->num_free >= 2 + MAX_SKB_FRAGS)
			goto xmit;
		else
			sq->xsk.need_wakeup = true;
	}

	virtnet_xsk_check_queue(sq);

	u64_stats_update_begin(&sq->stats.syncp);
	sq->stats.packets += stats.packets;
	sq->stats.bytes += stats.bytes;
	sq->stats.kicks += stats.kicks;
	sq->stats.xdp_tx += stats.xdp_tx;
	u64_stats_update_end(&sq->stats.syncp);

	return done;
}

int virtnet_poll_xsk(struct send_queue *sq, int budget)
{
	struct xsk_buff_pool *pool;
	int work_done = 0;

	rcu_read_lock();
	pool = rcu_dereference(sq->xsk.pool);
	if (pool)
		work_done = virtnet_xsk_run(sq, pool, budget, true);
	rcu_read_unlock();
	return work_done;
}

int virtnet_xsk_wakeup(struct net_device *dev, u32 qid, u32 flag)
{
	struct virtnet_info *vi = netdev_priv(dev);
	struct xsk_buff_pool *pool;
	struct send_queue *sq;

	if (!netif_running(dev))
		return -ENETDOWN;

	if (qid >= vi->curr_queue_pairs)
		return -EINVAL;

	sq = &vi->sq[qid];

	rcu_read_lock();
	pool = rcu_dereference(sq->xsk.pool);
	if (pool) {
		local_bh_disable();
		virtqueue_napi_schedule(&sq->napi, sq->vq);
		local_bh_enable();
	}
	rcu_read_unlock();
	return 0;
}

static struct virtnet_xsk_ctx_head *virtnet_xsk_ctx_alloc(struct xsk_buff_pool *pool,
							  struct virtqueue *vq)
{
	struct virtnet_xsk_ctx_head *head;
	u32 size, n, ring_size, ctx_sz;
	struct virtnet_xsk_ctx *ctx;
	void *p;

	ctx_sz = sizeof(struct virtnet_xsk_ctx_tx);

	ring_size = virtqueue_get_vring_size(vq);
	size = sizeof(*head) + ctx_sz * ring_size;

	head = kmalloc(size, GFP_ATOMIC);
	if (!head)
		return NULL;

	memset(head, 0, sizeof(*head));

	head->active = true;
	head->frame_size = xsk_pool_get_rx_frame_size(pool);

	p = head + 1;
	for (n = 0; n < ring_size; ++n) {
		ctx = p;
		ctx->head = head;
		ctx->next = head->ctx;
		head->ctx = ctx;

		p += ctx_sz;
	}

	return head;
}

static int virtnet_xsk_pool_enable(struct net_device *dev,
				   struct xsk_buff_pool *pool,
				   u16 qid)
{
	struct virtnet_info *vi = netdev_priv(dev);
	struct send_queue *sq;

	if (qid >= vi->curr_queue_pairs)
		return -EINVAL;

	sq = &vi->sq[qid];

	/* xsk zerocopy depend on the tx napi.
	 *
	 * All data is actually consumed and sent out from the xsk tx queue
	 * under the tx napi mechanism.
	 */
	if (!sq->napi.weight)
		return -EPERM;

	memset(&sq->xsk, 0, sizeof(sq->xsk));

	sq->xsk.ctx_head = virtnet_xsk_ctx_alloc(pool, sq->vq);
	if (!sq->xsk.ctx_head)
		return -ENOMEM;

	/* Here is already protected by rtnl_lock, so rcu_assign_pointer is
	 * safe.
	 */
	rcu_assign_pointer(sq->xsk.pool, pool);

	return 0;
}

static int virtnet_xsk_pool_disable(struct net_device *dev, u16 qid)
{
	struct virtnet_info *vi = netdev_priv(dev);
	struct send_queue *sq;

	if (qid >= vi->curr_queue_pairs)
		return -EINVAL;

	sq = &vi->sq[qid];

	/* Here is already protected by rtnl_lock, so rcu_assign_pointer is
	 * safe.
	 */
	rcu_assign_pointer(sq->xsk.pool, NULL);

	/* Sync with the XSK wakeup and with NAPI. */
	synchronize_net();

	if (READ_ONCE(sq->xsk.ctx_head->ref))
		WRITE_ONCE(sq->xsk.ctx_head->active, false);
	else
		kfree(sq->xsk.ctx_head);

	sq->xsk.ctx_head = NULL;

	return 0;
}

int virtnet_xsk_pool_setup(struct net_device *dev, struct netdev_bpf *xdp)
{
	if (xdp->xsk.pool)
		return virtnet_xsk_pool_enable(dev, xdp->xsk.pool,
					       xdp->xsk.queue_id);
	else
		return virtnet_xsk_pool_disable(dev, xdp->xsk.queue_id);
}

