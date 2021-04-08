// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright (c) 2021, Microsoft Corporation. */

#include <linux/inetdevice.h>
#include <linux/etherdevice.h>

#include <net/checksum.h>
#include <net/ip6_checksum.h>

#include "mana.h"

/* Microsoft Azure Network Adapter (ANA) functions */

static int ana_open(struct net_device *ndev)
{
	struct ana_context *ac = netdev_priv(ndev);

	ac->port_is_up = true;

	/* Ensure port state updated before txq state */
	smp_wmb();

	netif_carrier_on(ndev);
	netif_tx_wake_all_queues(ndev);

	return 0;
}

static int ana_close(struct net_device *ndev)
{
	struct ana_context *ac = netdev_priv(ndev);

	ac->port_is_up = false;

	/* Ensure port state updated before txq state */
	smp_wmb();

	netif_tx_disable(ndev);
	netif_carrier_off(ndev);

	return 0;
}

static bool gdma_can_tx(struct gdma_queue *wq)
{
	return gdma_wq_avail_space(wq) >= MAX_TX_WQE_SIZE;
}

static unsigned int  ana_checksum_info(struct sk_buff *skb)
{
	if (skb->protocol == htons(ETH_P_IP)) {
		struct iphdr *ip = ip_hdr(skb);

		if (ip->protocol == IPPROTO_TCP)
			return IPPROTO_TCP;

		if (ip->protocol == IPPROTO_UDP)
			return IPPROTO_UDP;
	} else if (skb->protocol == htons(ETH_P_IPV6)) {
		struct ipv6hdr *ip6 = ipv6_hdr(skb);

		if (ip6->nexthdr == IPPROTO_TCP)
			return IPPROTO_TCP;

		if (ip6->nexthdr == IPPROTO_UDP)
			return IPPROTO_UDP;
	}

	/* No csum offloading */
	return 0;
}

static int ana_map_skb(struct sk_buff *skb, struct ana_context *ac,
		       struct ana_tx_package *tp)
{
	struct ana_skb_head *ash = (struct ana_skb_head *)skb->head;
	struct gdma_dev *gd = ac->gdma_dev;
	struct gdma_context *gc;
	struct device *dev;
	skb_frag_t *frag;
	dma_addr_t da;
	int i;

	gc = ana_to_gdma_context(gd);
	dev = gc->dev;
	da = dma_map_single(dev, skb->data, skb_headlen(skb), DMA_TO_DEVICE);

	if (dma_mapping_error(dev, da))
		return -ENOMEM;

	ash->dma_handle[0] = da;
	ash->size[0] = skb_headlen(skb);

	tp->wqe_req.sgl[0].address = ash->dma_handle[0];
	tp->wqe_req.sgl[0].mem_key = gd->gpa_mkey;
	tp->wqe_req.sgl[0].size = ash->size[0];

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		frag = &skb_shinfo(skb)->frags[i];
		da = skb_frag_dma_map(dev, frag, 0, skb_frag_size(frag),
				      DMA_TO_DEVICE);

		if (dma_mapping_error(dev, da))
			goto frag_err;

		ash->dma_handle[i + 1] = da;
		ash->size[i + 1] = skb_frag_size(frag);

		tp->wqe_req.sgl[i + 1].address = ash->dma_handle[i + 1];
		tp->wqe_req.sgl[i + 1].mem_key = gd->gpa_mkey;
		tp->wqe_req.sgl[i + 1].size = ash->size[i + 1];
	}

	return 0;

frag_err:
	for (i = i - 1; i >= 0; i--)
		dma_unmap_page(dev, ash->dma_handle[i + 1], ash->size[i + 1],
			       DMA_TO_DEVICE);

	dma_unmap_single(dev, ash->dma_handle[0], ash->size[0], DMA_TO_DEVICE);

	return -ENOMEM;
}

static int ana_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	enum ana_tx_pkt_format pkt_fmt = ANA_SHORT_PKT_FMT;
	struct ana_context *ac = netdev_priv(ndev);
	u16 txq_idx = skb_get_queue_mapping(skb);
	bool ipv4 = false, ipv6 = false;
	struct ana_tx_package pkg = {};
	struct netdev_queue *net_txq;
	struct ana_stats *tx_stats;
	struct gdma_queue *gdma_sq;
	unsigned int csum_type;
	struct ana_txq *txq;
	struct ana_cq *cq;
	int err, len;

	if (unlikely(!ac->port_is_up))
		goto tx_drop;

	if (skb_cow_head(skb, ANA_HEADROOM))
		goto tx_drop_count;

	txq = &ac->tx_qp[txq_idx].txq;
	gdma_sq = txq->gdma_sq;
	cq = &ac->tx_qp[txq_idx].tx_cq;

	pkg.tx_oob.s_oob.vcq_num = cq->gdma_id;
	pkg.tx_oob.s_oob.vsq_frame = txq->vsq_frame;

	if (txq->vp_offset > ANA_SHORT_VPORT_OFFSET_MAX) {
		pkg.tx_oob.l_oob.long_vp_offset = txq->vp_offset;
		pkt_fmt = ANA_LONG_PKT_FMT;
	} else {
		pkg.tx_oob.s_oob.short_vp_offset = txq->vp_offset;
	}

	pkg.tx_oob.s_oob.pkt_fmt = pkt_fmt;

	if (pkt_fmt == ANA_SHORT_PKT_FMT)
		pkg.wqe_req.inline_oob_size = sizeof(struct ana_tx_short_oob);
	else
		pkg.wqe_req.inline_oob_size = sizeof(struct ana_tx_oob);

	pkg.wqe_req.inline_oob_data = &pkg.tx_oob;
	pkg.wqe_req.flags = 0;
	pkg.wqe_req.client_data_unit = 0;

	pkg.wqe_req.num_sge = 1 + skb_shinfo(skb)->nr_frags;
	WARN_ON(pkg.wqe_req.num_sge > 30);

	if (pkg.wqe_req.num_sge <= ARRAY_SIZE(pkg.sgl_array)) {
		pkg.wqe_req.sgl = pkg.sgl_array;
	} else {
		pkg.sgl_ptr = kmalloc_array(pkg.wqe_req.num_sge,
					    sizeof(struct gdma_sge),
					    GFP_ATOMIC);
		if (!pkg.sgl_ptr)
			goto tx_drop_count;

		pkg.wqe_req.sgl = pkg.sgl_ptr;
	}

	if (skb->protocol == htons(ETH_P_IP))
		ipv4 = true;
	else if (skb->protocol == htons(ETH_P_IPV6))
		ipv6 = true;

	if (skb_is_gso(skb)) {
		pkg.tx_oob.s_oob.is_outer_ipv4 = ipv4;
		pkg.tx_oob.s_oob.is_outer_ipv6 = ipv6;

		pkg.tx_oob.s_oob.comp_iphdr_csum = 1;
		pkg.tx_oob.s_oob.comp_tcp_csum = 1;
		pkg.tx_oob.s_oob.trans_off = skb_transport_offset(skb);

		pkg.wqe_req.client_data_unit = skb_shinfo(skb)->gso_size;
		pkg.wqe_req.flags = GDMA_WR_OOB_IN_SGL |
				    GDMA_WR_PAD_DATA_BY_FIRST_SGE;
		if (ipv4) {
			ip_hdr(skb)->tot_len = 0;
			ip_hdr(skb)->check = 0;
			tcp_hdr(skb)->check =
				~csum_tcpudp_magic(ip_hdr(skb)->saddr,
						   ip_hdr(skb)->daddr, 0,
						   IPPROTO_TCP, 0);
		} else {
			ipv6_hdr(skb)->payload_len = 0;
			tcp_hdr(skb)->check =
				~csum_ipv6_magic(&ipv6_hdr(skb)->saddr,
						 &ipv6_hdr(skb)->daddr, 0,
						 IPPROTO_TCP, 0);
		}
	} else if (skb->ip_summed == CHECKSUM_PARTIAL) {
		csum_type = ana_checksum_info(skb);

		if (csum_type == IPPROTO_TCP) {
			pkg.tx_oob.s_oob.is_outer_ipv4 = ipv4;
			pkg.tx_oob.s_oob.is_outer_ipv6 = ipv6;

			pkg.tx_oob.s_oob.comp_tcp_csum = 1;
			pkg.tx_oob.s_oob.trans_off = skb_transport_offset(skb);

		} else if (csum_type == IPPROTO_UDP) {
			pkg.tx_oob.s_oob.is_outer_ipv4 = ipv4;
			pkg.tx_oob.s_oob.is_outer_ipv6 = ipv6;

			pkg.tx_oob.s_oob.comp_udp_csum = 1;
		} else {
			/* Can't do offload of this type of checksum */
			if (skb_checksum_help(skb))
				goto free_sgl_ptr;
		}
	}

	if (ana_map_skb(skb, ac, &pkg))
		goto free_sgl_ptr;

	skb_queue_tail(&txq->pending_skbs, skb);

	len = skb->len;
	net_txq = netdev_get_tx_queue(ndev, txq_idx);

	err = gdma_post_work_request(gdma_sq, &pkg.wqe_req,
				     (struct gdma_posted_wqe_info *)skb->cb);
	if (!gdma_can_tx(gdma_sq)) {
		netif_tx_stop_queue(net_txq);
		ac->eth_stats.stop_queue++;
	}

	if (err) {
		(void)skb_dequeue_tail(&txq->pending_skbs);
		netdev_warn(ndev, "Failed to post TX OOB: %d\n", err);
		err = NETDEV_TX_BUSY;
		goto tx_busy;
	}

	err = NETDEV_TX_OK;
	atomic_inc(&txq->pending_sends);

	gdma_wq_ring_doorbell(ana_to_gdma_context(gdma_sq->gdma_dev), gdma_sq);

	/* skb may be freed after gdma_post_work_request. Do not use it. */
	skb = NULL;

	tx_stats = &txq->stats;
	u64_stats_update_begin(&tx_stats->syncp);
	tx_stats->packets++;
	tx_stats->bytes += len;
	u64_stats_update_end(&tx_stats->syncp);

tx_busy:
	if (netif_tx_queue_stopped(net_txq) && gdma_can_tx(gdma_sq)) {
		netif_tx_wake_queue(net_txq);
		ac->eth_stats.wake_queue++;
	}

	kfree(pkg.sgl_ptr);
	return err;

free_sgl_ptr:
	kfree(pkg.sgl_ptr);
tx_drop_count:
	ndev->stats.tx_dropped++;
tx_drop:
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

static void ana_get_stats64(struct net_device *ndev,
			    struct rtnl_link_stats64 *st)
{
	struct ana_context *ac = netdev_priv(ndev);
	unsigned int num_queues = ac->num_queues;
	struct ana_stats *stats;
	unsigned int start;
	u64 packets, bytes;
	int q;

	if (ac->start_remove)
		return;

	netdev_stats_to_stats64(st, &ndev->stats);

	for (q = 0; q < num_queues; q++) {
		stats = &ac->rxqs[q]->stats;

		do {
			start = u64_stats_fetch_begin_irq(&stats->syncp);
			packets = stats->packets;
			bytes = stats->bytes;
		} while (u64_stats_fetch_retry_irq(&stats->syncp, start));

		st->rx_packets += packets;
		st->rx_bytes += bytes;
	}

	for (q = 0; q < num_queues; q++) {
		stats = &ac->tx_qp[q].txq.stats;

		do {
			start = u64_stats_fetch_begin_irq(&stats->syncp);
			packets = stats->packets;
			bytes = stats->bytes;
		} while (u64_stats_fetch_retry_irq(&stats->syncp, start));

		st->tx_packets += packets;
		st->tx_bytes += bytes;
	}
}

static int ana_get_tx_queue(struct net_device *ndev, struct sk_buff *skb,
			    int old_q)
{
	struct ana_context *ac = netdev_priv(ndev);
	struct sock *sk = skb->sk;
	int txq;

	txq = ac->ind_table[skb_get_hash(skb) & (ANA_INDIRECT_TABLE_SIZE - 1)];

	if (txq != old_q && sk && sk_fullsock(sk) &&
	    rcu_access_pointer(sk->sk_dst_cache))
		sk_tx_queue_set(sk, txq);

	return txq;
}

static u16 ana_select_queue(struct net_device *ndev, struct sk_buff *skb,
			    struct net_device *sb_dev)
{
	int txq;

	if (ndev->real_num_tx_queues == 1)
		return 0;

	txq = sk_tx_queue_get(skb->sk);

	if (txq < 0 || skb->ooo_okay || txq >= ndev->real_num_tx_queues) {
		if (skb_rx_queue_recorded(skb))
			txq = skb_get_rx_queue(skb);
		else
			txq = ana_get_tx_queue(ndev, skb, txq);
	}

	return txq;
}

static const struct net_device_ops ana_devops = {
	.ndo_open = ana_open,
	.ndo_stop = ana_close,
	.ndo_select_queue = ana_select_queue,
	.ndo_start_xmit = ana_start_xmit,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_get_stats64 = ana_get_stats64,
};

static void ana_cleanup_context(struct ana_context *ac)
{
	struct gdma_dev *gd = ac->gdma_dev;

	gdma_deregister_device(gd);

	kfree(ac->rxqs);
	ac->rxqs = NULL;
}

static int ana_init_context(struct ana_context *ac)
{
	struct gdma_dev *gd = ac->gdma_dev;
	int err;

	gd->pdid = INVALID_PDID;
	gd->doorbell = INVALID_DOORBELL;

	ac->rxqs = kcalloc(ac->num_queues, sizeof(struct ana_rxq *),
			   GFP_KERNEL);
	if (!ac->rxqs)
		return -ENOMEM;

	err = gdma_register_device(gd);
	if (err) {
		kfree(ac->rxqs);
		ac->rxqs = NULL;
		return err;
	}

	return 0;
}

static int ana_send_request(struct ana_context *ac, void *in_buf,
			    u32 in_buf_len, void *out_buf, u32 out_buf_len)
{
	struct gdma_context *gc = ana_to_gdma_context(ac->gdma_dev);
	struct gdma_send_ana_message_resp *resp = NULL;
	struct gdma_send_ana_message_req *req = NULL;
	struct net_device *ndev = ac->ndev;
	int err;

	if (is_gdma_msg_len(in_buf_len, out_buf_len, in_buf)) {
		struct gdma_req_hdr *g_req = in_buf;
		struct gdma_resp_hdr *g_resp = out_buf;

		static atomic_t act_id;

		g_req->dev_id = gc->ana.dev_id;
		g_req->activity_id = atomic_inc_return(&act_id);

		err = gdma_send_request(gc, in_buf_len, in_buf, out_buf_len,
					out_buf);
		if (err || g_resp->status) {
			netdev_err(ndev, "Send GDMA message failed: %d, 0x%x\n",
				   err, g_resp->status);
			return -EPROTO;
		}

		if (g_req->dev_id.as_uint32 != g_resp->dev_id.as_uint32 ||
		    g_req->activity_id != g_resp->activity_id) {
			netdev_err(ndev, "Wrong GDMA response: %x,%x,%x,%x\n",
				   g_req->dev_id.as_uint32,
				   g_resp->dev_id.as_uint32, g_req->activity_id,
				   g_resp->activity_id);
			return -EPROTO;
		}

		return 0;

	} else {
		u32 req_size = sizeof(*req) + in_buf_len;
		u32 resp_size = sizeof(*resp) + out_buf_len;

		req = kzalloc(req_size, GFP_KERNEL);
		if (!req) {
			err = -ENOMEM;
			goto out;
		}

		resp = kzalloc(resp_size, GFP_KERNEL);
		if (!resp) {
			err = -ENOMEM;
			goto out;
		}

		req->hdr.dev_id = gc->ana.dev_id;
		req->msg_size = in_buf_len;
		req->response_size = out_buf_len;
		memcpy(req->message, in_buf, in_buf_len);

		err = gdma_send_request(gc, req_size, req, resp_size, resp);
		if (err || resp->hdr.status) {
			netdev_err(ndev, "Send ANA message failed: %d, 0x%x\n",
				   err, resp->hdr.status);
			if (!err)
				err = -EPROTO;
			goto out;
		}

		memcpy(out_buf, resp->response, out_buf_len);
	}

out:
	kfree(resp);
	kfree(req);
	return err;
}

static int ana_verify_gdma_resp_hdr(const struct gdma_resp_hdr *resp_hdr,
				    const enum ana_command_code expected_code,
				    const u32 min_size)
{
	if (resp_hdr->response.msg_type != expected_code)
		return -EPROTO;

	if (resp_hdr->response.msg_version < GDMA_MESSAGE_V1)
		return -EPROTO;

	if (resp_hdr->response.msg_size < min_size)
		return -EPROTO;

	return 0;
}

static int ana_query_client_cfg(struct ana_context *ac, u32 drv_major_ver,
				u32 drv_minor_ver, u32 drv_micro_ver,
				u16 *max_num_vports)
{
	struct ana_query_client_cfg_resp resp = {};
	struct ana_query_client_cfg_req req = {};
	int err = 0;

	gdma_init_req_hdr(&req.hdr, ANA_QUERY_CLIENT_CONFIG,
			  sizeof(req), sizeof(resp));
	req.drv_major_ver = drv_major_ver;
	req.drv_minor_ver = drv_minor_ver;
	req.drv_micro_ver = drv_micro_ver;

	err = ana_send_request(ac, &req, sizeof(req), &resp, sizeof(resp));
	if (err) {
		netdev_err(ac->ndev, "Failed to query config: %d", err);
		return err;
	}

	err = ana_verify_gdma_resp_hdr(&resp.hdr, ANA_QUERY_CLIENT_CONFIG,
				       sizeof(resp));
	if (err || resp.hdr.status) {
		netdev_err(ac->ndev, "Invalid query result: %d, 0x%x\n", err,
			   resp.hdr.status);
		if (!err)
			err = -EPROTO;
		return err;
	}

	*max_num_vports = resp.max_num_vports;

	return 0;
}

static int ana_query_vport_cfg(struct ana_context *ac, u32 vport_index,
			       u32 *max_sq, u32 *max_rq, u32 *num_indir_entry)
{
	struct ana_query_vport_cfg_resp resp = {};
	struct ana_query_vport_cfg_req req = {};
	int err;

	gdma_init_req_hdr(&req.hdr, ANA_QUERY_VPORT_CONFIG,
			  sizeof(req), sizeof(resp));

	req.vport_index = vport_index;

	err = ana_send_request(ac, &req, sizeof(req), &resp, sizeof(resp));
	if (err)
		return err;

	err = ana_verify_gdma_resp_hdr(&resp.hdr, ANA_QUERY_VPORT_CONFIG,
				       sizeof(resp));
	if (err)
		return err;

	if (resp.hdr.status)
		return -EPROTO;

	*max_sq = resp.max_num_sq;
	*max_rq = resp.max_num_rq;
	*num_indir_entry = resp.num_indirection_ent;

	ac->default_vport = resp.vport;
	memcpy(ac->mac_addr, resp.mac_addr, ETH_ALEN);

	return 0;
}

static int ana_cfg_vport(struct ana_context *ac, u32 protection_dom_id,
			 u32 doorbell_pg_id)
{
	struct ana_config_vport_resp resp = {};
	struct ana_config_vport_req req = {};
	int err;

	gdma_init_req_hdr(&req.hdr, ANA_CONFIG_VPORT_TX,
			  sizeof(req), sizeof(resp));
	req.vport = ac->default_vport;
	req.pdid = protection_dom_id;
	req.doorbell_pageid = doorbell_pg_id;

	err = ana_send_request(ac, &req, sizeof(req), &resp, sizeof(resp));
	if (err) {
		netdev_err(ac->ndev, "Failed to configure vPort TX: %d\n", err);
		goto out;
	}

	err = ana_verify_gdma_resp_hdr(&resp.hdr, ANA_CONFIG_VPORT_TX,
				       sizeof(resp));
	if (err || resp.hdr.status) {
		netdev_err(ac->ndev, "Failed to configure vPort TX: %d, 0x%x\n",
			   err, resp.hdr.status);
		if (!err)
			err = -EPROTO;

		goto out;
	}

	ac->tx_shortform_allowed = resp.short_form_allowed;
	ac->tx_vp_offset = resp.tx_vport_offset;
out:
	return err;
}

static int ana_cfg_vport_steering(struct ana_context *ac, enum TRI_STATE rx,
				  bool update_default_rxobj, bool update_key,
				  bool update_tab)
{
	u16 num_entries = ANA_INDIRECT_TABLE_SIZE;
	struct ana_cfg_rx_steer_req *req = NULL;
	struct ana_cfg_rx_steer_resp resp = {};
	struct net_device *ndev = ac->ndev;
	ana_handle_t *req_indir_tab;
	u32 req_buf_size;
	int err;

	if (update_key && !ac->hashkey)
		return -EINVAL;

	if (update_tab && !ac->rxobj_table)
		return -EINVAL;

	req_buf_size = sizeof(*req) + sizeof(ana_handle_t) * num_entries;
	req = kzalloc(req_buf_size, GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	gdma_init_req_hdr(&req->hdr, ANA_CONFIG_VPORT_RX, req_buf_size,
			  sizeof(resp));

	req->vport = ac->default_vport;
	req->num_indir_entries = num_entries;
	req->indir_tab_offset = sizeof(*req);
	req->rx_enable = rx;
	req->rss_enable = ac->rss_state;
	req->update_default_rxobj = update_default_rxobj;
	req->update_hashkey = update_key;
	req->update_indir_tab = update_tab;
	req->default_rxobj = ac->default_rxobj;

	if (update_key)
		memcpy(&req->hashkey, ac->hashkey, ANA_HASH_KEY_SIZE);

	if (update_tab) {
		req_indir_tab = (ana_handle_t *)(req + 1);
		memcpy(req_indir_tab, ac->rxobj_table,
		       req->num_indir_entries * sizeof(ana_handle_t));
	}

	err = ana_send_request(ac, req, req_buf_size, &resp, sizeof(resp));
	if (err) {
		netdev_err(ndev, "Failed to configure vPort RX: %d\n", err);
		goto out;
	}

	err = ana_verify_gdma_resp_hdr(&resp.hdr, ANA_CONFIG_VPORT_RX,
				       sizeof(resp));
	if (err) {
		netdev_err(ndev, "vPort RX configuration failed: %d\n", err);
		goto out;
	}

	if (resp.hdr.status) {
		netdev_err(ndev, "vPort RX configuration failed: 0x%x\n",
			   resp.hdr.status);
		err = -EPROTO;
	}
out:
	kfree(req);
	return err;
}

static int ana_create_wq_obj(struct ana_context *ac, ana_handle_t vport,
			     u32 wq_type, struct ana_obj_spec *wq_spec,
			     struct ana_obj_spec *cq_spec, ana_handle_t *wq_obj)
{
	struct ana_create_wqobj_resp resp = {};
	struct ana_create_wqobj_req req = {};
	struct net_device *ndev = ac->ndev;
	int err;

	gdma_init_req_hdr(&req.hdr, ANA_CREATE_WQ_OBJ,
			  sizeof(req), sizeof(resp));
	req.vport = vport;
	req.wq_type = wq_type;
	req.wq_gdma_region = wq_spec->gdma_region;
	req.cq_gdma_region = cq_spec->gdma_region;
	req.wq_size = wq_spec->queue_size;
	req.cq_size = cq_spec->queue_size;
	req.cq_moderation_ctx_id = cq_spec->modr_ctx_id;
	req.cq_parent_qid = cq_spec->attached_eq;

	err = ana_send_request(ac, &req, sizeof(req), &resp, sizeof(resp));
	if (err) {
		netdev_err(ndev, "Failed to create WQ object: %d\n", err);
		goto out;
	}

	err = ana_verify_gdma_resp_hdr(&resp.hdr, ANA_CREATE_WQ_OBJ,
				       sizeof(resp));
	if (err || resp.hdr.status) {
		netdev_err(ndev, "Failed to create WQ object: %d, 0x%x\n", err,
			   resp.hdr.status);
		if (!err)
			err = -EPROTO;
		goto out;
	}

	if (resp.wq_obj == INVALID_ANA_HANDLE) {
		netdev_err(ndev, "Failed to create WQ object: invalid handle\n");
		err = -EPROTO;
		goto out;
	}

	*wq_obj = resp.wq_obj;
	wq_spec->queue_index = resp.wq_id;
	cq_spec->queue_index = resp.cq_id;

	return 0;

out:
	return err;
}

static void ana_destroy_wq_obj(struct ana_context *ac, u32 wq_type,
			       ana_handle_t wq_obj)
{
	struct ana_destroy_wqobj_resp resp = {};
	struct ana_destroy_wqobj_req req = {};
	struct net_device *ndev = ac->ndev;
	int err;

	gdma_init_req_hdr(&req.hdr, ANA_DESTROY_WQ_OBJ,
			  sizeof(req), sizeof(resp));
	req.wq_type = wq_type;
	req.wqobj_handle = wq_obj;

	err = ana_send_request(ac, &req, sizeof(req), &resp, sizeof(resp));
	if (err) {
		netdev_err(ndev, "Failed to destroy WQ object: %d\n", err);
		return;
	}

	err = ana_verify_gdma_resp_hdr(&resp.hdr, ANA_DESTROY_WQ_OBJ,
				       sizeof(resp));
	if (err || resp.hdr.status)
		netdev_err(ndev, "Failed to destroy WQ object: %d, 0x%x\n", err,
			   resp.hdr.status);
}

static void ana_init_cqe_pollbuf(struct gdma_comp *cqe_poll_buf)
{
	int i;

	for (i = 0; i < CQE_POLLING_BUFFER; i++)
		memset(&cqe_poll_buf[i], 0, sizeof(struct gdma_comp));
}

static void ana_destroy_eq(struct gdma_context *gc, struct ana_context *ac)
{
	struct gdma_queue *eq;
	int i;

	if (!ac->eqs)
		return;

	for (i = 0; i < ac->num_queues; i++) {
		eq = ac->eqs[i].eq;
		if (!eq)
			continue;

		gdma_destroy_queue(gc, eq);
	}

	kfree(ac->eqs);
	ac->eqs = NULL;
}

static int ana_create_eq(struct ana_context *ac)
{
	struct gdma_dev *gd = ac->gdma_dev;
	struct gdma_queue_spec spec = {};
	int err;
	int i;

	ac->eqs = kcalloc(ac->num_queues, sizeof(struct ana_eq),
			  GFP_KERNEL);
	if (!ac->eqs)
		return -ENOMEM;

	spec.type = GDMA_EQ;
	spec.monitor_avl_buf = false;
	spec.queue_size = EQ_SIZE;
	spec.eq.callback = NULL;
	spec.eq.context = ac->eqs;
	spec.eq.log2_throttle_limit = LOG2_EQ_THROTTLE;

	for (i = 0; i < ac->num_queues; i++) {
		ana_init_cqe_pollbuf(ac->eqs[i].cqe_poll);

		err = gdma_create_ana_eq(gd, &spec, &ac->eqs[i].eq);
		if (err)
			goto out;
	}

	return 0;
out:
	ana_destroy_eq(ana_to_gdma_context(gd), ac);
	return err;
}

static int gdma_move_wq_tail(struct gdma_queue *wq, u32 num_units)
{
	u32 used_space_old;
	u32 used_space_new;

	used_space_old = wq->head - wq->tail;
	used_space_new = wq->head - (wq->tail + num_units);

	if (used_space_new > used_space_old) {
		WARN_ON(1);
		return -ERANGE;
	}

	wq->tail += num_units;
	return 0;
}

static void ana_unmap_skb(struct sk_buff *skb, struct ana_context *ac)
{
	struct gdma_context *gc = ana_to_gdma_context(ac->gdma_dev);
	struct ana_skb_head *ash = (struct ana_skb_head *)skb->head;
	struct device *dev = gc->dev;
	int i;

	dma_unmap_single(dev, ash->dma_handle[0], ash->size[0], DMA_TO_DEVICE);

	for (i = 1; i < skb_shinfo(skb)->nr_frags + 1; i++)
		dma_unmap_page(dev, ash->dma_handle[i], ash->size[i],
			       DMA_TO_DEVICE);
}

static void ana_poll_tx_cq(struct ana_cq *cq)
{
	struct net_device *ndev = cq->gdma_cq->gdma_dev->driver_data;
	struct gdma_comp *completions = cq->gdma_comp_buf;
	struct gdma_queue *eqkb = cq->gdma_cq->cq.parent;
	struct ana_context *ac = netdev_priv(ndev);
	struct gdma_posted_wqe_info *wqe_info;
	unsigned int pkt_transmitted = 0;
	unsigned int wqe_unit_cnt = 0;
	struct ana_txq *txq = cq->txq;
	struct netdev_queue *net_txq;
	unsigned int avail_space;
	struct gdma_queue *wq;
	struct sk_buff *skb;
	bool txq_stopped;
	int comp_read;
	int i;

	comp_read = gdma_poll_cq(cq->gdma_cq, completions, CQE_POLLING_BUFFER);

	for (i = 0; i < comp_read; i++) {
		struct ana_tx_comp_oob *cqe_oob;

		if (WARN_ON(!completions[i].is_sq))
			return;

		cqe_oob = (struct ana_tx_comp_oob *)completions[i].cqe_data;
		if (WARN_ON(cqe_oob->cqe_hdr.client_type != ANA_CQE_COMPLETION))
			return;

		switch (cqe_oob->cqe_hdr.cqe_type) {
		case CQE_TX_OKAY:
			break;

		case CQE_TX_SA_DROP:
		case CQE_TX_MTU_DROP:
		case CQE_TX_INVALID_OOB:
		case CQE_TX_INVALID_ETH_TYPE:
		case CQE_TX_HDR_PROCESSING_ERROR:
		case CQE_TX_VF_DISABLED:
		case CQE_TX_VPORT_IDX_OUT_OF_RANGE:
		case CQE_TX_VPORT_DISABLED:
		case CQE_TX_VLAN_TAGGING_VIOLATION:
			WARN(1, "TX: CQE error %d: ignored.\n",
			     cqe_oob->cqe_hdr.cqe_type);
			break;

		default:
			/* If the CQE type is unexpected, log an error, assert,
			 * and go through the error path.
			 */
			WARN(1, "TX: Unexpected CQE type %d: HW BUG?\n",
			     cqe_oob->cqe_hdr.cqe_type);
			return;
		}

		if (WARN_ON(txq->gdma_txq_id != completions[i].wq_num))
			return;

		skb = skb_dequeue(&txq->pending_skbs);
		if (WARN_ON(!skb))
			return;

		wqe_info = (struct gdma_posted_wqe_info *)skb->cb;
		wqe_unit_cnt += wqe_info->wqe_size_in_bu;

		ana_unmap_skb(skb, ac);

		napi_consume_skb(skb, eqkb->eq.budget);

		pkt_transmitted++;
	}

	if (WARN_ON(wqe_unit_cnt == 0))
		return;

	gdma_move_wq_tail(txq->gdma_sq, wqe_unit_cnt);

	wq = txq->gdma_sq;
	avail_space = gdma_wq_avail_space(wq);

	/* Ensure tail updated before checking q stop */
	smp_mb();

	net_txq = txq->net_txq;
	txq_stopped = netif_tx_queue_stopped(net_txq);

	if (txq_stopped && ac->port_is_up && avail_space >= MAX_TX_WQE_SIZE) {
		netif_tx_wake_queue(net_txq);
		ac->eth_stats.wake_queue++;
	}

	if (atomic_sub_return(pkt_transmitted, &txq->pending_sends) < 0)
		WARN_ON(1);
}

static void ana_post_pkt_rxq(struct ana_rxq *rxq)
{
	struct ana_recv_buf_oob *recv_buf_oob;
	u32 curr_index;
	int err;

	curr_index = rxq->buf_index++;
	if (rxq->buf_index == rxq->num_rx_buf)
		rxq->buf_index = 0;

	recv_buf_oob = &rxq->rx_oobs[curr_index];

	err = gdma_post_and_ring(rxq->gdma_rq, &recv_buf_oob->wqe_req,
				 &recv_buf_oob->wqe_inf);
	if (WARN_ON(err))
		return;

	WARN_ON(recv_buf_oob->wqe_inf.wqe_size_in_bu != 1);
}

static void ana_rx_skb(void *buf_va, struct ana_rxcomp_oob *cqe,
		       struct ana_rxq *rxq)
{
	struct ana_stats *rx_stats = &rxq->stats;
	struct net_device *ndev = rxq->ndev;
	uint pkt_len = cqe->ppi[0].pkt_len;
	u16 rxq_idx = rxq->rxq_idx;
	struct napi_struct *napi;
	struct ana_context *ac;
	struct gdma_queue *eq;
	struct sk_buff *skb;
	u32 hash_value;

	ac = netdev_priv(ndev);
	eq = ac->eqs[rxq_idx].eq;
	eq->eq.work_done++;
	napi = &eq->eq.napi;

	if (!buf_va) {
		++ndev->stats.rx_dropped;
		return;
	}

	skb = build_skb(buf_va, PAGE_SIZE);

	if (!skb) {
		free_page((unsigned long)buf_va);
		++ndev->stats.rx_dropped;
		return;
	}

	skb_put(skb, pkt_len);
	skb->dev = napi->dev;

	skb->protocol = eth_type_trans(skb, ndev);
	skb_checksum_none_assert(skb);
	skb_record_rx_queue(skb, rxq_idx);

	if ((ndev->features & NETIF_F_RXCSUM) && cqe->rx_iphdr_csum_succeed) {
		if (cqe->rx_tcp_csum_succeed || cqe->rx_udp_csum_succeed)
			skb->ip_summed = CHECKSUM_UNNECESSARY;
	}

	if (cqe->rx_hashtype != 0 && (ndev->features & NETIF_F_RXHASH)) {
		hash_value = cqe->ppi[0].pkt_hash;

		if (cqe->rx_hashtype & ANA_HASH_L4)
			skb_set_hash(skb, hash_value, PKT_HASH_TYPE_L4);
		else
			skb_set_hash(skb, hash_value, PKT_HASH_TYPE_L3);
	}

	napi_gro_receive(napi, skb);

	u64_stats_update_begin(&rx_stats->syncp);
	rx_stats->packets++;
	rx_stats->bytes += pkt_len;
	u64_stats_update_end(&rx_stats->syncp);
}

static void ana_process_rx_cqe(struct ana_rxq *rxq, struct ana_cq *cq,
			       struct gdma_comp *cqe)
{
	struct gdma_context *gc = ana_to_gdma_context(rxq->gdma_rq->gdma_dev);
	struct ana_rxcomp_oob *oob = (struct ana_rxcomp_oob *)cqe->cqe_data;
	struct net_device *ndev = rxq->ndev;
	struct ana_recv_buf_oob *rxbuf_oob;
	struct device *dev = gc->dev;
	void *new_buf, *old_buf;
	struct page *new_page;
	u32 curr, pktlen;
	dma_addr_t da;

	switch (oob->cqe_hdr.cqe_type) {
	case CQE_RX_OKAY:
		break;

	case CQE_RX_TRUNCATED:
		netdev_err(ndev, "Dropped a truncated packet\n");
		return;

	case CQE_RX_COALESCED_4:
		netdev_err(ndev, "RX coalescing is unsupported\n");
		return;

	case CQE_RX_OBJECT_FENCE:
		netdev_err(ndev, "RX Fencing is unsupported\n");
		return;

	default:
		netdev_err(ndev, "Unknown RX CQE type = %d\n",
			   oob->cqe_hdr.cqe_type);
		return;
	}

	if (oob->cqe_hdr.cqe_type != CQE_RX_OKAY)
		return;

	pktlen = oob->ppi[0].pkt_len;

	if (pktlen == 0) {
		/* data packets should never have packetlength of zero */
		netdev_err(ndev, "RX pkt len=0, rq=%u, cq=%u, rxobj=0x%llx\n",
			   rxq->gdma_id, cq->gdma_id, rxq->rxobj);
		return;
	}

	curr = rxq->buf_index;
	rxbuf_oob = &rxq->rx_oobs[curr];
	WARN_ON(rxbuf_oob->wqe_inf.wqe_size_in_bu != 1);

	new_page = alloc_page(GFP_ATOMIC);

	if (new_page) {
		da = dma_map_page(dev, new_page, 0, rxq->datasize,
				  DMA_FROM_DEVICE);

		if (dma_mapping_error(dev, da)) {
			__free_page(new_page);
			new_page = NULL;
		}
	}

	new_buf = new_page ? page_to_virt(new_page) : NULL;

	if (new_buf) {
		dma_unmap_page(dev, rxbuf_oob->buf_dma_addr, rxq->datasize,
			       DMA_FROM_DEVICE);

		old_buf = rxbuf_oob->buf_va;

		/* refresh the rxbuf_oob with the new page */
		rxbuf_oob->buf_va = new_buf;
		rxbuf_oob->buf_dma_addr = da;
		rxbuf_oob->sgl[0].address = rxbuf_oob->buf_dma_addr;
	} else {
		old_buf = NULL; /* drop the packet if no memory */
	}

	ana_rx_skb(old_buf, oob, rxq);

	gdma_move_wq_tail(rxq->gdma_rq, rxbuf_oob->wqe_inf.wqe_size_in_bu);

	ana_post_pkt_rxq(rxq);
}

static void ana_poll_rx_cq(struct ana_cq *cq)
{
	struct gdma_comp *comp = cq->gdma_comp_buf;
	u32 comp_read, i;

	comp_read = gdma_poll_cq(cq->gdma_cq, comp, CQE_POLLING_BUFFER);
	WARN_ON(comp_read > CQE_POLLING_BUFFER);

	for (i = 0; i < comp_read; i++) {
		if (WARN_ON(comp[i].is_sq))
			return;

		/* verify recv cqe references the right rxq */
		if (WARN_ON(comp[i].wq_num != cq->rxq->gdma_id))
			return;

		ana_process_rx_cqe(cq->rxq, cq, &comp[i]);
	}
}

static void ana_cq_handler(void *context, struct gdma_queue *gdma_queue)
{
	struct ana_cq *cq = context;

	WARN_ON(cq->gdma_cq != gdma_queue);

	if (cq->type == ANA_CQ_TYPE_RX)
		ana_poll_rx_cq(cq);
	else
		ana_poll_tx_cq(cq);

	gdma_arm_cq(gdma_queue);
}

static void ana_deinit_cq(struct ana_context *ac, struct ana_cq *cq)
{
	if (!cq->gdma_cq)
		return;

	gdma_destroy_queue(ana_to_gdma_context(ac->gdma_dev), cq->gdma_cq);
}

static void ana_deinit_txq(struct ana_context *ac, struct ana_txq *txq)
{
	if (!txq->gdma_sq)
		return;

	gdma_destroy_queue(ana_to_gdma_context(ac->gdma_dev), txq->gdma_sq);
}

static void ana_destroy_txq(struct ana_context *ac)
{
	int i;

	if (!ac->tx_qp)
		return;

	for (i = 0; i < ac->num_queues; i++) {
		ana_destroy_wq_obj(ac, GDMA_SQ, ac->tx_qp[i].tx_object);

		ana_deinit_cq(ac, &ac->tx_qp[i].tx_cq);

		ana_deinit_txq(ac, &ac->tx_qp[i].txq);
	}

	kfree(ac->tx_qp);
	ac->tx_qp = NULL;
}

static int ana_create_txq(struct ana_context *ac, struct net_device *net)
{
	struct gdma_dev *gd = ac->gdma_dev;
	struct ana_obj_spec wq_spec;
	struct ana_obj_spec cq_spec;
	struct gdma_queue_spec spec;
	struct gdma_context *gc;
	struct ana_txq *txq;
	struct ana_cq *cq;
	u32 txq_size;
	u32 cq_size;
	int err;
	int i;

	ac->tx_qp = kcalloc(ac->num_queues, sizeof(struct ana_tx_qp),
			    GFP_KERNEL);
	if (!ac->tx_qp)
		return -ENOMEM;

	/*  The minimum size of the WQE is 32 bytes, hence
	 *  MAX_SEND_BUFFERS_PER_QUEUE represents the maximum number of WQEs
	 *  the send queue can store. This value is then used to size other
	 *  queues in the driver to prevent overflow.
	 *  SQ size must be divisible by PAGE_SIZE.
	 */
	txq_size = MAX_SEND_BUFFERS_PER_QUEUE * 32;
	BUILD_BUG_ON(txq_size % PAGE_SIZE != 0);

	cq_size = MAX_SEND_BUFFERS_PER_QUEUE * COMP_ENTRY_SIZE;
	cq_size = ALIGN(cq_size, PAGE_SIZE);

	gc = ana_to_gdma_context(gd);

	for (i = 0; i < ac->num_queues; i++) {
		ac->tx_qp[i].tx_object = INVALID_ANA_HANDLE;

		/* create SQ */
		txq = &ac->tx_qp[i].txq;

		u64_stats_init(&txq->stats.syncp);
		txq->net_txq = netdev_get_tx_queue(net, i);
		txq->vp_offset = ac->tx_vp_offset;
		skb_queue_head_init(&txq->pending_skbs);

		memset(&spec, 0, sizeof(spec));
		spec.type = GDMA_SQ;
		spec.monitor_avl_buf = true;
		spec.queue_size = txq_size;
		err = gdma_create_ana_wq_cq(gd, &spec, &txq->gdma_sq);
		if (err)
			goto out;

		/* create SQ's CQ */
		cq = &ac->tx_qp[i].tx_cq;
		cq->gdma_comp_buf = ac->eqs[i].cqe_poll;
		cq->type = ANA_CQ_TYPE_TX;

		cq->txq = txq;

		memset(&spec, 0, sizeof(spec));
		spec.type = GDMA_CQ;
		spec.monitor_avl_buf = false;
		spec.queue_size = cq_size;
		spec.cq.callback = ana_cq_handler;
		spec.cq.parent_eq = ac->eqs[i].eq;
		spec.cq.context = cq;
		err = gdma_create_ana_wq_cq(gd, &spec, &cq->gdma_cq);
		if (err)
			goto out;

		memset(&wq_spec, 0, sizeof(wq_spec));
		memset(&cq_spec, 0, sizeof(cq_spec));

		wq_spec.gdma_region = txq->gdma_sq->mem_info.gdma_region;
		wq_spec.queue_size = txq->gdma_sq->queue_size;

		cq_spec.gdma_region = cq->gdma_cq->mem_info.gdma_region;
		cq_spec.queue_size = cq->gdma_cq->queue_size;
		cq_spec.modr_ctx_id = 0;
		cq_spec.attached_eq = cq->gdma_cq->cq.parent->id;

		err = ana_create_wq_obj(ac, ac->default_vport, GDMA_SQ,
					&wq_spec, &cq_spec,
					&ac->tx_qp[i].tx_object);

		if (err)
			goto out;

		txq->gdma_sq->id = wq_spec.queue_index;
		cq->gdma_cq->id = cq_spec.queue_index;

		txq->gdma_sq->mem_info.gdma_region = GDMA_INVALID_DMA_REGION;
		cq->gdma_cq->mem_info.gdma_region = GDMA_INVALID_DMA_REGION;

		txq->gdma_txq_id = txq->gdma_sq->id;

		cq->gdma_id = cq->gdma_cq->id;

		if (cq->gdma_id >= gc->max_num_cq) {
			WARN_ON(1);
			return -EINVAL;
		}

		gc->cq_table[cq->gdma_id] = cq->gdma_cq;

		gdma_arm_cq(cq->gdma_cq);
	}

	return 0;

out:
	ana_destroy_txq(ac);
	return err;
}

static void gdma_napi_sync_for_rx(struct ana_rxq *rxq)
{
	struct net_device *ndev = rxq->ndev;
	u16 rxq_idx = rxq->rxq_idx;
	struct napi_struct *napi;
	struct ana_context *ac;
	struct gdma_queue *eq;

	ac = netdev_priv(ndev);
	eq = ac->eqs[rxq_idx].eq;
	napi = &eq->eq.napi;

	napi_synchronize(napi);
}

static void ana_destroy_rxq(struct ana_context *ac, struct ana_rxq *rxq,
			    bool validate_state)

{
	struct gdma_context *gc = ana_to_gdma_context(ac->gdma_dev);
	struct ana_recv_buf_oob *rx_oob;
	struct device *dev = gc->dev;
	int i;

	if (!rxq)
		return;

	if (validate_state)
		gdma_napi_sync_for_rx(rxq);

	ana_destroy_wq_obj(ac, GDMA_RQ, rxq->rxobj);

	ana_deinit_cq(ac, &rxq->rx_cq);

	for (i = 0; i < rxq->num_rx_buf; i++) {
		rx_oob = &rxq->rx_oobs[i];

		if (!rx_oob->buf_va)
			continue;

		dma_unmap_page(dev, rx_oob->buf_dma_addr, rxq->datasize,
			       DMA_FROM_DEVICE);

		free_page((unsigned long)rx_oob->buf_va);
		rx_oob->buf_va = NULL;
	}

	if (rxq->gdma_rq)
		gdma_destroy_queue(ana_to_gdma_context(ac->gdma_dev),
				   rxq->gdma_rq);

	kfree(rxq);
}

#define ANA_WQE_HEADER_SIZE 16
#define ANA_WQE_SGE_SIZE 16

static int ana_alloc_rx_wqe(struct ana_context *ac, struct ana_rxq *rxq,
			    u32 *rxq_size, u32 *cq_size)
{
	struct gdma_context *gc = ana_to_gdma_context(ac->gdma_dev);
	struct ana_recv_buf_oob *rx_oob;
	struct device *dev = gc->dev;
	struct page *page;
	dma_addr_t da;
	u32 buf_idx;

	WARN_ON(rxq->datasize == 0 || rxq->datasize > PAGE_SIZE);

	*rxq_size = 0;
	*cq_size = 0;

	for (buf_idx = 0; buf_idx < rxq->num_rx_buf; buf_idx++) {
		rx_oob = &rxq->rx_oobs[buf_idx];
		memset(rx_oob, 0, sizeof(*rx_oob));

		page = alloc_page(GFP_KERNEL);
		if (!page)
			return -ENOMEM;

		da = dma_map_page(dev, page, 0, rxq->datasize, DMA_FROM_DEVICE);

		if (dma_mapping_error(dev, da)) {
			__free_page(page);
			return -ENOMEM;
		}

		rx_oob->buf_va = page_to_virt(page);
		rx_oob->buf_dma_addr = da;

		rx_oob->num_sge = 1;
		rx_oob->sgl[0].address = rx_oob->buf_dma_addr;
		rx_oob->sgl[0].size = rxq->datasize;
		rx_oob->sgl[0].mem_key = ac->gdma_dev->gpa_mkey;

		rx_oob->wqe_req.sgl = rx_oob->sgl;
		rx_oob->wqe_req.num_sge = rx_oob->num_sge;
		rx_oob->wqe_req.inline_oob_size = 0;
		rx_oob->wqe_req.inline_oob_data = NULL;
		rx_oob->wqe_req.flags = 0;
		rx_oob->wqe_req.client_data_unit = 0;

		*rxq_size += ALIGN(ANA_WQE_HEADER_SIZE +
				   ANA_WQE_SGE_SIZE * rx_oob->num_sge, 32);
		*cq_size += COMP_ENTRY_SIZE;
	}

	return 0;
}

static int ana_push_wqe(struct ana_rxq *rxq)
{
	struct ana_recv_buf_oob *rx_oob;
	u32 buf_idx;
	int err;

	for (buf_idx = 0; buf_idx < rxq->num_rx_buf; buf_idx++) {
		rx_oob = &rxq->rx_oobs[buf_idx];

		err = gdma_post_and_ring(rxq->gdma_rq, &rx_oob->wqe_req,
					 &rx_oob->wqe_inf);
		if (err)
			return -ENOSPC;
	}

	return 0;
}

static struct ana_rxq *ana_create_rxq(struct ana_context *ac, u32 rxq_idx,
				      struct ana_eq *eq,
				      struct net_device *ndev)
{
	struct gdma_dev *gd = ac->gdma_dev;
	struct ana_obj_spec wq_spec;
	struct ana_obj_spec cq_spec;
	struct gdma_queue_spec spec;
	struct ana_cq *cq = NULL;
	struct gdma_context *gc;
	u32 cq_size, rq_size;
	struct ana_rxq *rxq;
	int err;

	gc = ana_to_gdma_context(gd);

	rxq = kzalloc(sizeof(*rxq) +
		      RX_BUFFERS_PER_QUEUE * sizeof(struct ana_recv_buf_oob),
		      GFP_KERNEL);
	if (!rxq)
		return NULL;

	rxq->ndev = ndev;
	rxq->num_rx_buf = RX_BUFFERS_PER_QUEUE;
	rxq->rxq_idx = rxq_idx;
	rxq->datasize = ALIGN(MAX_FRAME_SIZE, 64);
	rxq->rxobj = INVALID_ANA_HANDLE;

	err = ana_alloc_rx_wqe(ac, rxq, &rq_size, &cq_size);
	if (err)
		goto out;

	rq_size = ALIGN(rq_size, PAGE_SIZE);
	cq_size = ALIGN(cq_size, PAGE_SIZE);

	/* Create RQ */
	memset(&spec, 0, sizeof(spec));
	spec.type = GDMA_RQ;
	spec.monitor_avl_buf = true;
	spec.queue_size = rq_size;
	err = gdma_create_ana_wq_cq(gd, &spec, &rxq->gdma_rq);
	if (err)
		goto out;

	/* Create RQ's CQ */
	cq = &rxq->rx_cq;
	cq->gdma_comp_buf = eq->cqe_poll;
	cq->type = ANA_CQ_TYPE_RX;
	cq->rxq = rxq;

	memset(&spec, 0, sizeof(spec));
	spec.type = GDMA_CQ;
	spec.monitor_avl_buf = false;
	spec.queue_size = cq_size;
	spec.cq.callback = ana_cq_handler;
	spec.cq.parent_eq = eq->eq;
	spec.cq.context = cq;
	err = gdma_create_ana_wq_cq(gd, &spec, &cq->gdma_cq);
	if (err)
		goto out;

	memset(&wq_spec, 0, sizeof(wq_spec));
	memset(&cq_spec, 0, sizeof(cq_spec));
	wq_spec.gdma_region = rxq->gdma_rq->mem_info.gdma_region;
	wq_spec.queue_size = rxq->gdma_rq->queue_size;

	cq_spec.gdma_region = cq->gdma_cq->mem_info.gdma_region;
	cq_spec.queue_size = cq->gdma_cq->queue_size;
	cq_spec.modr_ctx_id = 0;
	cq_spec.attached_eq = cq->gdma_cq->cq.parent->id;

	err = ana_create_wq_obj(ac, ac->default_vport, GDMA_RQ,
				&wq_spec, &cq_spec, &rxq->rxobj);
	if (err)
		goto out;

	rxq->gdma_rq->id = wq_spec.queue_index;
	cq->gdma_cq->id = cq_spec.queue_index;

	rxq->gdma_rq->mem_info.gdma_region = GDMA_INVALID_DMA_REGION;
	cq->gdma_cq->mem_info.gdma_region = GDMA_INVALID_DMA_REGION;

	rxq->gdma_id = rxq->gdma_rq->id;
	cq->gdma_id = cq->gdma_cq->id;

	err = ana_push_wqe(rxq);
	if (err)
		goto out;

	if (cq->gdma_id >= gc->max_num_cq)
		goto out;

	gc->cq_table[cq->gdma_id] = cq->gdma_cq;

	gdma_arm_cq(cq->gdma_cq);

out:
	if (!err)
		return rxq;

	netdev_err(ndev, "Failed to create RXQ: err = %d\n", err);

	ana_destroy_rxq(ac, rxq, false);

	if (cq)
		ana_deinit_cq(ac, cq);

	return NULL;
}

static int ana_add_rx_queues(struct ana_context *ac, struct net_device *ndev)
{
	struct ana_rxq *rxq;
	int err = 0;
	int i;

	for (i = 0; i < ac->num_queues; i++) {
		rxq = ana_create_rxq(ac, i, &ac->eqs[i], ndev);
		if (!rxq) {
			err = -ENOMEM;
			goto out;
		}

		u64_stats_init(&rxq->stats.syncp);

		ac->rxqs[i] = rxq;
	}

	ac->default_rxobj = ac->rxqs[0]->rxobj;
out:
	return err;
}

static void ana_destroy_vport(struct ana_context *ac)
{
	struct ana_rxq *rxq;
	u32 rxq_idx;

	for (rxq_idx = 0; rxq_idx < ac->num_queues; rxq_idx++) {
		rxq = ac->rxqs[rxq_idx];
		if (!rxq)
			continue;

		ana_destroy_rxq(ac, rxq, true);
		ac->rxqs[rxq_idx] = NULL;
	}

	ana_destroy_txq(ac);
}

static int ana_create_vport(struct ana_context *ac, struct net_device *net)
{
	struct gdma_dev *gd = ac->gdma_dev;
	int err;

	ac->default_rxobj = INVALID_ANA_HANDLE;

	err = ana_cfg_vport(ac, gd->pdid, gd->doorbell);
	if (err)
		return err;

	err = ana_create_txq(ac, net);
	return err;
}

static void ana_key_table_init(struct ana_context *ac, bool reset_hash)
{
	int i;

	if (reset_hash)
		get_random_bytes(ac->hashkey, ANA_HASH_KEY_SIZE);

	for (i = 0; i < ANA_INDIRECT_TABLE_SIZE; i++)
		ac->ind_table[i] = i % ac->num_queues;
}

int ana_config_rss(struct ana_context *ac, enum TRI_STATE rx,
		   bool update_hash, bool update_tab)
{
	int err;
	int i;

	if (update_tab) {
		for (i = 0; i < ANA_INDIRECT_TABLE_SIZE; i++)
			ac->rxobj_table[i] = ac->rxqs[ac->ind_table[i]]->rxobj;
	}

	err = ana_cfg_vport_steering(ac, rx, true, update_hash, update_tab);
	return err;
}

int ana_detach(struct net_device *ndev)
{
	struct ana_context *ac = netdev_priv(ndev);
	struct ana_txq *txq;
	int i, err;

	ASSERT_RTNL();

	ac->port_st_save = ac->port_is_up;
	ac->port_is_up = false;
	ac->start_remove = true;

	/* Ensure port state updated before txq state */
	smp_wmb();

	netif_tx_disable(ndev);
	netif_carrier_off(ndev);

	/* No packet can be transmitted now since ac->port_is_up is false.
	 * There is still a tiny chance that ana_poll_tx_cq() can re-enable
	 * a txq because it may not timely see ac->port_is_up being cleared
	 * to false, but it doesn't matter since ana_start_xmit() drops any
	 * new packets due to ac->port_is_up being false.
	 *
	 * Drain all the in-flight TX packets
	 */
	for (i = 0; i < ac->num_queues; i++) {
		txq = &ac->tx_qp[i].txq;

		while (atomic_read(&txq->pending_sends) > 0)
			usleep_range(1000, 2000);
	}

	/* We're 100% sure the queues can no longer be woken up, because
	 * we're sure now ana_poll_tx_cq() can't be running.
	 */
	netif_device_detach(ndev);

	ac->rss_state = TRI_STATE_FALSE;
	err = ana_config_rss(ac, TRI_STATE_FALSE, false, false);
	if (err)
		netdev_err(ndev, "Failed to disable vPort: %d\n", err);

	ana_destroy_vport(ac);

	ana_destroy_eq(ana_to_gdma_context(ac->gdma_dev), ac);

	ana_cleanup_context(ac);

	/* TODO: Implement RX fencing */
	ssleep(1);

	return 0;
}

int ana_do_attach(struct net_device *ndev, bool reset_hash)
{
	struct ana_context *ac = netdev_priv(ndev);
	struct gdma_dev *gd = ac->gdma_dev;
	u32 max_txq, max_rxq, max_queues;
	u32 num_indirect_entries;
	u16 max_vports = 1;
	int err;

	err = ana_init_context(ac);
	if (err)
		return err;

	err = ana_query_client_cfg(ac, ANA_MAJOR_VERSION, ANA_MINOR_VERSION,
				   ANA_MICRO_VERSION, &max_vports);
	if (err)
		goto reset_ac;

	err = ana_query_vport_cfg(ac, 0, &max_txq, &max_rxq,
				  &num_indirect_entries);
	if (err) {
		netdev_err(ndev, "Failed to query info for vPort 0\n");
		goto reset_ac;
	}

	max_queues = min_t(u32, max_txq, max_rxq);
	if (ac->max_queues > max_queues)
		ac->max_queues = max_queues;

	if (ac->num_queues > ac->max_queues)
		ac->num_queues = ac->max_queues;

	memcpy(ndev->dev_addr, ac->mac_addr, ETH_ALEN);

	err = ana_create_eq(ac);
	if (err)
		goto reset_ac;

	err = ana_create_vport(ac, ndev);
	if (err)
		goto destroy_eq;

	netif_set_real_num_tx_queues(ndev, ac->num_queues);

	err = ana_add_rx_queues(ac, ndev);
	if (err)
		goto destroy_vport;

	ac->rss_state = ac->num_queues > 1 ? TRI_STATE_TRUE : TRI_STATE_FALSE;

	netif_set_real_num_rx_queues(ndev, ac->num_queues);

	ana_key_table_init(ac, reset_hash);

	err = ana_config_rss(ac, TRI_STATE_TRUE, true, true);
	if (err)
		goto destroy_vport;

	return 0;

destroy_vport:
	ana_destroy_vport(ac);
destroy_eq:
	ana_destroy_eq(ana_to_gdma_context(gd), ac);
reset_ac:
	gdma_deregister_device(gd);
	kfree(ac->rxqs);
	ac->rxqs = NULL;
	return err;
}

int ana_probe(struct gdma_dev *gd)
{
	struct gdma_context *gc = ana_to_gdma_context(gd);
	struct device *dev = gc->dev;
	struct net_device *ndev;
	struct ana_context *ac;
	int err;

	dev_info(dev, "Azure Network Adapter (ANA) Driver version: %d.%d.%d\n",
		 ANA_MAJOR_VERSION, ANA_MINOR_VERSION, ANA_MICRO_VERSION);

	ndev = alloc_etherdev_mq(sizeof(struct ana_context), gc->max_num_queue);
	if (!ndev)
		return -ENOMEM;

	gd->driver_data = ndev;

	ac = netdev_priv(ndev);
	ac->gdma_dev = gd;
	ac->ndev = ndev;
	ac->max_queues = gc->max_num_queue;
	ac->num_queues = min_t(uint, gc->max_num_queue, ANA_DEFAULT_NUM_QUEUE);
	ac->default_vport = INVALID_ANA_HANDLE;

	ndev->netdev_ops = &ana_devops;
	ndev->ethtool_ops = &ana_ethtool_ops;
	ndev->mtu = ETH_DATA_LEN;
	ndev->max_mtu = ndev->mtu;
	ndev->min_mtu = ndev->mtu;
	ndev->needed_headroom = ANA_HEADROOM;
	SET_NETDEV_DEV(ndev, gc->dev);

	netif_carrier_off(ndev);
	err = ana_do_attach(ndev, true);
	if (err)
		goto free_net;

	rtnl_lock();

	netdev_lockdep_set_classes(ndev);

	ndev->hw_features = NETIF_F_SG | NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM;
	ndev->hw_features |= NETIF_F_RXCSUM;
	ndev->hw_features |= NETIF_F_TSO | NETIF_F_TSO6;
	ndev->hw_features |= NETIF_F_RXHASH;
	ndev->features = ndev->hw_features;
	ndev->vlan_features = 0;

	err = register_netdevice(ndev);
	if (err) {
		netdev_err(ndev, "Unable to register netdev.\n");
		goto destroy_vport;
	}

	rtnl_unlock();

	return 0;
destroy_vport:
	rtnl_unlock();

	ana_destroy_vport(ac);
	ana_destroy_eq(gc, ac);
free_net:
	gd->driver_data = NULL;
	netdev_err(ndev, "Failed to probe net device:  %d\n", err);
	free_netdev(ndev);
	return err;
}

void ana_remove(struct gdma_dev *gd)
{
	struct gdma_context *gc = ana_to_gdma_context(gd);
	struct net_device *ndev = gd->driver_data;
	struct device *dev = gc->dev;

	if (!ndev) {
		dev_err(dev, "Failed to find a net device to remove\n");
		return;
	}

	/* All cleanup actions should stay after rtnl_lock(), otherwise
	 * other functions may access partially cleaned up data.
	 */
	rtnl_lock();

	ana_detach(ndev);

	unregister_netdevice(ndev);

	rtnl_unlock();

	free_netdev(ndev);

	gd->driver_data = NULL;
}
