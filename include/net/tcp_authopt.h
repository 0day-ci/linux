/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _LINUX_TCP_AUTHOPT_H
#define _LINUX_TCP_AUTHOPT_H

#include <uapi/linux/tcp.h>

struct tcp_authopt_alg_imp;

/**
 * struct tcp_authopt_key_info - Representation of a Master Key Tuple as per RFC5925
 *
 * Key structure lifetime is only protected by RCU so readers needs to hold a
 * single rcu_read_lock until they're done with the key.
 */
struct tcp_authopt_key_info {
	struct hlist_node node;
	struct rcu_head rcu;
	/* Local identifier */
	u32 local_id;
	u32 flags;
	/* Wire identifiers */
	u8 send_id, recv_id;
	u8 alg_id;
	u8 keylen;
	u8 key[TCP_AUTHOPT_MAXKEYLEN];
	u8 maclen;
	struct sockaddr_storage addr;
	struct tcp_authopt_alg_imp *alg;
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
	struct rcu_head rcu;
	/**
	 * @send_keyid - Current key used for sending, cached.
	 *
	 * Once a key is found it only changes by user or remote request.
	 */
	struct tcp_authopt_key_info *send_key;
	u32 flags;
	u8 send_keyid;
	u8 send_rnextkeyid;
	u8 recv_keyid;
	u8 recv_rnextkeyid;
	u32 src_isn;
	u32 dst_isn;
};

#ifdef CONFIG_TCP_AUTHOPT
struct tcp_authopt_key_info *tcp_authopt_select_key(const struct sock *sk,
						    const struct sock *addr_sk,
						    u8 *rnextkeyid);
void tcp_authopt_clear(struct sock *sk);
int tcp_set_authopt(struct sock *sk, sockptr_t optval, unsigned int optlen);
int tcp_get_authopt_val(struct sock *sk, struct tcp_authopt *key);
int tcp_set_authopt_key(struct sock *sk, sockptr_t optval, unsigned int optlen);
int tcp_authopt_hash(
		char *hash_location,
		struct tcp_authopt_key_info *key,
		struct sock *sk, struct sk_buff *skb);
int __tcp_authopt_openreq(struct sock *newsk, const struct sock *oldsk, struct request_sock *req);
static inline int tcp_authopt_openreq(
		struct sock *newsk,
		const struct sock *oldsk,
		struct request_sock *req)
{
	if (!rcu_dereference(tcp_sk(oldsk)->authopt_info))
		return 0;
	else
		return __tcp_authopt_openreq(newsk, oldsk, req);
}
int __tcp_authopt_inbound_check(
		struct sock *sk,
		struct sk_buff *skb,
		struct tcp_authopt_info *info);
static inline int tcp_authopt_inbound_check(struct sock *sk, struct sk_buff *skb)
{
	struct tcp_authopt_info *info = rcu_dereference(tcp_sk(sk)->authopt_info);

	if (info)
		return __tcp_authopt_inbound_check(sk, skb, info);
	else
		return 0;
}
#else
static inline struct tcp_authopt_key_info *tcp_authopt_select_key(
		const struct sock *sk,
		const struct sock *addr_sk,
		u8 *rnextkeyid)
{
	return NULL;
}
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
static inline int tcp_authopt_hash(
		char *hash_location,
		struct tcp_authopt_key_info *key,
		struct sock *sk, struct sk_buff *skb)
{
	return -EINVAL;
}
static inline int tcp_authopt_openreq(struct sock *newsk,
				      const struct sock *oldsk,
				      struct request_sock *req)
{
	return 0;
}
static inline int tcp_authopt_inbound_check(struct sock *sk, struct sk_buff *skb)
{
	return 0;
}
#endif

#endif /* _LINUX_TCP_AUTHOPT_H */
