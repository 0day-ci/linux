// SPDX-License-Identifier: GPL-2.0
/*
 * <:copyright-gpl
 * Copyright 2008 Broadcom Corp. All Rights Reserved.
 * Copyright (C) 2021 Allied Telesis Labs NZ
 *
 * This program is free software; you can distribute it and/or modify it
 * under the terms of the GNU General Public License (Version 2) as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.
 * :>
 */
/******************************************************************************
 * Filename:       nf_conntrack_proto_esp.c
 * Author:         Pavan Kumar
 * Creation Date:  05/27/04
 *
 * Description:
 * Implements the ESP ALG connectiontracking.
 * Migrated to kernel 2.6.21.5 on April 16, 2008 by Dan-Han Tsai.
 * Migrated to kernel 5.11.0-rc2+ on March 3, 2021 by Allied Telesis Labs NZ (Cole Dishington).
 *
 * Updates to ESP conntracking on October,2010,by Manamohan,Lantiq Deutschland GmbH:
 *	- Added the support for sessions with two or more different remote servers
 *    from single or multiple lan clients with same lan and remote SPI Ids
 *	- Support for associating the multiple LAN side sessions waiting
 *    for the reply from same remote server with the one which is created first
 * Updates to ESP conntracking on August,2015,by Allied Telesis Labs NZ:
 *	- Improve ESP entry lookup performance by adding hashtable. (Anthony Lineham)
 *	- Add locking around ESP connection table. (Anthony Lineham)
 *	- Fixups including adding destroy function, endian-safe SPIs and IPs,
 *	  replace prinks with DEBUGs. (Anthony Lineham)
 *	- Extend ESP connection tracking to allow conntrack ESP entry matching
 *	  of tuple values. (Matt Bennett)
 ****************************************************************************/

#include <linux/module.h>
#include <linux/types.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/seq_file.h>
#include <linux/in.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <net/dst.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_l4proto.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_timeout.h>
#include <linux/netfilter/nf_conntrack_proto_esp.h>

#include "nf_internals.h"

#if 0
#define ESP_DEBUG 1
#define DEBUGP(format, args...) printk(KERN_DEBUG "%s: " format, __func__, ## args)
#else
#undef ESP_DEBUG
#define DEBUGP(x, args...)
#endif

#define TEMP_SPI_START 1500
#define TEMP_SPI_MAX   (TEMP_SPI_START + ESP_MAX_PORTS - 1)

struct _esp_table {
	/* Hash table nodes for each required lookup
	 * lnode: l_spi, l_ip, r_ip
	 * rnode: r_spi, r_ip
	 * incmpl_rnode: r_ip
	 */
	struct hlist_node lnode;
	struct hlist_node rnode;
	struct hlist_node incmpl_rnode;

	u32 l_spi;
	u32 r_spi;
	u32 l_ip;
	u32 r_ip;
	u16 tspi;
	unsigned long allocation_time;
	struct net *net;
};

static unsigned int esp_timeouts[ESP_CT_MAX] = {
	[ESP_CT_UNREPLIED] = 60 * HZ,
	[ESP_CT_REPLIED] = 3600 * HZ,
};

static inline struct nf_esp_net *esp_pernet(struct net *net)
{
	return &net->ct.nf_ct_proto.esp;
}

static void esp_init_esp_tables(struct nf_esp_net *net_esp)
{
	struct _esp_table **esp_table;
	int i;

	rwlock_init(&net_esp->esp_table_lock);

	write_lock_bh(&net_esp->esp_table_lock);
	esp_table = net_esp->esp_table;
	for (i = 0; i < ESP_MAX_PORTS; i++)
		memset(&esp_table[i], 0, sizeof(struct _esp_table *));

	for (i = 0; i < HASH_TAB_SIZE; i++) {
		INIT_HLIST_HEAD(&net_esp->ltable[i]);
		INIT_HLIST_HEAD(&net_esp->rtable[i]);
		INIT_HLIST_HEAD(&net_esp->incmpl_rtable[i]);
	}
	DEBUGP("Initialized %i ESP table entries\n", i);
	write_unlock_bh(&net_esp->esp_table_lock);
}

void nf_conntrack_esp_init_net(struct net *net)
{
	struct nf_esp_net *net_esp = esp_pernet(net);
	int i;

	esp_init_esp_tables(net_esp);
	for (i = 0; i < ESP_CT_MAX; i++)
		net_esp->esp_timeouts[i] = esp_timeouts[i];
}

/* Free an entry referred to by TSPI.
 * Entry table locking and unlocking is the responsibility of the calling function.
 * Range checking is the responsibility of the calling function.
 */
static void esp_table_free_entry_by_tspi(struct net *net, u16 tspi)
{
	struct nf_esp_net *esp_net = esp_pernet(net);
	struct _esp_table *esp_entry = NULL;

	esp_entry = esp_net->esp_table[tspi - TEMP_SPI_START];
	if (esp_entry) {
		/* Remove from all the hash tables. Hlist utility can handle items
		 * that aren't actually in the list, so just try removing from
		 * each list
		 */
		DEBUGP("Removing entry %x (%p) from all tables",
		       esp_entry->tspi, esp_entry);
		hlist_del_init(&esp_entry->lnode);
		hlist_del_init(&esp_entry->incmpl_rnode);
		hlist_del_init(&esp_entry->rnode);
		kfree(esp_entry);
		esp_net->esp_table[tspi - TEMP_SPI_START] = NULL;
	}
}

/* Allocate a free IPSEC table entry.
 * NOTE: The ESP entry table must be locked prior to calling this function.
 */
struct _esp_table *alloc_esp_entry(struct net *net)
{
	struct nf_esp_net *net_esp = esp_pernet(net);
	struct _esp_table **esp_table = net_esp->esp_table;
	struct _esp_table *esp_entry = NULL;
	int idx = 0;

	/* Find the first unused slot */
	for (; idx < ESP_MAX_PORTS; idx++) {
		if (esp_table[idx])
			continue;

		esp_table[idx] = kmalloc(sizeof(*esp_entry), GFP_ATOMIC);
		memset(esp_table[idx], 0, sizeof(struct _esp_table));
		esp_table[idx]->tspi = idx + TEMP_SPI_START;

		DEBUGP("   New esp_entry (%p) at idx %d tspi %u\n",
		       esp_table[idx], idx, esp_table[idx]->tspi);

		esp_table[idx]->allocation_time = jiffies;
		esp_table[idx]->net = net;
		esp_entry = esp_table[idx];
		break;
	}
	return esp_entry;
}

static u32 calculate_hash(const u32 spi, const u32 src_ip,
			  const u32 dst_ip)
{
	u32 hash;

	/* Simple combination */
	hash = spi + src_ip + dst_ip;
	/* Reduce to an index to fit the table size */
	hash %= HASH_TAB_SIZE;

	DEBUGP("Generated hash %x from spi %x srcIP %x dstIP %x\n", hash, spi,
	       src_ip, dst_ip);
	return hash;
}

/*	Search for an ESP entry in the initial state based the IP address of the
 *	remote peer.
 *	NOTE: The ESP entry table must be locked prior to calling this function.
 */
static struct _esp_table *search_esp_entry_init_remote(struct nf_esp_net *net_esp,
						       const u32 src_ip)
{
	struct _esp_table **esp_table = net_esp->esp_table;
	struct _esp_table *esp_entry = NULL;
	u32 hash = 0;
	int first_entry = -1;

	hash = calculate_hash(0, src_ip, 0);
	hlist_for_each_entry(esp_entry, &net_esp->incmpl_rtable[hash],
			     incmpl_rnode) {
		DEBUGP("Checking against incmpl_rtable entry %x (%p) with l_spi %x r_spi %x r_ip %x\n",
		       esp_entry->tspi, esp_entry, esp_entry->l_spi,
		       esp_entry->r_spi, esp_entry->r_ip);
		if (src_ip == esp_entry->r_ip && esp_entry->l_spi != 0 &&
		    esp_entry->r_spi == 0) {
			DEBUGP("Matches entry %x", esp_entry->tspi);
			if (first_entry == -1) {
				DEBUGP("First match\n");
				first_entry = esp_entry->tspi - TEMP_SPI_START;
			} else if (esp_table[first_entry]->allocation_time >
				   esp_entry->allocation_time) {
				/* This entry is older than the last one found so treat this
				 * as a better match.
				 */
				DEBUGP("Older/better match\n");
				first_entry = esp_entry->tspi - TEMP_SPI_START;
			}
		}
	}

	if (first_entry != -1) {
		DEBUGP("returning esp entry\n");
		esp_entry = esp_table[first_entry];
		return esp_entry;
	}

	DEBUGP("No init entry found\n");
	return NULL;
}

/*	Search for an ESP entry by SPI and source and destination IP addresses.
 *	NOTE: The ESP entry table must be locked prior to calling this function.
 */
struct _esp_table *search_esp_entry_by_spi(struct net *net, const __u32 spi,
					   const __u32 src_ip, const __u32 dst_ip)
{
	struct nf_esp_net *net_esp = esp_pernet(net);
	struct _esp_table *esp_entry = NULL;
	u32 hash = 0;

	/* Check for matching established session or repeated initial LAN side */
	/* LAN side first */
	hash = calculate_hash(spi, src_ip, dst_ip);
	hlist_for_each_entry(esp_entry, &net_esp->ltable[hash], lnode) {
		DEBUGP
		    ("Checking against ltable entry %x (%p) with l_spi %x l_ip %x r_ip %x\n",
		     esp_entry->tspi, esp_entry, esp_entry->l_spi,
		     esp_entry->l_ip, esp_entry->r_ip);
		if (spi == esp_entry->l_spi && src_ip == esp_entry->l_ip &&
		    dst_ip == esp_entry->r_ip) {
			/* When r_spi is set this is an established session. When not set it's
			 * a repeated initial packet from LAN side. But both cases are treated
			 * the same.
			 */
			DEBUGP("Matches entry %x", esp_entry->tspi);
			return esp_entry;
		}
	}

	/* Established remote side */
	hash = calculate_hash(spi, src_ip, 0);
	hlist_for_each_entry(esp_entry, &net_esp->rtable[hash], rnode) {
		DEBUGP
		    ("Checking against rtable entry %x (%p) with l_spi %x r_spi %x r_ip %x\n",
		     esp_entry->tspi, esp_entry, esp_entry->l_spi,
		     esp_entry->r_spi, esp_entry->r_ip);
		if (spi == esp_entry->r_spi && src_ip == esp_entry->r_ip &&
		    esp_entry->l_spi != 0) {
			DEBUGP("Matches entry %x", esp_entry->tspi);
			return esp_entry;
		}
	}

	/* Incomplete remote side */
	esp_entry = search_esp_entry_init_remote(net_esp, src_ip);
	if (esp_entry) {
		esp_entry->r_spi = spi;
		/* Remove entry from incmpl_rtable and add to rtable */
		DEBUGP("Completing entry %x with remote SPI info",
		       esp_entry->tspi);
		hlist_del_init(&esp_entry->incmpl_rnode);
		hash = calculate_hash(spi, src_ip, 0);
		hlist_add_head(&esp_entry->rnode, &net_esp->rtable[hash]);
		return esp_entry;
	}

	DEBUGP("No Entry\n");
	return NULL;
}

/* invert esp part of tuple */
bool nf_conntrack_invert_esp_tuple(struct nf_conntrack_tuple *tuple,
				   const struct nf_conntrack_tuple *orig)
{
	tuple->dst.u.esp.spi = orig->dst.u.esp.spi;
	tuple->src.u.esp.spi = orig->src.u.esp.spi;
	return true;
}

/* esp hdr info to tuple */
bool esp_pkt_to_tuple(const struct sk_buff *skb, unsigned int dataoff,
		      struct net *net, struct nf_conntrack_tuple *tuple)
{
	struct nf_esp_net *net_esp = esp_pernet(net);
	struct esphdr _esphdr, *esphdr;
	struct _esp_table *esp_entry = NULL;
	u32 spi = 0;

	esphdr = skb_header_pointer(skb, dataoff, sizeof(_esphdr), &_esphdr);
	if (!esphdr) {
		/* try to behave like "nf_conntrack_proto_generic" */
		tuple->src.u.all = 0;
		tuple->dst.u.all = 0;
		return true;
	}
	spi = ntohl(esphdr->spi);

	DEBUGP("Enter pkt_to_tuple() with spi %x\n", spi);
	/* check if esphdr has a new SPI:
	 *   if no, update tuple with correct tspi;
	 *   if yes, check if we have seen the source IP:
	 *             if yes, update the ESP tables update the tuple with correct tspi
	 *             if no, create a new entry
	 */
	write_lock_bh(&net_esp->esp_table_lock);
	esp_entry = search_esp_entry_by_spi(net, spi, tuple->src.u3.ip,
					    tuple->dst.u3.ip);
	if (!esp_entry) {
		u32 hash = 0;

		esp_entry = alloc_esp_entry(net);
		if (!esp_entry) {
			DEBUGP("All entries in use\n");
			write_unlock_bh(&net_esp->esp_table_lock);
			return false;
		}
		esp_entry->l_spi = spi;
		esp_entry->l_ip = tuple->src.u3.ip;
		esp_entry->r_ip = tuple->dst.u3.ip;
		/* Add entries to the hash tables */
		hash = calculate_hash(spi, esp_entry->l_ip, esp_entry->r_ip);
		hlist_add_head(&esp_entry->lnode, &net_esp->ltable[hash]);
		hash = calculate_hash(0, 0, esp_entry->r_ip);
		hlist_add_head(&esp_entry->incmpl_rnode,
			       &net_esp->incmpl_rtable[hash]);
	}

	DEBUGP
	    ("entry_info: tspi %u l_spi 0x%x r_spi 0x%x l_ip %x r_ip %x srcIP %x dstIP %x\n",
	     esp_entry->tspi, esp_entry->l_spi, esp_entry->r_spi,
	     esp_entry->l_ip, esp_entry->r_ip, tuple->src.u3.ip,
	     tuple->dst.u3.ip);

	tuple->dst.u.esp.spi = esp_entry->tspi;
	tuple->src.u.esp.spi = esp_entry->tspi;
	write_unlock_bh(&net_esp->esp_table_lock);
	return true;
}

#ifdef CONFIG_NF_CONNTRACK_PROCFS
/* print private data for conntrack */
static void esp_print_conntrack(struct seq_file *s, struct nf_conn *ct)
{
	seq_printf(s, "timeout=%u, stream_timeout=%u ",
		   (ct->proto.esp.timeout / HZ),
		   (ct->proto.esp.stream_timeout / HZ));
}
#endif

/* Returns verdict for packet, and may modify conntrack */
int nf_conntrack_esp_packet(struct nf_conn *ct, struct sk_buff *skb,
			    unsigned int dataoff,
			    enum ip_conntrack_info ctinfo,
			    const struct nf_hook_state *state)
{
	unsigned int *timeouts = nf_ct_timeout_lookup(ct);
#ifdef ESP_DEBUG
	const struct iphdr *iph;
	struct esphdr _esphdr, *esphdr;

	iph = ip_hdr(skb);
	esphdr = skb_header_pointer(skb, dataoff, sizeof(_esphdr), &_esphdr);
	if (iph && esphdr) {
		u32 spi;

		spi = ntohl(esphdr->spi);
		DEBUGP("(0x%x) %x <-> %x status %s info %d %s\n",
		       spi, iph->saddr, iph->daddr,
		       (ct->status & IPS_SEEN_REPLY) ? "SEEN" : "NOT_SEEN",
		       ctinfo, (ctinfo == IP_CT_NEW) ? "CT_NEW" : "SEEN_REPLY");
	}
#endif /* ESP_DEBUG */

	if (!timeouts)
		timeouts = esp_pernet(nf_ct_net(ct))->esp_timeouts;

	if (!nf_ct_is_confirmed(ct)) {
		ct->proto.esp.stream_timeout = timeouts[ESP_CT_REPLIED];
		ct->proto.esp.timeout = timeouts[ESP_CT_UNREPLIED];
	}

	/* If we've seen traffic both ways, this is some kind of ESP
	 * stream.  Extend timeout.
	 */
	if (test_bit(IPS_SEEN_REPLY_BIT, &ct->status)) {
		nf_ct_refresh_acct(ct, ctinfo, skb, timeouts[ESP_CT_REPLIED]);
		/* Also, more likely to be important, and not a probe */
		if (!test_and_set_bit(IPS_ASSURED_BIT, &ct->status))
			/* Was originally IPCT_STATUS but this is no longer an option.
			 * GRE uses assured for same purpose
			 */
			nf_conntrack_event_cache(IPCT_ASSURED, ct);
	} else {
		nf_ct_refresh_acct(ct, ctinfo, skb, timeouts[ESP_CT_UNREPLIED]);
	}

	return NF_ACCEPT;
}

/* Called when a conntrack entry has already been removed from the hashes
 * and is about to be deleted from memory
 */
void destroy_esp_conntrack_entry(struct nf_conn *ct)
{
	struct nf_conntrack_tuple *tuple = NULL;
	enum ip_conntrack_dir dir;
	u16 tspi = 0;
	struct net *net = nf_ct_net(ct);
	struct nf_esp_net *net_esp = esp_pernet(net);

	write_lock_bh(&net_esp->esp_table_lock);

	/* Probably all the ESP entries referenced in this connection are the same,
	 * but the free function handles repeated frees, so best to do them all.
	 */
	for (dir = IP_CT_DIR_ORIGINAL; dir < IP_CT_DIR_MAX; dir++) {
		tuple = nf_ct_tuple(ct, dir);

		tspi = tuple->src.u.esp.spi;
		if (tspi >= TEMP_SPI_START && tspi <= TEMP_SPI_MAX) {
			DEBUGP("Deleting src tspi %x (dir %i)\n", tspi, dir);
			esp_table_free_entry_by_tspi(net, tspi);
		}
		tuple->src.u.esp.spi = 0;
		tspi = tuple->dst.u.esp.spi;
		if (tspi >= TEMP_SPI_START && tspi <= TEMP_SPI_MAX) {
			DEBUGP("Deleting dst tspi %x (dir %i)\n", tspi, dir);
			esp_table_free_entry_by_tspi(net, tspi);
		}
		tuple->dst.u.esp.spi = 0;
	}

	write_unlock_bh(&net_esp->esp_table_lock);
}

#if IS_ENABLED(CONFIG_NF_CT_NETLINK)

#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_conntrack.h>

static int esp_tuple_to_nlattr(struct sk_buff *skb,
			       const struct nf_conntrack_tuple *t)
{
	if (nla_put_be16(skb, CTA_PROTO_SRC_ESP_SPI, t->src.u.esp.spi) ||
	    nla_put_be16(skb, CTA_PROTO_DST_ESP_SPI, t->dst.u.esp.spi))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -1;
}

static const struct nla_policy esp_nla_policy[CTA_PROTO_MAX + 1] = {
	[CTA_PROTO_SRC_ESP_SPI] = { .type = NLA_U16 },
	[CTA_PROTO_DST_ESP_SPI] = { .type = NLA_U16 },
};

static int esp_nlattr_to_tuple(struct nlattr *tb[],
			       struct nf_conntrack_tuple *t,
				   u32 flags)
{
	if (flags & CTA_FILTER_FLAG(CTA_PROTO_SRC_ESP_SPI)) {
		if (!tb[CTA_PROTO_SRC_ESP_SPI])
			return -EINVAL;

		t->src.u.esp.spi = nla_get_be16(tb[CTA_PROTO_SRC_ESP_SPI]);
	}

	if (flags & CTA_FILTER_FLAG(CTA_PROTO_DST_ESP_SPI)) {
		if (!tb[CTA_PROTO_DST_ESP_SPI])
			return -EINVAL;

		t->dst.u.esp.spi = nla_get_be16(tb[CTA_PROTO_DST_ESP_SPI]);
	}

	return 0;
}

static unsigned int esp_nlattr_tuple_size(void)
{
	return nla_policy_len(esp_nla_policy, CTA_PROTO_MAX + 1);
}
#endif

/* protocol helper struct */
const struct nf_conntrack_l4proto nf_conntrack_l4proto_esp = {
	.l4proto = IPPROTO_ESP,
#ifdef CONFIG_NF_CONNTRACK_PROCFS
	.print_conntrack = esp_print_conntrack,
#endif
#if IS_ENABLED(CONFIG_NF_CT_NETLINK)
	.tuple_to_nlattr = esp_tuple_to_nlattr,
	.nlattr_tuple_size = esp_nlattr_tuple_size,
	.nlattr_to_tuple = esp_nlattr_to_tuple,
	.nla_policy = esp_nla_policy,
#endif
};
