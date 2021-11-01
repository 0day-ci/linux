/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _LINUX_TCP_AUTHOPT_H
#define _LINUX_TCP_AUTHOPT_H

#include <uapi/linux/tcp.h>

/**
 * struct tcp_authopt_key_info - Representation of a Master Key Tuple as per RFC5925
 *
 * Key structure lifetime is only protected by RCU so readers needs to hold a
 * single rcu_read_lock until they're done with the key.
 */
struct tcp_authopt_key_info {
	/** @node: node in &tcp_authopt_info.head list */
	struct hlist_node node;
	/** @rcu: for kfree_rcu */
	struct rcu_head rcu;
	/** @flags: Combination of &enum tcp_authopt_key_flag */
	u32 flags;
	/** @send_id: Same as &tcp_authopt_key.send_id */
	u8 send_id;
	/** @recv_id: Same as &tcp_authopt_key.recv_id */
	u8 recv_id;
	/** @alg_id: Same as &tcp_authopt_key.alg */
	u8 alg_id;
	/** @keylen: Same as &tcp_authopt_key.keylen */
	u8 keylen;
	/** @key: Same as &tcp_authopt_key.key */
	u8 key[TCP_AUTHOPT_MAXKEYLEN];
	/** @addr: Same as &tcp_authopt_key.addr */
	struct sockaddr_storage addr;
};

/**
 * struct tcp_authopt_info - Per-socket information regarding tcp_authopt
 *
 * This is lazy-initialized in order to avoid increasing memory usage for
 * regular TCP sockets. Once created it is only destroyed on socket close.
 */
struct tcp_authopt_info {
	/** @head: List of tcp_authopt_key_info */
	struct hlist_head head;
	/** @rcu: for kfree_rcu */
	struct rcu_head rcu;
	/** @flags: Combination of &enum tcp_authopt_key_flag */
	u32 flags;
	/** @src_isn: Local Initial Sequence Number */
	u32 src_isn;
	/** @dst_isn: Remote Initial Sequence Number */
	u32 dst_isn;
};

#ifdef CONFIG_TCP_AUTHOPT
void tcp_authopt_clear(struct sock *sk);
int tcp_set_authopt(struct sock *sk, sockptr_t optval, unsigned int optlen);
int tcp_get_authopt_val(struct sock *sk, struct tcp_authopt *key);
int tcp_set_authopt_key(struct sock *sk, sockptr_t optval, unsigned int optlen);
#else
static inline int tcp_set_authopt(struct sock *sk, sockptr_t optval, unsigned int optlen)
{
	return -ENOPROTOOPT;
}
static inline int tcp_get_authopt_val(struct sock *sk, struct tcp_authopt *key)
{
	return -ENOPROTOOPT;
}
static inline void tcp_authopt_clear(struct sock *sk)
{
}
static inline int tcp_set_authopt_key(struct sock *sk, sockptr_t optval, unsigned int optlen)
{
	return -ENOPROTOOPT;
}
#endif

#endif /* _LINUX_TCP_AUTHOPT_H */
