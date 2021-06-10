// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * virtio-net xsk
 */

#include "virtio_net.h"

int xsk_kick_thr = 8;

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
#define virtnet_xsk_ctx_rx_get(head) ((struct virtnet_xsk_ctx_rx *)virtnet_xsk_ctx_get(head))

static unsigned int virtnet_receive_buf_num(struct virtnet_info *vi, char *buf)
{
	struct virtio_net_hdr_mrg_rxbuf *hdr;

	if (vi->mergeable_rx_bufs) {
		hdr = (struct virtio_net_hdr_mrg_rxbuf *)buf;
		return virtio16_to_cpu(vi->vdev, hdr->num_buffers);
	}

	return 1;
}

/* when xsk rx ctx ref two page, copy to dst from two page */
static void virtnet_xsk_rx_ctx_merge(struct virtnet_xsk_ctx_rx *ctx,
				     char *dst, unsigned int len)
{
	unsigned int size;
	int offset;
	char *src;

	/* data start from first page */
	if (ctx->offset >= 0) {
		offset = ctx->offset;

		size = min_t(int, PAGE_SIZE - offset, len);
		src = page_address(ctx->ctx.page) + offset;
		memcpy(dst, src, size);

		if (len > size) {
			src = page_address(ctx->ctx.page_unaligned);
			memcpy(dst + size, src, len - size);
		}

	} else {
		offset = -ctx->offset;

		src = page_address(ctx->ctx.page_unaligned) + offset;

		memcpy(dst, src, len);
	}
}

/* copy ctx to dst, need to make sure that len is safe */
void virtnet_xsk_ctx_rx_copy(struct virtnet_xsk_ctx_rx *ctx,
			     char *dst, unsigned int len,
			     bool hdr)
{
	char *src;
	int size;

	if (hdr) {
		size = min_t(int, ctx->ctx.head->hdr_len, len);
		memcpy(dst, &ctx->hdr, size);
		len -= size;
		if (!len)
			return;
		dst += size;
	}

	if (!ctx->ctx.page_unaligned) {
		src = page_address(ctx->ctx.page) + ctx->offset;
		memcpy(dst, src, len);

	} else {
		virtnet_xsk_rx_ctx_merge(ctx, dst, len);
	}
}

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

static struct sk_buff *virtnet_xsk_construct_skb_xdp(struct receive_queue *rq,
						     struct xdp_buff *xdp)
{
	unsigned int metasize = xdp->data - xdp->data_meta;
	struct sk_buff *skb;
	unsigned int size;

	size = xdp->data_end - xdp->data_hard_start;
	skb = napi_alloc_skb(&rq->napi, size);
	if (unlikely(!skb))
		return NULL;

	skb_reserve(skb, xdp->data_meta - xdp->data_hard_start);

	size = xdp->data_end - xdp->data_meta;
	memcpy(__skb_put(skb, size), xdp->data_meta, size);

	if (metasize) {
		__skb_pull(skb, metasize);
		skb_metadata_set(skb, metasize);
	}

	return skb;
}

static struct sk_buff *virtnet_xsk_construct_skb_ctx(struct net_device *dev,
						     struct virtnet_info *vi,
						     struct receive_queue *rq,
						     struct virtnet_xsk_ctx_rx *ctx,
						     unsigned int len,
						     struct virtnet_rq_stats *stats)
{
	struct virtio_net_hdr_mrg_rxbuf *hdr;
	struct sk_buff *skb;
	int num_buf;
	char *dst;

	len -= vi->hdr_len;

	skb = napi_alloc_skb(&rq->napi, len);
	if (unlikely(!skb))
		return NULL;

	dst = __skb_put(skb, len);

	virtnet_xsk_ctx_rx_copy(ctx, dst, len, false);

	num_buf = virtnet_receive_buf_num(vi, (char *)&ctx->hdr);
	if (num_buf > 1) {
		skb = merge_receive_follow_bufs(dev, vi, rq, skb, num_buf,
						stats);
		if (!skb)
			return NULL;
	}

	hdr = skb_vnet_hdr(skb);
	memcpy(hdr, &ctx->hdr, vi->hdr_len);

	return skb;
}

/* len not include virtio-net hdr */
static struct xdp_buff *virtnet_xsk_check_xdp(struct virtnet_info *vi,
					      struct receive_queue *rq,
					      struct virtnet_xsk_ctx_rx *ctx,
					      struct xdp_buff *_xdp,
					      unsigned int len)
{
	struct xdp_buff *xdp;
	struct page *page;
	int frame_sz;
	char *data;

	if (ctx->ctx.head->active) {
		xdp = ctx->xdp;
		xdp->data_end = xdp->data + len;

		return xdp;
	}

	/* ctx->xdp is invalid, because of that is released */

	if (!ctx->ctx.page_unaligned) {
		data = page_address(ctx->ctx.page) + ctx->offset;
		page = ctx->ctx.page;
	} else {
		page = alloc_page(GFP_ATOMIC);
		if (!page)
			return NULL;

		data = page_address(page) + ctx->headroom;

		virtnet_xsk_rx_ctx_merge(ctx, data, len);

		put_page(ctx->ctx.page);
		put_page(ctx->ctx.page_unaligned);

		/* page will been put when ctx is put */
		ctx->ctx.page = page;
		ctx->ctx.page_unaligned = NULL;
	}

	/* If xdp consume the data with XDP_REDIRECT/XDP_TX, the page
	 * ref will been dec. So call get_page here.
	 *
	 * If xdp has been consumed, the page ref will dec auto and
	 * virtnet_xsk_ctx_rx_put will dec the ref again.
	 *
	 * If xdp has not been consumed, then manually put_page once before
	 * virtnet_xsk_ctx_rx_put.
	 */
	get_page(page);

	xdp = _xdp;

	frame_sz = ctx->ctx.head->frame_size + ctx->headroom;

	/* use xdp rxq without MEM_TYPE_XSK_BUFF_POOL */
	xdp_init_buff(xdp, frame_sz, &rq->xdp_rxq);
	xdp_prepare_buff(xdp, data - ctx->headroom, ctx->headroom, len, true);

	return xdp;
}

int add_recvbuf_xsk(struct virtnet_info *vi, struct receive_queue *rq,
		    struct xsk_buff_pool *pool, gfp_t gfp)
{
	struct page *page, *page_start, *page_end;
	unsigned long data, data_end, data_start;
	struct virtnet_xsk_ctx_rx *ctx;
	struct xdp_buff *xsk_xdp;
	int err, size, n;
	u32 offset;

	xsk_xdp = xsk_buff_alloc(pool);
	if (!xsk_xdp)
		return -ENOMEM;

	ctx = virtnet_xsk_ctx_rx_get(rq->xsk.ctx_head);

	ctx->xdp = xsk_xdp;
	ctx->headroom = xsk_xdp->data - xsk_xdp->data_hard_start;

	offset = offset_in_page(xsk_xdp->data);

	data_start = (unsigned long)xsk_xdp->data_hard_start;
	data       = (unsigned long)xsk_xdp->data;
	data_end   = data + ctx->ctx.head->frame_size - 1;

	page_start = vmalloc_to_page((void *)data_start);

	ctx->ctx.page = page_start;
	get_page(page_start);

	if ((data_end & PAGE_MASK) == (data_start & PAGE_MASK)) {
		page_end = page_start;
		page = page_start;
		ctx->offset = offset;

		ctx->ctx.page_unaligned = NULL;
		n = 2;
	} else {
		page_end = vmalloc_to_page((void *)data_end);

		ctx->ctx.page_unaligned = page_end;
		get_page(page_end);

		if ((data_start & PAGE_MASK) == (data & PAGE_MASK)) {
			page = page_start;
			ctx->offset = offset;
			n = 3;
		} else {
			page = page_end;
			ctx->offset = -offset;
			n = 2;
		}
	}

	size = min_t(int, PAGE_SIZE - offset, ctx->ctx.head->frame_size);

	sg_init_table(rq->sg, n);
	sg_set_buf(rq->sg, &ctx->hdr, vi->hdr_len);
	sg_set_page(rq->sg + 1, page, size, offset);

	if (n == 3) {
		size = ctx->ctx.head->frame_size - size;
		sg_set_page(rq->sg + 2, page_end, size, 0);
	}

	err = virtqueue_add_inbuf_ctx(rq->vq, rq->sg, n, ctx,
				      VIRTNET_XSK_BUFF_CTX, gfp);
	if (err < 0)
		virtnet_xsk_ctx_rx_put(ctx);

	return err;
}

struct sk_buff *receive_xsk(struct net_device *dev, struct virtnet_info *vi,
			    struct receive_queue *rq, void *buf,
			    unsigned int len, unsigned int *xdp_xmit,
			    struct virtnet_rq_stats *stats)
{
	struct virtnet_xsk_ctx_rx *ctx;
	struct xsk_buff_pool *pool;
	struct sk_buff *skb = NULL;
	struct xdp_buff *xdp, _xdp;
	struct bpf_prog *xdp_prog;
	u16 num_buf = 1;
	int ret;

	ctx = (struct virtnet_xsk_ctx_rx *)buf;

	rcu_read_lock();

	pool     = rcu_dereference(rq->xsk.pool);
	xdp_prog = rcu_dereference(rq->xdp_prog);
	if (!pool || !xdp_prog)
		goto skb;

	/* this may happen when xsk chunk size too small. */
	num_buf = virtnet_receive_buf_num(vi, (char *)&ctx->hdr);
	if (num_buf > 1)
		goto drop;

	xdp = virtnet_xsk_check_xdp(vi, rq, ctx, &_xdp, len - vi->hdr_len);
	if (!xdp)
		goto drop;

	ret = virtnet_run_xdp(dev, xdp_prog, xdp, xdp_xmit, stats);
	if (unlikely(ret)) {
		/* pair for get_page inside virtnet_xsk_check_xdp */
		if (!ctx->ctx.head->active)
			put_page(ctx->ctx.page);

		if (unlikely(ret < 0))
			goto drop;

		/* XDP_PASS */
		skb = virtnet_xsk_construct_skb_xdp(rq, xdp);
	} else {
		/* ctx->xdp has been consumed */
		ctx->xdp = NULL;
	}

end:
	virtnet_xsk_ctx_rx_put(ctx);
	rcu_read_unlock();
	return skb;

skb:
	skb = virtnet_xsk_construct_skb_ctx(dev, vi, rq, ctx, len, stats);
	goto end;

drop:
	stats->drops++;

	if (num_buf > 1)
		merge_drop_follow_bufs(dev, rq, num_buf, stats);
	goto end;
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
	int need_kick = 0;

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
		++need_kick;
		if (need_kick > xsk_kick_thr) {
			if (virtqueue_kick_prepare(sq->vq) &&
			    virtqueue_notify(sq->vq))
				++stats->kicks;

			need_kick = 0;
		}
	}

	if (packet) {
		if (need_kick) {
			if (virtqueue_kick_prepare(sq->vq) &&
			    virtqueue_notify(sq->vq))
				++stats->kicks;
		}

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
	struct netdev_queue *txq;
	struct send_queue *sq;

	if (!netif_running(dev))
		return -ENETDOWN;

	if (qid >= vi->curr_queue_pairs)
		return -EINVAL;

	sq = &vi->sq[qid];

	rcu_read_lock();
	pool = rcu_dereference(sq->xsk.pool);
	if (!pool)
		goto end;

	if (napi_if_scheduled_mark_missed(&sq->napi))
		goto end;

	txq = netdev_get_tx_queue(dev, qid);

	__netif_tx_lock_bh(txq);

	/* Send part of the packet directly to reduce the delay in sending the
	 * packet, and this can actively trigger the tx interrupts.
	 *
	 * If no packet is sent out, the ring of the device is full. In this
	 * case, we will still get a tx interrupt response. Then we will deal
	 * with the subsequent packet sending work.
	 */
	virtnet_xsk_run(sq, pool, sq->napi.weight, false);

	__netif_tx_unlock_bh(txq);

end:
	rcu_read_unlock();
	return 0;
}

static struct virtnet_xsk_ctx_head *virtnet_xsk_ctx_alloc(struct virtnet_info *vi,
							  struct xsk_buff_pool *pool,
							  struct virtqueue *vq,
							  bool rx)
{
	struct virtnet_xsk_ctx_head *head;
	u32 size, n, ring_size, ctx_sz;
	struct virtnet_xsk_ctx *ctx;
	void *p;

	if (rx)
		ctx_sz = sizeof(struct virtnet_xsk_ctx_rx);
	else
		ctx_sz = sizeof(struct virtnet_xsk_ctx_tx);

	ring_size = virtqueue_get_vring_size(vq);
	size = sizeof(*head) + ctx_sz * ring_size;

	head = kmalloc(size, GFP_ATOMIC);
	if (!head)
		return NULL;

	memset(head, 0, sizeof(*head));

	head->active = true;
	head->frame_size = xsk_pool_get_rx_frame_size(pool);
	head->hdr_len = vi->hdr_len;
	head->truesize = head->frame_size + vi->hdr_len;

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
	struct receive_queue *rq;
	struct send_queue *sq;
	int err;

	if (qid >= vi->curr_queue_pairs)
		return -EINVAL;

	sq = &vi->sq[qid];
	rq = &vi->rq[qid];

	/* xsk zerocopy depend on the tx napi.
	 *
	 * All data is actually consumed and sent out from the xsk tx queue
	 * under the tx napi mechanism.
	 */
	if (!sq->napi.weight)
		return -EPERM;

	memset(&sq->xsk, 0, sizeof(sq->xsk));

	sq->xsk.ctx_head = virtnet_xsk_ctx_alloc(vi, pool, sq->vq, false);
	if (!sq->xsk.ctx_head)
		return -ENOMEM;

	/* In big_packets mode, xdp cannot work, so there is no need to
	 * initialize xsk of rq.
	 */
	if (!vi->big_packets || vi->mergeable_rx_bufs) {
		err = xdp_rxq_info_reg(&rq->xsk.xdp_rxq, dev, qid,
				       rq->napi.napi_id);
		if (err < 0)
			goto err;

		err = xdp_rxq_info_reg_mem_model(&rq->xsk.xdp_rxq,
						 MEM_TYPE_XSK_BUFF_POOL, NULL);
		if (err < 0) {
			xdp_rxq_info_unreg(&rq->xsk.xdp_rxq);
			goto err;
		}

		rq->xsk.ctx_head = virtnet_xsk_ctx_alloc(vi, pool, rq->vq, true);
		if (!rq->xsk.ctx_head) {
			err = -ENOMEM;
			goto err;
		}

		xsk_pool_set_rxq_info(pool, &rq->xsk.xdp_rxq);

		/* Here is already protected by rtnl_lock, so rcu_assign_pointer
		 * is safe.
		 */
		rcu_assign_pointer(rq->xsk.pool, pool);
	}

	/* Here is already protected by rtnl_lock, so rcu_assign_pointer is
	 * safe.
	 */
	rcu_assign_pointer(sq->xsk.pool, pool);

	return 0;

err:
	kfree(sq->xsk.ctx_head);
	return err;
}

static int virtnet_xsk_pool_disable(struct net_device *dev, u16 qid)
{
	struct virtnet_info *vi = netdev_priv(dev);
	struct receive_queue *rq;
	struct send_queue *sq;

	if (qid >= vi->curr_queue_pairs)
		return -EINVAL;

	sq = &vi->sq[qid];
	rq = &vi->rq[qid];

	/* Here is already protected by rtnl_lock, so rcu_assign_pointer is
	 * safe.
	 */
	rcu_assign_pointer(rq->xsk.pool, NULL);
	rcu_assign_pointer(sq->xsk.pool, NULL);

	/* Sync with the XSK wakeup and with NAPI. */
	synchronize_net();

	if (READ_ONCE(sq->xsk.ctx_head->ref))
		WRITE_ONCE(sq->xsk.ctx_head->active, false);
	else
		kfree(sq->xsk.ctx_head);

	sq->xsk.ctx_head = NULL;

	if (!vi->big_packets || vi->mergeable_rx_bufs) {
		if (READ_ONCE(rq->xsk.ctx_head->ref))
			WRITE_ONCE(rq->xsk.ctx_head->active, false);
		else
			kfree(rq->xsk.ctx_head);

		rq->xsk.ctx_head = NULL;

		xdp_rxq_info_unreg(&rq->xsk.xdp_rxq);
	}

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

