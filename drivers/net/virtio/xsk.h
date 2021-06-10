/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef __XSK_H__
#define __XSK_H__

#define VIRTIO_XSK_FLAG	BIT(1)

#define VIRTNET_XSK_BUFF_CTX  ((void *)(unsigned long)~0L)

/* When xsk disable, under normal circumstances, the network card must reclaim
 * all the memory that has been sent and the memory added to the rq queue by
 * destroying the queue.
 *
 * But virtio's queue does not support separate setting to been disable. "Reset"
 * is not very suitable.
 *
 * The method here is that each sent chunk or chunk added to the rq queue is
 * described by an independent structure struct virtnet_xsk_ctx.
 *
 * We will use get_page(page) to refer to the page where these chunks are
 * located. And these pages will be recorded in struct virtnet_xsk_ctx. So these
 * chunks in vq are safe. When recycling, put the these page.
 *
 * These structures point to struct virtnet_xsk_ctx_head, and ref records how
 * many chunks have not been reclaimed. If active == 0, it means that xsk has
 * been disabled.
 *
 * In this way, even if xsk has been unbundled with rq/sq, or a new xsk and
 * rq/sq  are bound, and a new virtnet_xsk_ctx_head is created. It will not
 * affect the old virtnet_xsk_ctx to be recycled. And free all head and ctx when
 * ref is 0.
 */
struct virtnet_xsk_ctx;
struct virtnet_xsk_ctx_head {
	struct virtnet_xsk_ctx *ctx;

	/* how many ctx has been add to vq */
	u64 ref;

	unsigned int frame_size;
	unsigned int truesize;
	unsigned int hdr_len;

	/* the xsk status */
	bool active;
};

struct virtnet_xsk_ctx {
	struct virtnet_xsk_ctx_head *head;
	struct virtnet_xsk_ctx *next;

	struct page *page;

	/* xsk unaligned mode will use two page in one desc */
	struct page *page_unaligned;
};

struct virtnet_xsk_ctx_tx {
	/* this *MUST* be the first */
	struct virtnet_xsk_ctx ctx;

	/* xsk tx xmit use this record the len of packet */
	u32 len;
};

struct virtnet_xsk_ctx_rx {
	/* this *MUST* be the first */
	struct virtnet_xsk_ctx ctx;

	/* xdp get from xsk */
	struct xdp_buff *xdp;

	/* offset of the xdp.data inside it's page */
	int offset;

	/* xsk xdp headroom */
	unsigned int headroom;

	/* Users don't want us to occupy xsk frame to save virtio hdr */
	struct virtio_net_hdr_mrg_rxbuf hdr;
};

static inline bool is_xsk_ctx(void *ctx)
{
	return ctx == VIRTNET_XSK_BUFF_CTX;
}

static inline void *xsk_to_ptr(struct virtnet_xsk_ctx_tx *ctx)
{
	return (void *)((unsigned long)ctx | VIRTIO_XSK_FLAG);
}

static inline struct virtnet_xsk_ctx_tx *ptr_to_xsk(void *ptr)
{
	unsigned long p;

	p = (unsigned long)ptr;
	return (struct virtnet_xsk_ctx_tx *)(p & ~VIRTIO_XSK_FLAG);
}

static inline void virtnet_xsk_ctx_put(struct virtnet_xsk_ctx *ctx)
{
	put_page(ctx->page);
	if (ctx->page_unaligned)
		put_page(ctx->page_unaligned);

	--ctx->head->ref;

	if (ctx->head->active) {
		ctx->next = ctx->head->ctx;
		ctx->head->ctx = ctx;
	} else {
		if (!ctx->head->ref)
			kfree(ctx->head);
	}
}

#define virtnet_xsk_ctx_tx_put(ctx) \
	virtnet_xsk_ctx_put((struct virtnet_xsk_ctx *)ctx)

static inline void virtnet_xsk_ctx_rx_put(struct virtnet_xsk_ctx_rx *ctx)
{
	if (ctx->xdp && ctx->ctx.head->active)
		xsk_buff_free(ctx->xdp);

	virtnet_xsk_ctx_put((struct virtnet_xsk_ctx *)ctx);
}

static inline void virtnet_rx_put_buf(char *buf, void *ctx)
{
	if (is_xsk_ctx(ctx))
		virtnet_xsk_ctx_rx_put((struct virtnet_xsk_ctx_rx *)buf);
	else
		put_page(virt_to_head_page(buf));
}

void virtnet_xsk_ctx_rx_copy(struct virtnet_xsk_ctx_rx *ctx,
			     char *dst, unsigned int len, bool hdr);
int add_recvbuf_xsk(struct virtnet_info *vi, struct receive_queue *rq,
		    struct xsk_buff_pool *pool, gfp_t gfp);
struct sk_buff *receive_xsk(struct net_device *dev, struct virtnet_info *vi,
			    struct receive_queue *rq, void *buf,
			    unsigned int len, unsigned int *xdp_xmit,
			    struct virtnet_rq_stats *stats);
int virtnet_xsk_wakeup(struct net_device *dev, u32 qid, u32 flag);
int virtnet_poll_xsk(struct send_queue *sq, int budget);
void virtnet_xsk_complete(struct send_queue *sq, u32 num);
int virtnet_xsk_pool_setup(struct net_device *dev, struct netdev_bpf *xdp);

static inline bool fill_recv_xsk(struct virtnet_info *vi,
				 struct receive_queue *rq,
				 gfp_t gfp)
{
	struct xsk_buff_pool *pool;
	int err;

	rcu_read_lock();
	pool = rcu_dereference(rq->xsk.pool);
	if (pool) {
		while (rq->vq->num_free >= 3) {
			err = add_recvbuf_xsk(vi, rq, pool, gfp);
			if (err)
				break;
		}
	} else {
		rcu_read_unlock();
		return false;
	}
	rcu_read_unlock();

	return err != -ENOMEM;
}

#endif
