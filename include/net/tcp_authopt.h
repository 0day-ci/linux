/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _LINUX_TCP_AUTHOPT_H
#define _LINUX_TCP_AUTHOPT_H

#include <uapi/linux/tcp.h>

struct tcp_authopt_alg_imp;

/* Representation of a Master Key Tuple as per RFC5925 */
struct tcp_authopt_key_info {
	struct hlist_node node;
	/* Local identifier */
	u32 local_id;
	u32 flags;
	/* Wire identifiers */
	u8 send_id, recv_id;
	u8 alg_id;
	u8 keylen;
	u8 key[TCP_AUTHOPT_MAXKEYLEN];
	u8 maclen;
	struct rcu_head rcu;
	struct sockaddr_storage addr;
	struct tcp_authopt_alg_imp *alg;
};

/* Per-socket information regarding tcp_authopt */
struct tcp_authopt_info {
	/* List of tcp_authopt_key_info */
	struct hlist_head head;
	/* Current send_key, cached.
	 * Once a key is found it only changes by user or remote request.
	 */
	struct tcp_authopt_key_info *send_key;
	u32 flags;
	u32 local_send_id;
	u8 send_rnextkeyid;
	u8 recv_keyid;
	u8 recv_rnextkeyid;
	u32 src_isn;
	u32 dst_isn;
	struct rcu_head rcu;
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
static struct tcp_authopt_key_info *tcp_authopt_select_key(const struct sock *sk,
							   const struct sock *addr_sk,
							   u8 *rnextkeyid);
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
