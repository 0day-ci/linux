// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/kernel.h>
#include <net/tcp.h>
#include <net/tcp_authopt.h>
#include <crypto/hash.h>

/* This is mainly intended to protect against local privilege escalations through
 * a rarely used feature so it is deliberately not namespaced.
 */
int sysctl_tcp_authopt;

/* This is enabled when first struct tcp_authopt_info is allocated and never released */
DEFINE_STATIC_KEY_FALSE(tcp_authopt_needed);
/* only for CONFIG_IPV6=m */
EXPORT_SYMBOL(tcp_authopt_needed);

/* All current algorithms have a mac length of 12 but crypto API digestsize can be larger */
#define TCP_AUTHOPT_MAXMACBUF			20
#define TCP_AUTHOPT_MAX_TRAFFIC_KEY_LEN		20
#define TCP_AUTHOPT_MACLEN			12

/* Constant data with per-algorithm information from RFC5926
 * The "KDF" and "MAC" happen to be the same for both algorithms.
 */
struct tcp_authopt_alg_imp {
	/* Name of algorithm in crypto-api */
	const char *alg_name;
	/* One of the TCP_AUTHOPT_ALG_* constants from uapi */
	u8 alg_id;
	/* Length of traffic key */
	u8 traffic_key_len;

	/* shared crypto_shash */
	struct mutex init_mutex;
	bool init_done;
	struct crypto_shash * __percpu *tfms;
};

static struct tcp_authopt_alg_imp tcp_authopt_alg_list[] = {
	{
		.alg_id = TCP_AUTHOPT_ALG_HMAC_SHA_1_96,
		.alg_name = "hmac(sha1)",
		.traffic_key_len = 20,
		.init_mutex = __MUTEX_INITIALIZER(tcp_authopt_alg_list[0].init_mutex),
	},
	{
		.alg_id = TCP_AUTHOPT_ALG_AES_128_CMAC_96,
		.alg_name = "cmac(aes)",
		.traffic_key_len = 16,
		.init_mutex = __MUTEX_INITIALIZER(tcp_authopt_alg_list[1].init_mutex),
	},
};

/* get a pointer to the tcp_authopt_alg instance or NULL if id invalid */
static inline struct tcp_authopt_alg_imp *tcp_authopt_alg_get(int alg_num)
{
	if (alg_num <= 0 || alg_num > 2)
		return NULL;
	return &tcp_authopt_alg_list[alg_num - 1];
}

static void __tcp_authopt_alg_free(struct tcp_authopt_alg_imp *alg)
{
	int cpu;
	struct crypto_shash *tfm;

	if (!alg->tfms)
		return;
	for_each_possible_cpu(cpu) {
		tfm = *per_cpu_ptr(alg->tfms, cpu);
		if (tfm) {
			crypto_free_shash(tfm);
			*per_cpu_ptr(alg->tfms, cpu) = NULL;
		}
	}
	free_percpu(alg->tfms);
	alg->tfms = NULL;
}

static int __tcp_authopt_alg_init(struct tcp_authopt_alg_imp *alg)
{
	struct crypto_shash *tfm;
	int cpu;
	int err;

	BUILD_BUG_ON(TCP_AUTHOPT_MAXMACBUF < TCPOLEN_AUTHOPT_OUTPUT);
	if (WARN_ON_ONCE(alg->traffic_key_len > TCP_AUTHOPT_MAX_TRAFFIC_KEY_LEN))
		return -ENOBUFS;

	alg->tfms = alloc_percpu(struct crypto_shash*);
	if (!alg->tfms)
		return -ENOMEM;
	for_each_possible_cpu(cpu) {
		tfm = crypto_alloc_shash(alg->alg_name, 0, 0);
		if (IS_ERR(tfm)) {
			err = PTR_ERR(tfm);
			goto out_err;
		}

		/* sanity checks: */
		if (WARN_ON_ONCE(crypto_shash_digestsize(tfm) != alg->traffic_key_len)) {
			err = -EINVAL;
			goto out_err;
		}
		if (WARN_ON_ONCE(crypto_shash_digestsize(tfm) > TCP_AUTHOPT_MAXMACBUF)) {
			err = -EINVAL;
			goto out_err;
		}

		*per_cpu_ptr(alg->tfms, cpu) = tfm;
	}
	return 0;

out_err:
	__tcp_authopt_alg_free(alg);
	return err;
}

static int tcp_authopt_alg_require(struct tcp_authopt_alg_imp *alg)
{
	int err = 0;

	mutex_lock(&alg->init_mutex);
	if (alg->init_done)
		goto out;
	err = __tcp_authopt_alg_init(alg);
	if (err)
		goto out;
	pr_info("initialized tcp-ao algorithm %s", alg->alg_name);
	alg->init_done = true;

out:
	mutex_unlock(&alg->init_mutex);
	return err;
}

static struct crypto_shash *tcp_authopt_alg_get_tfm(struct tcp_authopt_alg_imp *alg)
{
	preempt_disable();
	return *this_cpu_ptr(alg->tfms);
}

static void tcp_authopt_alg_put_tfm(struct tcp_authopt_alg_imp *alg, struct crypto_shash *tfm)
{
	WARN_ON(tfm != *this_cpu_ptr(alg->tfms));
	preempt_enable();
}

static struct crypto_shash *tcp_authopt_get_kdf_shash(struct tcp_authopt_key_info *key)
{
	return tcp_authopt_alg_get_tfm(key->alg);
}

static void tcp_authopt_put_kdf_shash(struct tcp_authopt_key_info *key,
				      struct crypto_shash *tfm)
{
	return tcp_authopt_alg_put_tfm(key->alg, tfm);
}

static struct crypto_shash *tcp_authopt_get_mac_shash(struct tcp_authopt_key_info *key)
{
	return tcp_authopt_alg_get_tfm(key->alg);
}

static void tcp_authopt_put_mac_shash(struct tcp_authopt_key_info *key,
				      struct crypto_shash *tfm)
{
	return tcp_authopt_alg_put_tfm(key->alg, tfm);
}

/* checks that ipv4 or ipv6 addr matches. */
static bool ipvx_addr_match(struct sockaddr_storage *a1,
			    struct sockaddr_storage *a2)
{
	if (a1->ss_family != a2->ss_family)
		return false;
	if (a1->ss_family == AF_INET && (
			((struct sockaddr_in *)a1)->sin_addr.s_addr !=
			((struct sockaddr_in *)a2)->sin_addr.s_addr))
		return false;
	if (a1->ss_family == AF_INET6 && !ipv6_addr_equal(
			&((struct sockaddr_in6 *)a1)->sin6_addr,
			&((struct sockaddr_in6 *)a2)->sin6_addr))
		return false;
	return true;
}

static bool tcp_authopt_key_match_exact(struct tcp_authopt_key_info *info,
					struct tcp_authopt_key *key)
{
	if (info->send_id != key->send_id)
		return false;
	if (info->recv_id != key->recv_id)
		return false;
	if ((info->flags & TCP_AUTHOPT_KEY_ADDR_BIND) != (key->recv_id & TCP_AUTHOPT_KEY_ADDR_BIND))
		return false;
	if (info->flags & TCP_AUTHOPT_KEY_ADDR_BIND)
		if (!ipvx_addr_match(&info->addr, &key->addr))
			return false;

	return true;
}

static bool tcp_authopt_key_match_skb_addr(
		struct tcp_authopt_key_info *key,
		struct sk_buff *skb)
{
	u16 keyaf = key->addr.ss_family;
	struct iphdr *iph = (struct iphdr *)skb_network_header(skb);

	if (keyaf == AF_INET && iph->version == 4) {
		struct sockaddr_in *key_addr = (struct sockaddr_in *)&key->addr;

		return iph->saddr == key_addr->sin_addr.s_addr;
	} else if (keyaf == AF_INET6 && iph->version == 6) {
		struct ipv6hdr *ip6h = (struct ipv6hdr *)skb_network_header(skb);
		struct sockaddr_in6 *key_addr = (struct sockaddr_in6 *)&key->addr;

		return ipv6_addr_equal(&ip6h->saddr, &key_addr->sin6_addr);
	} else {
		/* This actually happens with ipv6-mapped-ipv4-addresses
		 * IPv6 listen sockets will be asked to validate ipv4 packets.
		 */
		return false;
	}
}

static bool tcp_authopt_key_match_sk_addr(
		struct tcp_authopt_key_info *key,
		const struct sock *addr_sk)
{
	u16 keyaf = key->addr.ss_family;

	/* This probably can't happen even with ipv4-mapped-ipv6 */
	if (keyaf != addr_sk->sk_family)
		return false;

	if (keyaf == AF_INET) {
		struct sockaddr_in *key_addr = (struct sockaddr_in *)&key->addr;

		return addr_sk->sk_daddr == key_addr->sin_addr.s_addr;
	} else if (keyaf == AF_INET6) {
		struct sockaddr_in6 *key_addr = (struct sockaddr_in6 *)&key->addr;

		return ipv6_addr_equal(&addr_sk->sk_v6_daddr, &key_addr->sin6_addr);
	}

	return false;
}

static struct tcp_authopt_key_info *tcp_authopt_key_lookup_exact(const struct sock *sk,
								 struct tcp_authopt_info *info,
								 struct tcp_authopt_key *ukey)
{
	struct tcp_authopt_key_info *key_info;

	hlist_for_each_entry_rcu(key_info, &info->head, node, lockdep_sock_is_held(sk))
		if (tcp_authopt_key_match_exact(key_info, ukey))
			return key_info;

	return NULL;
}

static struct tcp_authopt_key_info *tcp_authopt_lookup_send(struct tcp_authopt_info *info,
							    const struct sock *addr_sk,
							    int send_id)
{
	struct tcp_authopt_key_info *result = NULL;
	struct tcp_authopt_key_info *key;

	hlist_for_each_entry_rcu(key, &info->head, node, 0) {
		if (send_id >= 0 && key->send_id != send_id)
			continue;
		if (key->flags & TCP_AUTHOPT_KEY_ADDR_BIND)
			if (!tcp_authopt_key_match_sk_addr(key, addr_sk))
				continue;
		if (result && net_ratelimit())
			pr_warn("ambiguous tcp authentication keys configured for send\n");
		result = key;
	}

	return result;
}

/**
 * tcp_authopt_select_key - select key for sending
 *
 * addr_sk is the sock used for comparing daddr, it is only different from sk in
 * the synack case.
 *
 * Result is protected by RCU and can't be stored, it may only be passed to
 * tcp_authopt_hash and only under a single rcu_read_lock.
 *
 * If locked is false then we're not holding the socket lock. This happens for
 * some timewait and reset cases.
 */
struct tcp_authopt_key_info *__tcp_authopt_select_key(
		const struct sock *sk,
		struct tcp_authopt_info *info,
		const struct sock *addr_sk,
		u8 *rnextkeyid,
		bool locked)
{
	struct tcp_authopt_key_info *key, *new_key = NULL;

	/* Listen sockets don't refer to any specific connection so we don't try
	 * to keep using the same key and ignore any received keyids.
	 */
	if (sk->sk_state == TCP_LISTEN) {
		int send_keyid = -1;
		if (info->flags & TCP_AUTHOPT_FLAG_LOCK_KEYID)
			send_keyid = info->send_keyid;
		key = tcp_authopt_lookup_send(info, addr_sk, send_keyid);
		if (key)
			*rnextkeyid = key->recv_id;

		return key;
	}

	if (locked)
		key = rcu_dereference_protected(info->send_key, lockdep_sock_is_held(sk));
	else
		key = rcu_dereference(info->send_key);

	/* Try to keep the same sending key unless user or peer requires a different key
	 * User request (via TCP_AUTHOPT_FLAG_LOCK_KEYID) always overrides peer request.
	 */
	if (info->flags & TCP_AUTHOPT_FLAG_LOCK_KEYID) {
		int send_keyid = info->send_keyid;

		if (!key || key->send_id != send_keyid)
			new_key = tcp_authopt_lookup_send(info, addr_sk, send_keyid);
	} else {
		if (!key || key->send_id != info->recv_rnextkeyid)
			new_key = tcp_authopt_lookup_send(info, addr_sk, info->recv_rnextkeyid);
	}
	/* If no key found with specific send_id try anything else. */
	if (!key && !new_key)
		new_key = tcp_authopt_lookup_send(info, addr_sk, -1);

	/* Update current key only if we hold the socket lock, otherwise we might
	 * store a pointer that goes stale
	 */
	if (new_key && key != new_key) {
		key = new_key;
		if (locked)
			rcu_assign_pointer(info->send_key, key);
	}

	if (key) {
		if (info->flags & TCP_AUTHOPT_FLAG_LOCK_RNEXTKEYID)
			*rnextkeyid = info->send_rnextkeyid;
		else
			*rnextkeyid = info->send_rnextkeyid = key->recv_id;
	}

	return key;
}

static struct tcp_authopt_info *__tcp_authopt_info_get_or_create(struct sock *sk)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct tcp_authopt_info *info;

	info = rcu_dereference_check(tp->authopt_info, lockdep_sock_is_held(sk));
	if (info)
		return info;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return ERR_PTR(-ENOMEM);

	/* Never released: */
	static_branch_inc(&tcp_authopt_needed);
	sk_nocaps_add(sk, NETIF_F_GSO_MASK);
	INIT_HLIST_HEAD(&info->head);
	rcu_assign_pointer(tp->authopt_info, info);

	return info;
}

#define TCP_AUTHOPT_KNOWN_FLAGS ( \
	TCP_AUTHOPT_FLAG_LOCK_KEYID | \
	TCP_AUTHOPT_FLAG_LOCK_RNEXTKEYID | \
	TCP_AUTHOPT_FLAG_REJECT_UNEXPECTED)

/* Like copy_from_sockopt except tolerate different optlen for compatibility reasons
 *
 * If the src is shorter then it's from an old userspace and the rest of dst is
 * filled with zeros.
 *
 * If the dst is shorter then src is from a newer userspace and we only accept
 * if the rest of the option is all zeros.
 *
 * This allows sockopts to grow as long as for new fields zeros has no effect.
 */
static int _copy_from_sockptr_tolerant(
		u8* dst, unsigned int dstlen,
		sockptr_t src, unsigned int srclen)
{
	int err;

	/* If userspace optlen is too short fill the rest with zeros */
	if (srclen > dstlen) {
		if (sockptr_is_kernel(src))
			return -EINVAL;
		err = check_zeroed_user(src.user + dstlen, srclen - dstlen);
		if (err < 0)
			return err;
		if (err == 0)
			return -EINVAL;
	}
	err = copy_from_sockptr(dst, src, min(srclen, dstlen));
	if (err)
		return err;
	if (srclen < dstlen)
		memset(dst + dstlen, 0, dstlen - srclen);

	return err;
}

int tcp_set_authopt(struct sock *sk, sockptr_t optval, unsigned int optlen)
{
	struct tcp_authopt opt;
	struct tcp_authopt_info *info;
	int err;

	sock_owned_by_me(sk);
	if (!sysctl_tcp_authopt)
		return -EPERM;

	err = _copy_from_sockptr_tolerant((u8*)&opt, sizeof(opt), optval, optlen);
	if (err)
		return err;

	if (opt.flags & ~TCP_AUTHOPT_KNOWN_FLAGS)
		return -EINVAL;

	info = __tcp_authopt_info_get_or_create(sk);
	if (IS_ERR(info))
		return PTR_ERR(info);

	info->flags = opt.flags & TCP_AUTHOPT_KNOWN_FLAGS;
	if (opt.flags & TCP_AUTHOPT_FLAG_LOCK_KEYID)
		info->send_keyid = opt.send_keyid;
	if (opt.flags & TCP_AUTHOPT_FLAG_LOCK_RNEXTKEYID)
		info->send_rnextkeyid = opt.send_rnextkeyid;

	return 0;
}

int tcp_get_authopt_val(struct sock *sk, struct tcp_authopt *opt)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct tcp_authopt_info *info;
	struct tcp_authopt_key_info *send_key;

	sock_owned_by_me(sk);
	if (!sysctl_tcp_authopt)
		return -EPERM;

	memset(opt, 0, sizeof(*opt));
	info = rcu_dereference_check(tp->authopt_info, lockdep_sock_is_held(sk));
	if (!info)
		return -ENOENT;

	opt->flags = info->flags & TCP_AUTHOPT_KNOWN_FLAGS;
	/* These keyids might be undefined, for example before connect.
	 * Reporting zero is not strictly correct because there are no reserved
	 * values.
	 */
	if ((send_key = rcu_dereference_check(info->send_key, lockdep_sock_is_held(sk))))
		opt->send_keyid = send_key->send_id;
	else
		opt->send_keyid = 0;
	opt->send_rnextkeyid = info->send_rnextkeyid;
	opt->recv_keyid = info->recv_keyid;
	opt->recv_rnextkeyid = info->recv_rnextkeyid;

	return 0;
}

/* Free key nicely, for living sockets */
static void tcp_authopt_key_del(struct sock *sk,
				struct tcp_authopt_info *info,
				struct tcp_authopt_key_info *key)
{
	sock_owned_by_me(sk);
	hlist_del_rcu(&key->node);
	if (rcu_dereference_protected(info->send_key, lockdep_sock_is_held(sk)) == key)
		rcu_assign_pointer(info->send_key, NULL);
	atomic_sub(sizeof(*key), &sk->sk_omem_alloc);
	kfree_rcu(key, rcu);
}

/* Free info and keys.
 * Don't touch tp->authopt_info, it might not even be assigned yes.
 */
void tcp_authopt_free(struct sock *sk, struct tcp_authopt_info *info)
{
	struct hlist_node *n;
	struct tcp_authopt_key_info *key;

	hlist_for_each_entry_safe(key, n, &info->head, node) {
		/* sk is NULL for timewait case
		 * struct timewait_sock doesn't track sk_omem_alloc
		 */
		if (sk)
			atomic_sub(sizeof(*key), &sk->sk_omem_alloc);
		hlist_del_rcu(&key->node);
		kfree_rcu(key, rcu);
	}
	kfree_rcu(info, rcu);
}

/* free everything and clear tcp_sock.authopt_info to NULL */
void tcp_authopt_clear(struct sock *sk)
{
	struct tcp_authopt_info *info;

	info = rcu_dereference_protected(tcp_sk(sk)->authopt_info, lockdep_sock_is_held(sk));
	if (info) {
		tcp_authopt_free(sk, info);
		tcp_sk(sk)->authopt_info = NULL;
	}
}

#define TCP_AUTHOPT_KEY_KNOWN_FLAGS ( \
	TCP_AUTHOPT_KEY_DEL | \
	TCP_AUTHOPT_KEY_EXCLUDE_OPTS | \
	TCP_AUTHOPT_KEY_ADDR_BIND)

int tcp_set_authopt_key(struct sock *sk, sockptr_t optval, unsigned int optlen)
{
	struct tcp_authopt_key opt;
	struct tcp_authopt_info *info;
	struct tcp_authopt_key_info *key_info, *old_key_info;
	struct tcp_authopt_alg_imp *alg;
	int err;

	sock_owned_by_me(sk);
	if (!sysctl_tcp_authopt)
		return -EPERM;

	err = _copy_from_sockptr_tolerant((u8*)&opt, sizeof(opt), optval, optlen);
	if (err)
		return err;

	if (opt.flags & ~TCP_AUTHOPT_KEY_KNOWN_FLAGS)
		return -EINVAL;

	if (opt.keylen > TCP_AUTHOPT_MAXKEYLEN)
		return -EINVAL;

	/* Delete is a special case: */
	if (opt.flags & TCP_AUTHOPT_KEY_DEL) {
		info = rcu_dereference_check(tcp_sk(sk)->authopt_info, lockdep_sock_is_held(sk));
		if (!info)
			return -ENOENT;
		key_info = tcp_authopt_key_lookup_exact(sk, info, &opt);
		if (!key_info)
			return -ENOENT;
		tcp_authopt_key_del(sk, info, key_info);
		return 0;
	}

	/* check key family */
	if (opt.flags & TCP_AUTHOPT_KEY_ADDR_BIND) {
		if (sk->sk_family != opt.addr.ss_family)
			return -EINVAL;
	}

	/* Initialize tcp_authopt_info if not already set */
	info = __tcp_authopt_info_get_or_create(sk);
	if (IS_ERR(info))
		return PTR_ERR(info);

	/* check the algorithm */
	alg = tcp_authopt_alg_get(opt.alg);
	if (!alg)
		return -EINVAL;
	if (WARN_ON_ONCE(alg->alg_id != opt.alg))
		return -EINVAL;
	err = tcp_authopt_alg_require(alg);
	if (err)
		return err;

	key_info = sock_kmalloc(sk, sizeof(*key_info), GFP_KERNEL | __GFP_ZERO);
	if (!key_info)
		return -ENOMEM;
	/* If an old key exists with exact ID then remove and replace.
	 * RCU-protected readers might observe both and pick any.
	 */
	if ((old_key_info = tcp_authopt_key_lookup_exact(sk, info, &opt)))
		tcp_authopt_key_del(sk, info, old_key_info);
	key_info->flags = opt.flags & TCP_AUTHOPT_KEY_KNOWN_FLAGS;
	key_info->send_id = opt.send_id;
	key_info->recv_id = opt.recv_id;
	key_info->alg_id = opt.alg;
	key_info->alg = alg;
	key_info->keylen = opt.keylen;
	memcpy(key_info->key, opt.key, opt.keylen);
	memcpy(&key_info->addr, &opt.addr, sizeof(key_info->addr));
	hlist_add_head_rcu(&key_info->node, &info->head);

	return 0;
}

static int tcp_authopt_get_isn(struct sock *sk,
			       struct sk_buff *skb,
			       int input,
			       __be32 *sisn,
			       __be32 *disn)
{
	struct tcp_authopt_info *authopt_info;
	struct tcphdr *th = tcp_hdr(skb);

	/* special cases for SYN and SYN/ACK */
	if (th->syn && !th->ack) {
		*sisn = th->seq;
		*disn = 0;
		return 0;
	}
	if (th->syn && th->ack) {
		*sisn = th->seq;
		*disn = htonl(ntohl(th->ack_seq) - 1);
		return 0;
	}

	/* Fetching authopt_info like this should be safe because authopt_info
	 * is never released intil the socket is being closed
	 *
	 * tcp_timewait_sock is handled but not tcp_request_sock.
	 * for the synack case sk should be the listen socket.
	 */
	rcu_read_lock();
	if (sk->sk_state == TCP_TIME_WAIT)
		authopt_info = tcp_twsk(sk)->tw_authopt_info;
	else if (unlikely(sk->sk_state == TCP_NEW_SYN_RECV)) {
		/* should never happen, sk should be the listen socket */
		authopt_info = NULL;
		WARN_ONCE(1, "TCP-AO can't sign with request sock\n");
		return -EINVAL;
	} else if (sk->sk_state == TCP_LISTEN) {
		/* Signature computation for non-syn packet on a listen
		 * socket is not possible because we lack the initial
		 * sequence numbers.
		 *
		 * Input segments that are not matched by any request,
		 * established or timewait socket will get here. These
		 * are not normally sent by peers.
		 *
		 * Their signature might be valid but we don't have
		 * enough state to determine that. TCP-MD5 can attempt
		 * to validate and reply with a signed RST because it
		 * doesn't care about ISNs.
		 *
		 * Reporting an error from signature code causes the
		 * packet to be discarded which is good.
		 */
		if (input) {
			/* Assume this is an ACK to a SYN/ACK
			 * This will incorrectly report "failed
			 * signature" for segments without a connection.
			 */
			*sisn = htonl(ntohl(th->seq) - 1);
			*disn = htonl(ntohl(th->ack_seq) - 1);
			rcu_read_unlock();
			return 0;
		} else {
			/* This would be an internal bug. */
			authopt_info = NULL;
			WARN_ONCE(1, "TCP-AO can't sign non-syn from TCP_LISTEN sock\n");
			return -EINVAL;
		}
	} else
		authopt_info = rcu_dereference(tcp_sk(sk)->authopt_info);
	if (!authopt_info) {
		rcu_read_unlock();
		return -EINVAL;
	}
	/* Initial sequence numbers for ESTABLISHED connections from info */
	if (input) {
		*sisn = htonl(authopt_info->dst_isn);
		*disn = htonl(authopt_info->src_isn);
	} else {
		*sisn = htonl(authopt_info->src_isn);
		*disn = htonl(authopt_info->dst_isn);
	}
	rcu_read_unlock();

	return 0;
}

static int tcp_authopt_clone_keys(struct sock *newsk,
				  const struct sock *oldsk,
				  struct tcp_authopt_info *new_info,
				  struct tcp_authopt_info *old_info)
{
	struct tcp_authopt_key_info *old_key;
	struct tcp_authopt_key_info *new_key;

	hlist_for_each_entry_rcu(old_key, &old_info->head, node, lockdep_sock_is_held(oldsk)) {
		new_key = sock_kmalloc(newsk, sizeof(*new_key), GFP_ATOMIC);
		if (!new_key)
			return -ENOMEM;
		memcpy(new_key, old_key, sizeof(*new_key));
		hlist_add_head_rcu(&new_key->node, &new_info->head);
	}

	return 0;
}

/** Called to create accepted sockets.
 *
 *  Need to copy authopt info from listen socket.
 */
int __tcp_authopt_openreq(struct sock *newsk, const struct sock *oldsk, struct request_sock *req)
{
	struct tcp_authopt_info *old_info;
	struct tcp_authopt_info *new_info;
	int err;

	old_info = rcu_dereference(tcp_sk(oldsk)->authopt_info);
	if (!old_info)
		return 0;

	/* Clear value copies from oldsk: */
	rcu_assign_pointer(tcp_sk(newsk)->authopt_info, NULL);

	new_info = kzalloc(sizeof(*new_info), GFP_ATOMIC);
	if (!new_info)
		return -ENOMEM;

	new_info->src_isn = tcp_rsk(req)->snt_isn;
	new_info->dst_isn = tcp_rsk(req)->rcv_isn;
	INIT_HLIST_HEAD(&new_info->head);
	err = tcp_authopt_clone_keys(newsk, oldsk, new_info, old_info);
	if (err) {
		tcp_authopt_free(newsk, new_info);
		return err;
	}
	sk_nocaps_add(newsk, NETIF_F_GSO_MASK);
	rcu_assign_pointer(tcp_sk(newsk)->authopt_info, new_info);

	return 0;
}

/* feed traffic key into shash */
static int tcp_authopt_shash_traffic_key(struct shash_desc *desc,
					 struct sock *sk,
					 struct sk_buff *skb,
					 bool input,
					 bool ipv6)
{
	struct tcphdr *th = tcp_hdr(skb);
	int err;
	__be32 sisn, disn;
	__be16 digestbits = htons(crypto_shash_digestsize(desc->tfm) * 8);

	// RFC5926 section 3.1.1.1
	err = crypto_shash_update(desc, "\x01TCP-AO", 7);
	if (err)
		return err;

	/* Addresses from packet on input and from sk_common on output
	 * This is because on output MAC is computed before prepending IP header
	 */
	if (input) {
		if (ipv6)
			err = crypto_shash_update(desc, (u8 *)&ipv6_hdr(skb)->saddr, 32);
		else
			err = crypto_shash_update(desc, (u8 *)&ip_hdr(skb)->saddr, 8);
		if (err)
			return err;
	} else {
		if (ipv6) {
			err = crypto_shash_update(desc, (u8 *)&sk->sk_v6_rcv_saddr, 16);
			if (err)
				return err;
			err = crypto_shash_update(desc, (u8 *)&sk->sk_v6_daddr, 16);
			if (err)
				return err;
		} else {
			err = crypto_shash_update(desc, (u8 *)&sk->sk_rcv_saddr, 4);
			if (err)
				return err;
			err = crypto_shash_update(desc, (u8 *)&sk->sk_daddr, 4);
			if (err)
				return err;
		}
	}

	/* TCP ports from header */
	err = crypto_shash_update(desc, (u8 *)&th->source, 4);
	if (err)
		return err;
	err = tcp_authopt_get_isn(sk, skb, input, &sisn, &disn);
	if (err)
		return err;
	err = crypto_shash_update(desc, (u8 *)&sisn, 4);
	if (err)
		return err;
	err = crypto_shash_update(desc, (u8 *)&disn, 4);
	if (err)
		return err;
	err = crypto_shash_update(desc, (u8 *)&digestbits, 2);
	if (err)
		return err;

	return 0;
}

/* Convert a variable-length key to a 16-byte fixed-length key for AES-CMAC
 * This is described in RFC5926 section 3.1.1.2
 */
static int aes_setkey_derived(struct crypto_shash *tfm, u8 *key, size_t keylen)
{
	static const u8 zeros[16] = {0};
	u8 derived_key[16];
	int err;

	if (WARN_ON_ONCE(crypto_shash_digestsize(tfm) != 16))
		return -EINVAL;
	err = crypto_shash_setkey(tfm, zeros, sizeof(zeros));
	if (err)
		return err;
	err = crypto_shash_tfm_digest(tfm, key, keylen, derived_key);
	if (err)
		return err;
	return crypto_shash_setkey(tfm, derived_key, sizeof(derived_key));
}

static int tcp_authopt_setkey(struct crypto_shash *tfm, struct tcp_authopt_key_info *key)
{
	if (key->alg_id == TCP_AUTHOPT_ALG_AES_128_CMAC_96 && key->keylen != 16)
		return aes_setkey_derived(tfm, key->key, key->keylen);
	else
		return crypto_shash_setkey(tfm, key->key, key->keylen);
}

static int tcp_authopt_get_traffic_key(struct sock *sk,
				       struct sk_buff *skb,
				       struct tcp_authopt_key_info *key,
				       bool input,
				       bool ipv6,
				       u8 *traffic_key)
{
	SHASH_DESC_ON_STACK(desc, kdf_tfm);
	struct crypto_shash *kdf_tfm;
	int err;

	kdf_tfm = tcp_authopt_get_kdf_shash(key);
	if (IS_ERR(kdf_tfm))
		return PTR_ERR(kdf_tfm);

	err = tcp_authopt_setkey(kdf_tfm, key);
	if (err)
		goto out;

	desc->tfm = kdf_tfm;
	err = crypto_shash_init(desc);
	if (err)
		goto out;

	err = tcp_authopt_shash_traffic_key(desc, sk, skb, input, ipv6);
	if (err)
		goto out;

	err = crypto_shash_final(desc, traffic_key);
	if (err)
		goto out;

out:
	tcp_authopt_put_kdf_shash(key, kdf_tfm);
	return err;
}

struct tcp_v4_authopt_context_data {
	__be32 saddr;
	__be32 daddr;
	__be16 sport;
	__be16 dport;
	__be32 sisn;
	__be32 disn;
	__be16 digestbits;
} __packed;

static int tcp_v4_authopt_get_traffic_key_noskb(
		struct tcp_authopt_key_info *key,
		__be32 saddr,
		__be32 daddr,
		__be16 sport,
		__be16 dport,
		__be32 sisn,
		__be32 disn,
		u8 *traffic_key)
{
	int err;
	struct crypto_shash *kdf_tfm;
	SHASH_DESC_ON_STACK(desc, kdf_tfm);
	struct tcp_v4_authopt_context_data data;
	BUILD_BUG_ON(sizeof(data) != 22);

	kdf_tfm = tcp_authopt_get_kdf_shash(key);
	if (IS_ERR(kdf_tfm))
		return PTR_ERR(kdf_tfm);

	err = tcp_authopt_setkey(kdf_tfm, key);
	if (err)
		goto out;

	desc->tfm = kdf_tfm;
	err = crypto_shash_init(desc);
	if (err)
		goto out;

	// RFC5926 section 3.1.1.1
	// Separate to keep alignment semi-sane
	err = crypto_shash_update(desc, "\x01TCP-AO", 7);
	if (err)
		return err;
	data.saddr = saddr;
	data.daddr = daddr;
	data.sport = sport;
	data.dport = dport;
	data.sisn = sisn;
	data.disn = disn;
	data.digestbits = htons(crypto_shash_digestsize(desc->tfm) * 8);

	err = crypto_shash_update(desc, (u8*)&data, sizeof(data));
	if (err)
		goto out;
	err = crypto_shash_final(desc, traffic_key);
	if (err)
		goto out;

out:
	tcp_authopt_put_kdf_shash(key, kdf_tfm);
	return err;
}

static int crypto_shash_update_zero(struct shash_desc *desc, int len)
{
	u8 zero = 0;
	int i, err;

	for (i = 0; i < len; ++i) {
		err = crypto_shash_update(desc, &zero, 1);
		if (err)
			return err;
	}

	return 0;
}

static int tcp_authopt_hash_tcp4_pseudoheader(struct shash_desc *desc,
					      __be32 saddr,
					      __be32 daddr,
					      int nbytes)
{
	struct tcp4_pseudohdr phdr = {
		.saddr = saddr,
		.daddr = daddr,
		.pad = 0,
		.protocol = IPPROTO_TCP,
		.len = htons(nbytes)
	};
	return crypto_shash_update(desc, (u8 *)&phdr, sizeof(phdr));
}

static int tcp_authopt_hash_tcp6_pseudoheader(struct shash_desc *desc,
					      struct in6_addr *saddr,
					      struct in6_addr *daddr,
					      u32 plen)
{
	int err;
	__be32 buf[2];

	buf[0] = htonl(plen);
	buf[1] = htonl(IPPROTO_TCP);

	err = crypto_shash_update(desc, (u8 *)saddr, sizeof(*saddr));
	if (err)
		return err;
	err = crypto_shash_update(desc, (u8 *)daddr, sizeof(*daddr));
	if (err)
		return err;
	return crypto_shash_update(desc, (u8 *)&buf, sizeof(buf));
}

/* TCP authopt as found in header */
struct tcphdr_authopt {
	u8 num;
	u8 len;
	u8 keyid;
	u8 rnextkeyid;
	u8 mac[0];
};

/* Find TCP_AUTHOPT in header.
 *
 * Returns pointer to TCP_AUTHOPT or NULL if not found.
 */
static u8 *tcp_authopt_find_option(struct tcphdr *th)
{
	int length = (th->doff << 2) - sizeof(*th);
	u8 *ptr = (u8 *)(th + 1);

	while (length >= 2) {
		int opcode = *ptr++;
		int opsize;

		switch (opcode) {
		case TCPOPT_EOL:
			return NULL;
		case TCPOPT_NOP:
			length--;
			continue;
		default:
			if (length < 2)
				return NULL;
			opsize = *ptr++;
			if (opsize < 2)
				return NULL;
			if (opsize > length)
				return NULL;
			if (opcode == TCPOPT_AUTHOPT)
				return ptr - 2;
		}
		ptr += opsize - 2;
		length -= opsize;
	}
	return NULL;
}

/** Hash tcphdr options.
 *  If include_options is false then only the TCPOPT_AUTHOPT option itself is hashed
 *  Maybe we could skip option parsing by assuming the AUTHOPT header is at hash_location-4?
 */
static int tcp_authopt_hash_opts(struct shash_desc *desc,
				 struct tcphdr *th,
				 bool include_options)
{
	int err;
	/* start of options */
	u8 *tcp_opts = (u8 *)(th + 1);
	/* end of options */
	u8 *tcp_data = ((u8 *)th) + th->doff * 4;
	/* pointer to TCPOPT_AUTHOPT */
	u8 *authopt_ptr = tcp_authopt_find_option(th);
	u8 authopt_len;

	if (!authopt_ptr)
		return -EINVAL;
	authopt_len = *(authopt_ptr + 1);

	if (include_options) {
		err = crypto_shash_update(desc, tcp_opts, authopt_ptr - tcp_opts + 4);
		if (err)
			return err;
		err = crypto_shash_update_zero(desc, authopt_len - 4);
		if (err)
			return err;
		err = crypto_shash_update(desc,
					  authopt_ptr + authopt_len,
					  tcp_data - (authopt_ptr + authopt_len));
		if (err)
			return err;
	} else {
		err = crypto_shash_update(desc, authopt_ptr, 4);
		if (err)
			return err;
		err = crypto_shash_update_zero(desc, authopt_len - 4);
		if (err)
			return err;
	}

	return 0;
}

static int skb_shash_frags(struct shash_desc *desc,
			   struct sk_buff *skb)
{
	struct sk_buff *frag_iter;
	int err, i;

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		skb_frag_t *f = &skb_shinfo(skb)->frags[i];
		u32 p_off, p_len, copied;
		struct page *p;
		u8 *vaddr;

		skb_frag_foreach_page(f, skb_frag_off(f), skb_frag_size(f),
				      p, p_off, p_len, copied) {
			vaddr = kmap_atomic(p);
			err = crypto_shash_update(desc, vaddr + p_off, p_len);
			kunmap_atomic(vaddr);
			if (err)
				return err;
		}
	}

	skb_walk_frags(skb, frag_iter) {
		err = skb_shash_frags(desc, frag_iter);
		if (err)
			return err;
	}

	return 0;
}

static int tcp_authopt_hash_packet(struct crypto_shash *tfm,
				   struct sock *sk,
				   struct sk_buff *skb,
				   bool input,
				   bool ipv6,
				   bool include_options,
				   u8 *macbuf)
{
	struct tcphdr *th = tcp_hdr(skb);
	SHASH_DESC_ON_STACK(desc, tfm);
	int err;

	/* NOTE: SNE unimplemented */
	__be32 sne = 0;

	desc->tfm = tfm;
	err = crypto_shash_init(desc);
	if (err)
		return err;

	err = crypto_shash_update(desc, (u8 *)&sne, 4);
	if (err)
		return err;

	if (ipv6) {
		struct in6_addr *saddr;
		struct in6_addr *daddr;

		if (input) {
			saddr = &ipv6_hdr(skb)->saddr;
			daddr = &ipv6_hdr(skb)->daddr;
		} else {
			saddr = &sk->sk_v6_rcv_saddr;
			daddr = &sk->sk_v6_daddr;
		}
		err = tcp_authopt_hash_tcp6_pseudoheader(desc, saddr, daddr, skb->len);
		if (err)
			return err;
	} else {
		__be32 saddr;
		__be32 daddr;

		if (input) {
			saddr = ip_hdr(skb)->saddr;
			daddr = ip_hdr(skb)->daddr;
		} else {
			saddr = sk->sk_rcv_saddr;
			daddr = sk->sk_daddr;
		}
		err = tcp_authopt_hash_tcp4_pseudoheader(desc, saddr, daddr, skb->len);
		if (err)
			return err;
	}

	// TCP header with checksum set to zero
	{
		struct tcphdr hashed_th = *th;

		hashed_th.check = 0;
		err = crypto_shash_update(desc, (u8 *)&hashed_th, sizeof(hashed_th));
		if (err)
			return err;
	}

	// TCP options
	err = tcp_authopt_hash_opts(desc, th, include_options);
	if (err)
		return err;

	// Rest of SKB->data
	err = crypto_shash_update(desc, (u8 *)th + th->doff * 4, skb_headlen(skb) - th->doff * 4);
	if (err)
		return err;

	err = skb_shash_frags(desc, skb);
	if (err)
		return err;

	return crypto_shash_final(desc, macbuf);
}

/**
 * __tcp_authopt_calc_mac - Compute packet MAC using key
 *
 * @macbuf: output buffer. Must be large enough to fit the digestsize of the
 *          underlying transform before truncation. Please use TCP_AUTHOPT_MAXMACBUF
 */
static int __tcp_authopt_calc_mac(struct sock *sk,
				  struct sk_buff *skb,
				  struct tcp_authopt_key_info *key,
				  bool input,
				  char *macbuf)
{
	struct crypto_shash *mac_tfm;
	u8 traffic_key[TCP_AUTHOPT_MAX_TRAFFIC_KEY_LEN];
	int err;
	bool ipv6 = (sk->sk_family != AF_INET);

	if (sk->sk_family != AF_INET && sk->sk_family != AF_INET6)
		return -EINVAL;

	err = tcp_authopt_get_traffic_key(sk, skb, key, input, ipv6, traffic_key);
	if (err)
		return err;

	mac_tfm = tcp_authopt_get_mac_shash(key);
	if (IS_ERR(mac_tfm))
		return PTR_ERR(mac_tfm);
	err = crypto_shash_setkey(mac_tfm, traffic_key, key->alg->traffic_key_len);
	if (err)
		goto out;

	err = tcp_authopt_hash_packet(mac_tfm,
				      sk,
				      skb,
				      input,
				      ipv6,
				      !(key->flags & TCP_AUTHOPT_KEY_EXCLUDE_OPTS),
				      macbuf);

out:
	tcp_authopt_put_mac_shash(key, mac_tfm);
	return err;
}

/**
 * tcp_authopt_hash - fill in the mac
 *
 * The key must come from tcp_authopt_select_key.
 */
int tcp_authopt_hash(char *hash_location,
		     struct tcp_authopt_key_info *key,
		     struct sock *sk,
		     struct sk_buff *skb)
{
	/* MAC inside option is truncated to 12 bytes but crypto API needs output
	 * buffer to be large enough so we use a buffer on the stack.
	 */
	u8 macbuf[TCP_AUTHOPT_MAXMACBUF];
	int err;

	err = __tcp_authopt_calc_mac(sk, skb, key, false, macbuf);
	if (err) {
		/* If mac calculation fails and caller doesn't handle the error
		 * try to make it obvious inside the packet.
		 */
		memset(hash_location, 0, TCP_AUTHOPT_MACLEN);
		return err;
	}
	memcpy(hash_location, macbuf, TCP_AUTHOPT_MACLEN);

	return 0;
}

/**
 * tcp_v4_authopt_hash_hdr - Hash tcp+ipv4 header without SKB
 *
 * The key must come from tcp_authopt_select_key.
 */
int tcp_v4_authopt_hash_reply(char *hash_location,
			      struct tcp_authopt_info *info,
			      struct tcp_authopt_key_info *key,
			      __be32 saddr,
			      __be32 daddr,
			      struct tcphdr *th)
{
	struct crypto_shash *mac_tfm;
	u8 macbuf[TCP_AUTHOPT_MAXMACBUF];
	u8 traffic_key[TCP_AUTHOPT_MAX_TRAFFIC_KEY_LEN];
	SHASH_DESC_ON_STACK(desc, tfm);
	__be32 sne = 0;
	int err;

	/* Call special code path for computing traffic key without skb
	 * This can be called from tcp_v4_reqsk_send_ack so caching would be
	 * difficult here.
	 */
	err = tcp_v4_authopt_get_traffic_key_noskb(
			key,
			saddr,
			daddr,
			th->source,
			th->dest,
			htonl(info->src_isn),
			htonl(info->dst_isn),
			traffic_key);
	if (err)
		goto out_err_traffic_key;

	/* Init mac shash */
	mac_tfm = tcp_authopt_get_mac_shash(key);
	if (IS_ERR(mac_tfm))
		return PTR_ERR(mac_tfm);
	err = crypto_shash_setkey(mac_tfm, traffic_key, key->alg->traffic_key_len);
	if (err)
		goto out_err;

	desc->tfm = mac_tfm;
	err = crypto_shash_init(desc);
	if (err)
		return err;

	err = crypto_shash_update(desc, (u8 *)&sne, 4);
	if (err)
		return err;

	err = tcp_authopt_hash_tcp4_pseudoheader(desc, saddr, daddr, th->doff * 4);
	if (err)
		return err;

	// TCP header with checksum set to zero. Caller ensures this.
	if (WARN_ON_ONCE(th->check != 0))
		goto out_err;
	err = crypto_shash_update(desc, (u8 *)th, sizeof(*th));
	if (err)
		goto out_err;

	// TCP options
	err = tcp_authopt_hash_opts(desc, th, !(key->flags & TCP_AUTHOPT_KEY_EXCLUDE_OPTS));
	if (err)
		goto out_err;

	err = crypto_shash_final(desc, macbuf);
	if (err)
		goto out_err;
	memcpy(hash_location, macbuf, TCP_AUTHOPT_MACLEN);

	tcp_authopt_put_mac_shash(key, mac_tfm);
	return 0;

out_err:
	tcp_authopt_put_mac_shash(key, mac_tfm);
out_err_traffic_key:
	memset(hash_location, 0, TCP_AUTHOPT_MACLEN);
	return err;
}

static struct tcp_authopt_key_info *tcp_authopt_lookup_recv(struct sock *sk,
							    struct sk_buff *skb,
							    struct tcp_authopt_info *info,
							    int recv_id)
{
	struct tcp_authopt_key_info *result = NULL;
	struct tcp_authopt_key_info *key;

	/* multiple matches will cause occasional failures */
	hlist_for_each_entry_rcu(key, &info->head, node, 0) {
		if (recv_id >= 0 && key->recv_id != recv_id)
			continue;
		if (key->flags & TCP_AUTHOPT_KEY_ADDR_BIND &&
				!tcp_authopt_key_match_skb_addr(key, skb))
			continue;
		if (result && net_ratelimit())
			pr_warn("ambiguous tcp authentication keys configured for receive\n");
		result = key;
	}

	return result;
}

int __tcp_authopt_inbound_check(struct sock *sk, struct sk_buff *skb, struct tcp_authopt_info *info)
{
	struct tcphdr *th = (struct tcphdr *)skb_transport_header(skb);
	struct tcphdr_authopt *opt;
	struct tcp_authopt_key_info *key;
	u8 macbuf[TCP_AUTHOPT_MAXMACBUF];
	int err;

	opt = (struct tcphdr_authopt *)tcp_authopt_find_option(th);
	/* RFC5925 2.2: An endpoint MUST NOT use TCP-AO for the same connection
	 * in which TCP MD5 is used. When both options appear, TCP MUST silently
	 * discard the segment.
	 */
	if (tcp_parse_md5sig_option(th)) {
		NET_INC_STATS(sock_net(sk), LINUX_MIB_TCPAUTHOPTFAILURE);
		return -EINVAL;
	}
	key = tcp_authopt_lookup_recv(sk, skb, info, opt ? opt->keyid : -1);

	/* nothing found or expected */
	if (!opt && !key)
		return 0;
	if (!opt && key) {
		NET_INC_STATS(sock_net(sk), LINUX_MIB_TCPAUTHOPTFAILURE);
		net_info_ratelimited("TCP Authentication Missing\n");
		return -EINVAL;
	}
	if (opt && !key) {
		/* RFC5925 Section 7.3:
		 * A TCP-AO implementation MUST allow for configuration of the behavior
		 * of segments with TCP-AO but that do not match an MKT. The initial
		 * default of this configuration SHOULD be to silently accept such
		 * connections.
		 */
		if (info->flags & TCP_AUTHOPT_FLAG_REJECT_UNEXPECTED) {
			NET_INC_STATS(sock_net(sk), LINUX_MIB_TCPAUTHOPTFAILURE);
			net_info_ratelimited("TCP Authentication Unexpected: Rejected\n");
			return -EINVAL;
		} else {
			net_info_ratelimited("TCP Authentication Unexpected: Accepted\n");
			goto accept;
		}
	}

	/* bad inbound key len */
	if (TCPOLEN_AUTHOPT_OUTPUT != opt->len)
		return -EINVAL;

	err = __tcp_authopt_calc_mac(sk, skb, key, true, macbuf);
	if (err)
		return err;

	if (memcmp(macbuf, opt->mac, TCP_AUTHOPT_MACLEN)) {
		NET_INC_STATS(sock_net(sk), LINUX_MIB_TCPAUTHOPTFAILURE);
		net_info_ratelimited("TCP Authentication Failed\n");
		return -EINVAL;
	}

accept:
	/* Doing this for all valid packets will results in keyids temporarily
	 * flipping back and forth if packets are reordered or retransmitted
	 * but keys should eventually stabilize.
	 *
	 * This is connection-specific so don't store for listen sockets.
	 *
	 * We could store rnextkeyid from SYN in a request sock and use it for
	 * the SYNACK but we don't.
	 */
	if (sk->sk_state != TCP_LISTEN) {
		info->recv_keyid = opt->keyid;
		info->recv_rnextkeyid = opt->rnextkeyid;
	}

	return 1;
}
/* only for CONFIG_IPV6=m */
EXPORT_SYMBOL(__tcp_authopt_inbound_check);
