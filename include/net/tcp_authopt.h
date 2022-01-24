/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _LINUX_TCP_AUTHOPT_H
#define _LINUX_TCP_AUTHOPT_H

#include <uapi/linux/tcp.h>
#include <net/netns/tcp_authopt.h>
#include <linux/tcp.h>

/* According to RFC5925 the length of the authentication option varies based on
 * the signature algorithm. Linux only implements the algorithms defined in
 * RFC5926 which have a constant length of 16.
 *
 * This is used in stack allocation of tcp option buffers for output. It is
 * shorter than the length of the MD5 option.
 *
 * Input packets can have authentication options of different lengths but they
 * will always be flagged as invalid (since no such algorithms are supported).
 */
#define TCPOLEN_AUTHOPT_OUTPUT	16

struct tcp_authopt_alg_imp;

/**
 * struct tcp_authopt_key_info - Representation of a Master Key Tuple as per RFC5925
 *
 * Key structure lifetime is protected by RCU so send/recv code needs to hold a
 * single rcu_read_lock until they're done with the key.
 *
 * Global keys can be cached in sockets, this requires increasing kref.
 */
struct tcp_authopt_key_info {
	/** @node: node in &netns_tcp_authopt.head list */
	struct hlist_node node;
	/** @rcu: for kfree_rcu */
	struct rcu_head rcu;
	/** @ref: for kref_put */
	struct kref ref;
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
	/** @alg: Algorithm implementation matching alg_id */
	struct tcp_authopt_alg_imp *alg;
};

/**
 * struct tcp_authopt_info - Per-socket information regarding tcp_authopt
 *
 * This is lazy-initialized in order to avoid increasing memory usage for
 * regular TCP sockets. Once created it is only destroyed on socket close.
 */
struct tcp_authopt_info {
	/** @rcu: for kfree_rcu */
	struct rcu_head rcu;
	/** @flags: Combination of &enum tcp_authopt_key_flag */
	u32 flags;
	/** @src_isn: Local Initial Sequence Number */
	u32 src_isn;
	/** @dst_isn: Remote Initial Sequence Number */
	u32 dst_isn;
	/** @rcv_sne: Recv-side Sequence Number Extension tracking tcp_sock.rcv_nxt */
	u32 rcv_sne;
	/** @snd_sne: Send-side Sequence Number Extension tracking tcp_sock.snd_nxt */
	u32 snd_sne;
};

/* TCP authopt as found in header */
struct tcphdr_authopt {
	u8 num;
	u8 len;
	u8 keyid;
	u8 rnextkeyid;
	u8 mac[0];
};

#ifdef CONFIG_TCP_AUTHOPT
DECLARE_STATIC_KEY_FALSE(tcp_authopt_needed_key);
#define tcp_authopt_needed (static_branch_unlikely(&tcp_authopt_needed_key))
extern int sysctl_tcp_authopt;

void tcp_authopt_free(struct sock *sk, struct tcp_authopt_info *info);
void tcp_authopt_clear(struct sock *sk);
int tcp_set_authopt(struct sock *sk, sockptr_t optval, unsigned int optlen);
int tcp_get_authopt_val(struct sock *sk, struct tcp_authopt *key);
int tcp_set_authopt_key(struct sock *sk, sockptr_t optval, unsigned int optlen);
struct tcp_authopt_key_info *__tcp_authopt_select_key(
		const struct sock *sk,
		struct tcp_authopt_info *info,
		const struct sock *addr_sk,
		u8 *rnextkeyid);
static inline struct tcp_authopt_key_info *tcp_authopt_select_key(
		const struct sock *sk,
		const struct sock *addr_sk,
		struct tcp_authopt_info **info,
		u8 *rnextkeyid)
{
	if (tcp_authopt_needed) {
		*info = rcu_dereference(tcp_sk(sk)->authopt_info);

		if (*info)
			return __tcp_authopt_select_key(sk, *info, addr_sk, rnextkeyid);
	}
	return NULL;
}
int tcp_authopt_hash(
		char *hash_location,
		struct tcp_authopt_key_info *key,
		struct tcp_authopt_info *info,
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
void __tcp_authopt_finish_connect(struct sock *sk, struct sk_buff *skb,
				  struct tcp_authopt_info *info);
static inline void tcp_authopt_finish_connect(struct sock *sk, struct sk_buff *skb)
{
	struct tcp_authopt_info *info;

	if (tcp_authopt_needed) {
		info = rcu_dereference_protected(tcp_sk(sk)->authopt_info,
						 lockdep_sock_is_held(sk));

		if (info)
			__tcp_authopt_finish_connect(sk, skb, info);
	}
}
static inline void tcp_authopt_time_wait(
		struct tcp_timewait_sock *tcptw,
		struct tcp_sock *tp)
{
	if (tcp_authopt_needed) {
		/* Transfer ownership of authopt_info to the twsk
		 * This requires no other users of the origin sock.
		 */
		tcptw->tw_authopt_info = rcu_dereference_protected(
				tp->authopt_info,
				lockdep_sock_is_held((struct sock *)tp));
		rcu_assign_pointer(tp->authopt_info, NULL);
	} else {
		tcptw->tw_authopt_info = NULL;
	}
}
/** tcp_authopt_inbound_check - check for valid TCP-AO signature.
 *
 * Return negative ERRNO on error, 0 if not present and 1 if present and valid.
 *
 * If the AO signature is present and valid then caller skips MD5 check.
 */
int __tcp_authopt_inbound_check(
		struct sock *sk,
		struct sk_buff *skb,
		struct tcp_authopt_info *info,
		const u8 *opt);
static inline int tcp_authopt_inbound_check(struct sock *sk, struct sk_buff *skb, const u8 *opt)
{
	if (tcp_authopt_needed) {
		struct tcp_authopt_info *info = rcu_dereference(tcp_sk(sk)->authopt_info);

		if (info)
			return __tcp_authopt_inbound_check(sk, skb, info, opt);
	}
	return 0;
}
static inline int tcp_authopt_inbound_check_req(struct request_sock *req, struct sk_buff *skb,
						const u8 *opt)
{
	if (tcp_authopt_needed) {
		struct sock *lsk = req->rsk_listener;
		struct tcp_authopt_info *info = rcu_dereference(tcp_sk(lsk)->authopt_info);

		if (info)
			return __tcp_authopt_inbound_check((struct sock *)req, skb, info, opt);
	}
	return 0;
}
void __tcp_authopt_update_rcv_sne(struct tcp_sock *tp, struct tcp_authopt_info *info, u32 seq);
static inline void tcp_authopt_update_rcv_sne(struct tcp_sock *tp, u32 seq)
{
	struct tcp_authopt_info *info;

	if (tcp_authopt_needed) {
		info = rcu_dereference_protected(tp->authopt_info,
						 lockdep_sock_is_held((struct sock *)tp));
		if (info)
			__tcp_authopt_update_rcv_sne(tp, info, seq);
	}
}
void __tcp_authopt_update_snd_sne(struct tcp_sock *tp, struct tcp_authopt_info *info, u32 seq);
static inline void tcp_authopt_update_snd_sne(struct tcp_sock *tp, u32 seq)
{
	struct tcp_authopt_info *info;

	if (tcp_authopt_needed) {
		info = rcu_dereference_protected(tp->authopt_info,
						 lockdep_sock_is_held((struct sock *)tp));
		if (info)
			__tcp_authopt_update_snd_sne(tp, info, seq);
	}
}
#else
static inline int tcp_set_authopt(struct sock *sk, sockptr_t optval, unsigned int optlen)
{
	return -ENOPROTOOPT;
}
static inline int tcp_get_authopt_val(struct sock *sk, struct tcp_authopt *key)
{
	return -ENOPROTOOPT;
}
static inline void tcp_authopt_free(struct sock *sk, struct tcp_authopt_info *info)
{
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
		struct tcp_authopt_key *info,
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
static inline void tcp_authopt_finish_connect(struct sock *sk, struct sk_buff *skb)
{
}
static inline void tcp_authopt_time_wait(
		struct tcp_timewait_sock *tcptw,
		struct tcp_sock *tp)
{
}
static inline int tcp_authopt_inbound_check(struct sock *sk, struct sk_buff *skb, const u8 *opt)
{
	return 0;
}
static inline int tcp_authopt_inbound_check_req(struct request_sock *sk, struct sk_buff *skb,
						const u8 *opt)
{
	return 0;
}
static inline void tcp_authopt_update_rcv_sne(struct tcp_sock *tp, u32 seq)
{
}
static inline void tcp_authopt_update_snd_sne(struct tcp_sock *tp, u32 seq)
{
}
#endif

#endif /* _LINUX_TCP_AUTHOPT_H */
