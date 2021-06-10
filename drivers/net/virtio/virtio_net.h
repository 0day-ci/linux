/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef __VIRTIO_NET_H__
#define __VIRTIO_NET_H__
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/module.h>
#include <linux/virtio.h>
#include <linux/virtio_net.h>
#include <linux/bpf.h>
#include <linux/bpf_trace.h>
#include <linux/scatterlist.h>
#include <linux/if_vlan.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/average.h>
#include <linux/filter.h>
#include <linux/kernel.h>
#include <net/route.h>
#include <net/xdp.h>
#include <net/net_failover.h>
#include <net/xdp_sock_drv.h>

#define VIRTIO_XDP_FLAG	BIT(0)

struct virtnet_info {
	struct virtio_device *vdev;
	struct virtqueue *cvq;
	struct net_device *dev;
	struct send_queue *sq;
	struct receive_queue *rq;
	unsigned int status;

	/* Max # of queue pairs supported by the device */
	u16 max_queue_pairs;

	/* # of queue pairs currently used by the driver */
	u16 curr_queue_pairs;

	/* # of XDP queue pairs currently used by the driver */
	u16 xdp_queue_pairs;

	/* xdp_queue_pairs may be 0, when xdp is already loaded. So add this. */
	bool xdp_enabled;

	/* I like... big packets and I cannot lie! */
	bool big_packets;

	/* Host will merge rx buffers for big packets (shake it! shake it!) */
	bool mergeable_rx_bufs;

	/* Has control virtqueue */
	bool has_cvq;

	/* Host can handle any s/g split between our header and packet data */
	bool any_header_sg;

	/* Packet virtio header size */
	u8 hdr_len;

	/* Work struct for refilling if we run low on memory. */
	struct delayed_work refill;

	/* Work struct for config space updates */
	struct work_struct config_work;

	/* Does the affinity hint is set for virtqueues? */
	bool affinity_hint_set;

	/* CPU hotplug instances for online & dead */
	struct hlist_node node;
	struct hlist_node node_dead;

	struct control_buf *ctrl;

	/* Ethtool settings */
	u8 duplex;
	u32 speed;

	unsigned long guest_offloads;
	unsigned long guest_offloads_capable;

	/* failover when STANDBY feature enabled */
	struct failover *failover;
};

/* RX packet size EWMA. The average packet size is used to determine the packet
 * buffer size when refilling RX rings. As the entire RX ring may be refilled
 * at once, the weight is chosen so that the EWMA will be insensitive to short-
 * term, transient changes in packet size.
 */
DECLARE_EWMA(pkt_len, 0, 64)

struct virtnet_stat_desc {
	char desc[ETH_GSTRING_LEN];
	size_t offset;
};

struct virtnet_sq_stats {
	struct u64_stats_sync syncp;
	u64 packets;
	u64 bytes;
	u64 xdp_tx;
	u64 xdp_tx_drops;
	u64 kicks;
};

struct virtnet_rq_stats {
	struct u64_stats_sync syncp;
	u64 packets;
	u64 bytes;
	u64 drops;
	u64 xdp_packets;
	u64 xdp_tx;
	u64 xdp_redirects;
	u64 xdp_drops;
	u64 kicks;
};

#define VIRTNET_SQ_STAT(m)	offsetof(struct virtnet_sq_stats, m)
#define VIRTNET_RQ_STAT(m)	offsetof(struct virtnet_rq_stats, m)

/* Internal representation of a send virtqueue */
struct send_queue {
	/* Virtqueue associated with this send _queue */
	struct virtqueue *vq;

	/* TX: fragments + linear part + virtio header */
	struct scatterlist sg[MAX_SKB_FRAGS + 2];

	/* Name of the send queue: output.$index */
	char name[40];

	struct virtnet_sq_stats stats;

	struct napi_struct napi;

	struct {
		struct xsk_buff_pool __rcu *pool;

		/* xsk wait for tx inter or softirq */
		bool need_wakeup;

		/* ctx used to record the page added to vq */
		struct virtnet_xsk_ctx_head *ctx_head;
	} xsk;
};

/* Internal representation of a receive virtqueue */
struct receive_queue {
	/* Virtqueue associated with this receive_queue */
	struct virtqueue *vq;

	struct napi_struct napi;

	struct bpf_prog __rcu *xdp_prog;

	struct virtnet_rq_stats stats;

	/* Chain pages by the private ptr. */
	struct page *pages;

	/* Average packet length for mergeable receive buffers. */
	struct ewma_pkt_len mrg_avg_pkt_len;

	/* Page frag for packet buffer allocation. */
	struct page_frag alloc_frag;

	/* RX: fragments + linear part + virtio header */
	struct scatterlist sg[MAX_SKB_FRAGS + 2];

	/* Min single buffer size for mergeable buffers case. */
	unsigned int min_buf_len;

	/* Name of this receive queue: input.$index */
	char name[40];

	struct xdp_rxq_info xdp_rxq;

	struct {
		struct xsk_buff_pool __rcu *pool;

		/* xdp rxq used by xsk */
		struct xdp_rxq_info xdp_rxq;

		/* ctx used to record the page added to vq */
		struct virtnet_xsk_ctx_head *ctx_head;
	} xsk;
};

static inline struct virtio_net_hdr_mrg_rxbuf *skb_vnet_hdr(struct sk_buff *skb)
{
	return (struct virtio_net_hdr_mrg_rxbuf *)skb->cb;
}

static inline bool is_xdp_raw_buffer_queue(struct virtnet_info *vi, int q)
{
	if (q < (vi->curr_queue_pairs - vi->xdp_queue_pairs))
		return false;
	else if (q < vi->curr_queue_pairs)
		return true;
	else
		return false;
}

static inline void virtqueue_napi_schedule(struct napi_struct *napi,
					   struct virtqueue *vq)
{
	if (napi_schedule_prep(napi)) {
		virtqueue_disable_cb(vq);
		__napi_schedule(napi);
	}
}

#include "xsk.h"

static inline bool is_skb_ptr(void *ptr)
{
	return !((unsigned long)ptr & (VIRTIO_XDP_FLAG | VIRTIO_XSK_FLAG));
}

static inline bool is_xdp_frame(void *ptr)
{
	return (unsigned long)ptr & VIRTIO_XDP_FLAG;
}

static inline void *xdp_to_ptr(struct xdp_frame *ptr)
{
	return (void *)((unsigned long)ptr | VIRTIO_XDP_FLAG);
}

static inline struct xdp_frame *ptr_to_xdp(void *ptr)
{
	return (struct xdp_frame *)((unsigned long)ptr & ~VIRTIO_XDP_FLAG);
}

static inline void __free_old_xmit(struct send_queue *sq, bool in_napi,
				   struct virtnet_sq_stats *stats)
{
	unsigned int xsknum = 0;
	unsigned int len;
	void *ptr;

	while ((ptr = virtqueue_get_buf(sq->vq, &len)) != NULL) {
		if (is_skb_ptr(ptr)) {
			struct sk_buff *skb = ptr;

			pr_debug("Sent skb %p\n", skb);

			stats->bytes += skb->len;
			napi_consume_skb(skb, in_napi);
		} else if (is_xdp_frame(ptr)) {
			struct xdp_frame *frame = ptr_to_xdp(ptr);

			stats->bytes += frame->len;
			xdp_return_frame(frame);
		} else {
			struct virtnet_xsk_ctx_tx *ctx;

			ctx = ptr_to_xsk(ptr);

			/* Maybe this ptr was sent by the last xsk. */
			if (ctx->ctx.head->active)
				++xsknum;

			stats->bytes += ctx->len;
			virtnet_xsk_ctx_tx_put(ctx);
		}
		stats->packets++;
	}

	if (xsknum)
		virtnet_xsk_complete(sq, xsknum);
}

int virtnet_run_xdp(struct net_device *dev, struct bpf_prog *xdp_prog,
		    struct xdp_buff *xdp, unsigned int *xdp_xmit,
		    struct virtnet_rq_stats *stats);
struct sk_buff *merge_receive_follow_bufs(struct net_device *dev,
					  struct virtnet_info *vi,
					  struct receive_queue *rq,
					  struct sk_buff *head_skb,
					  u16 num_buf,
					  struct virtnet_rq_stats *stats);
void merge_drop_follow_bufs(struct net_device *dev, struct receive_queue *rq,
			    u16 num_buf, struct virtnet_rq_stats *stats);
#endif
