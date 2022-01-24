// SPDX-License-Identifier: GPL-2.0-or-later

#include <net/tcp_authopt.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <net/tcp.h>
#include <linux/kref.h>
#include <crypto/hash.h>

/* This is mainly intended to protect against local privilege escalations through
 * a rarely used feature so it is deliberately not namespaced.
 */
int sysctl_tcp_authopt;

/* This is enabled when first struct tcp_authopt_info is allocated and never released */
DEFINE_STATIC_KEY_FALSE(tcp_authopt_needed_key);
EXPORT_SYMBOL(tcp_authopt_needed_key);

/* All current algorithms have a mac length of 12 but crypto API digestsize can be larger */
#define TCP_AUTHOPT_MAXMACBUF			20
#define TCP_AUTHOPT_MAX_TRAFFIC_KEY_LEN		20
#define TCP_AUTHOPT_MACLEN			12

struct tcp_authopt_alg_pool {
	struct crypto_ahash *tfm;
	struct ahash_request *req;
};

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

	/* shared crypto_ahash */
	struct mutex init_mutex;
	bool init_done;
	struct tcp_authopt_alg_pool __percpu *pool;
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

static int tcp_authopt_alg_pool_init(struct tcp_authopt_alg_imp *alg,
				     struct tcp_authopt_alg_pool *pool)
{
	pool->tfm = crypto_alloc_ahash(alg->alg_name, 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(pool->tfm))
		return PTR_ERR(pool->tfm);

	pool->req = ahash_request_alloc(pool->tfm, GFP_ATOMIC);
	if (IS_ERR(pool->req))
		return PTR_ERR(pool->req);
	ahash_request_set_callback(pool->req, 0, NULL, NULL);

	return 0;
}

static void tcp_authopt_alg_pool_free(struct tcp_authopt_alg_pool *pool)
{
	if (!IS_ERR_OR_NULL(pool->req))
		ahash_request_free(pool->req);
	pool->req = NULL;
	if (!IS_ERR_OR_NULL(pool->tfm))
		crypto_free_ahash(pool->tfm);
	pool->tfm = NULL;
}

static void __tcp_authopt_alg_free(struct tcp_authopt_alg_imp *alg)
{
	int cpu;
	struct tcp_authopt_alg_pool *pool;

	if (!alg->pool)
		return;
	for_each_possible_cpu(cpu) {
		pool = per_cpu_ptr(alg->pool, cpu);
		tcp_authopt_alg_pool_free(pool);
	}
	free_percpu(alg->pool);
	alg->pool = NULL;
}

static int __tcp_authopt_alg_init(struct tcp_authopt_alg_imp *alg)
{
	struct tcp_authopt_alg_pool *pool;
	int cpu;
	int err;

	BUILD_BUG_ON(TCP_AUTHOPT_MAXMACBUF < TCPOLEN_AUTHOPT_OUTPUT);
	if (WARN_ON_ONCE(alg->traffic_key_len > TCP_AUTHOPT_MAX_TRAFFIC_KEY_LEN))
		return -ENOBUFS;

	alg->pool = alloc_percpu(struct tcp_authopt_alg_pool);
	if (!alg->pool)
		return -ENOMEM;
	for_each_possible_cpu(cpu) {
		pool = per_cpu_ptr(alg->pool, cpu);
		err = tcp_authopt_alg_pool_init(alg, pool);
		if (err)
			goto out_err;

		pool = per_cpu_ptr(alg->pool, cpu);
		/* sanity checks: */
		if (WARN_ON_ONCE(crypto_ahash_digestsize(pool->tfm) != alg->traffic_key_len)) {
			err = -EINVAL;
			goto out_err;
		}
		if (WARN_ON_ONCE(crypto_ahash_digestsize(pool->tfm) > TCP_AUTHOPT_MAXMACBUF)) {
			err = -EINVAL;
			goto out_err;
		}
	}
	return 0;

out_err:
	pr_info("Failed to initialize %s\n", alg->alg_name);
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

static struct tcp_authopt_alg_pool *tcp_authopt_alg_get_pool(struct tcp_authopt_alg_imp *alg)
{
	local_bh_disable();
	return this_cpu_ptr(alg->pool);
}

static void tcp_authopt_alg_put_pool(struct tcp_authopt_alg_imp *alg,
				     struct tcp_authopt_alg_pool *pool)
{
	WARN_ON(pool != this_cpu_ptr(alg->pool));
	local_bh_enable();
}

static struct tcp_authopt_alg_pool *tcp_authopt_get_kdf_pool(struct tcp_authopt_key_info *key)
{
	return tcp_authopt_alg_get_pool(key->alg);
}

static void tcp_authopt_put_kdf_pool(struct tcp_authopt_key_info *key,
				     struct tcp_authopt_alg_pool *pool)
{
	return tcp_authopt_alg_put_pool(key->alg, pool);
}

static struct tcp_authopt_alg_pool *tcp_authopt_get_mac_pool(struct tcp_authopt_key_info *key)
{
	return tcp_authopt_alg_get_pool(key->alg);
}

static void tcp_authopt_put_mac_pool(struct tcp_authopt_key_info *key,
				     struct tcp_authopt_alg_pool *pool)
{
	return tcp_authopt_alg_put_pool(key->alg, pool);
}

static inline struct netns_tcp_authopt *sock_net_tcp_authopt(const struct sock *sk)
{
	return &sock_net(sk)->tcp_authopt;
}

static void tcp_authopt_key_release_kref(struct kref *ref)
{
	struct tcp_authopt_key_info *key = container_of(ref, struct tcp_authopt_key_info, ref);

	kfree_rcu(key, rcu);
}

static void tcp_authopt_key_put(struct tcp_authopt_key_info *key)
{
	if (key)
		kref_put(&key->ref, tcp_authopt_key_release_kref);
}

static void tcp_authopt_key_del(struct netns_tcp_authopt *net,
				struct tcp_authopt_key_info *key)
{
	lockdep_assert_held(&net->mutex);
	hlist_del_rcu(&key->node);
	key->flags |= TCP_AUTHOPT_KEY_DEL;
	kref_put(&key->ref, tcp_authopt_key_release_kref);
}

/* Free info and keys.
 * Don't touch tp->authopt_info, it might not even be assigned yes.
 */
void tcp_authopt_free(struct sock *sk, struct tcp_authopt_info *info)
{
	kfree_rcu(info, rcu);
}

/* Free everything and clear tcp_sock.authopt_info to NULL */
void tcp_authopt_clear(struct sock *sk)
{
	struct tcp_authopt_info *info;

	info = rcu_dereference_protected(tcp_sk(sk)->authopt_info, lockdep_sock_is_held(sk));
	if (info) {
		tcp_authopt_free(sk, info);
		tcp_sk(sk)->authopt_info = NULL;
	}
}
/* checks that ipv4 or ipv6 addr matches. */
static bool ipvx_addr_match(struct sockaddr_storage *a1,
			    struct sockaddr_storage *a2)
{
	if (a1->ss_family != a2->ss_family)
		return false;
	if (a1->ss_family == AF_INET &&
	    (((struct sockaddr_in *)a1)->sin_addr.s_addr !=
	     ((struct sockaddr_in *)a2)->sin_addr.s_addr))
		return false;
	if (a1->ss_family == AF_INET6 &&
	    !ipv6_addr_equal(&((struct sockaddr_in6 *)a1)->sin6_addr,
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
	if ((info->flags & TCP_AUTHOPT_KEY_IFINDEX) != (key->flags & TCP_AUTHOPT_KEY_IFINDEX))
		return false;
	if ((info->flags & TCP_AUTHOPT_KEY_IFINDEX) && info->l3index != key->ifindex)
		return false;
	if ((info->flags & TCP_AUTHOPT_KEY_ADDR_BIND) != (key->flags & TCP_AUTHOPT_KEY_ADDR_BIND))
		return false;
	if (info->flags & TCP_AUTHOPT_KEY_ADDR_BIND)
		if (!ipvx_addr_match(&info->addr, &key->addr))
			return false;

	return true;
}

static bool tcp_authopt_key_match_skb_addr(struct tcp_authopt_key_info *key,
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
	}

	/* This actually happens with ipv6-mapped-ipv4-addresses
	 * IPv6 listen sockets will be asked to validate ipv4 packets.
	 */
	return false;
}

static bool tcp_authopt_key_match_sk_addr(struct tcp_authopt_key_info *key,
					  const struct sock *addr_sk)
{
	u16 keyaf = key->addr.ss_family;

	/* This probably can't happen even with ipv4-mapped-ipv6 */
	if (keyaf != addr_sk->sk_family)
		return false;

	if (keyaf == AF_INET) {
		struct sockaddr_in *key_addr = (struct sockaddr_in *)&key->addr;

		return addr_sk->sk_daddr == key_addr->sin_addr.s_addr;
#if IS_ENABLED(CONFIG_IPV6)
	} else if (keyaf == AF_INET6) {
		struct sockaddr_in6 *key_addr = (struct sockaddr_in6 *)&key->addr;

		return ipv6_addr_equal(&addr_sk->sk_v6_daddr, &key_addr->sin6_addr);
#endif
	}

	return false;
}

static struct tcp_authopt_key_info *tcp_authopt_key_lookup_exact(const struct sock *sk,
								 struct netns_tcp_authopt *net,
								 struct tcp_authopt_key *ukey)
{
	struct tcp_authopt_key_info *key_info;

	hlist_for_each_entry_rcu(key_info, &net->head, node, lockdep_is_held(&net->mutex))
		if (tcp_authopt_key_match_exact(key_info, ukey))
			return key_info;

	return NULL;
}

static bool better_key_match(struct tcp_authopt_key_info *old, struct tcp_authopt_key_info *new)
{
	if (!old)
		return true;

	/* l3index always overrides non-l3index */
	if (old->l3index && new->l3index == 0)
		return false;
	if (old->l3index == 0 && new->l3index)
		return true;

	return false;
}

static struct tcp_authopt_key_info *tcp_authopt_lookup_send(struct netns_tcp_authopt *net,
							    const struct sock *addr_sk,
							    int send_id)
{
	struct tcp_authopt_key_info *result = NULL;
	struct tcp_authopt_key_info *key;
	int l3index = -1;

	hlist_for_each_entry_rcu(key, &net->head, node, 0) {
		if (send_id >= 0 && key->send_id != send_id)
			continue;
		if (key->flags & TCP_AUTHOPT_KEY_NOSEND)
			continue;
		if (key->flags & TCP_AUTHOPT_KEY_ADDR_BIND)
			if (!tcp_authopt_key_match_sk_addr(key, addr_sk))
				continue;
		if (key->flags & TCP_AUTHOPT_KEY_IFINDEX) {
			if (l3index < 0)
				l3index = l3mdev_master_ifindex_by_index(sock_net(addr_sk),
									 addr_sk->sk_bound_dev_if);
			if (l3index != key->l3index)
				continue;
		}
		if (better_key_match(result, key))
			result = key;
		else if (result)
			net_warn_ratelimited("ambiguous tcp authentication keys configured for send\n");
	}

	return result;
}

/**
 * __tcp_authopt_select_key - select key for sending
 *
 * @sk: socket
 * @info: socket's tcp_authopt_info
 * @addr_sk: socket used for address lookup. Same as sk except for synack case
 * @rnextkeyid: value of rnextkeyid caller should write in packet
 * @locked: If we're holding the socket lock. This is false for some timewait and reset cases
 *
 * Result is protected by RCU and can't be stored, it may only be passed to
 * tcp_authopt_hash and only under a single rcu_read_lock.
 */
struct tcp_authopt_key_info *__tcp_authopt_select_key(const struct sock *sk,
						      struct tcp_authopt_info *info,
						      const struct sock *addr_sk,
						      u8 *rnextkeyid,
						      bool locked)
{
	struct tcp_authopt_key_info *key, *new_key = NULL;
	struct netns_tcp_authopt *net = sock_net_tcp_authopt(sk);

	/* Listen sockets don't refer to any specific connection so we don't try
	 * to keep using the same key and ignore any received keyids.
	 */
	if (sk->sk_state == TCP_LISTEN) {
		int send_keyid = -1;

		if (info->flags & TCP_AUTHOPT_FLAG_LOCK_KEYID)
			send_keyid = info->send_keyid;
		key = tcp_authopt_lookup_send(net, addr_sk, send_keyid);
		if (key)
			*rnextkeyid = key->recv_id;

		return key;
	}

	if (locked) {
		sock_owned_by_me(sk);
		key = rcu_dereference_protected(info->send_key, lockdep_sock_is_held(sk));
		if (key && (key->flags & TCP_AUTHOPT_KEY_DEL) == TCP_AUTHOPT_KEY_DEL) {
			info->send_key = NULL;
			tcp_authopt_key_put(key);
			key = NULL;
		}
	} else {
		key = NULL;
	}

	/* Try to keep the same sending key unless user or peer requires a different key
	 * User request (via TCP_AUTHOPT_FLAG_LOCK_KEYID) always overrides peer request.
	 */
	if (info->flags & TCP_AUTHOPT_FLAG_LOCK_KEYID) {
		int send_keyid = info->send_keyid;

		if (!key || key->send_id != send_keyid)
			new_key = tcp_authopt_lookup_send(net, addr_sk, send_keyid);
	} else {
		if (!key || key->send_id != info->recv_rnextkeyid)
			new_key = tcp_authopt_lookup_send(net, addr_sk, info->recv_rnextkeyid);
	}
	/* If no key found with specific send_id try anything else. */
	if (!key && !new_key)
		new_key = tcp_authopt_lookup_send(net, addr_sk, -1);

	/* Update current key only if we hold the socket lock. */
	if (new_key && key != new_key) {
		if (locked) {
			if (kref_get_unless_zero(&new_key->ref)) {
				rcu_assign_pointer(info->send_key, new_key);
				tcp_authopt_key_put(key);
			}
			/* If key was deleted it's still valid until the end of
			 * the RCU grace period.
			 */
		}
		key = new_key;
	}

	if (key) {
		if (info->flags & TCP_AUTHOPT_FLAG_LOCK_RNEXTKEYID)
			*rnextkeyid = info->send_rnextkeyid;
		else
			*rnextkeyid = info->send_rnextkeyid = key->recv_id;
	}

	return key;
}
EXPORT_SYMBOL(__tcp_authopt_select_key);

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
	static_branch_inc(&tcp_authopt_needed_key);
	sk_gso_disable(sk);
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
static int _copy_from_sockptr_tolerant(u8 *dst,
				       unsigned int dstlen,
				       sockptr_t src,
				       unsigned int srclen)
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
		memset(dst + srclen, 0, dstlen - srclen);

	return err;
}

static int check_sysctl_tcp_authopt(void)
{
	if (!sysctl_tcp_authopt) {
		net_warn_ratelimited("TCP Authentication Option disabled by sysctl.\n");
		return -EPERM;
	}

	return 0;
}

int tcp_set_authopt(struct sock *sk, sockptr_t optval, unsigned int optlen)
{
	struct tcp_authopt opt;
	struct tcp_authopt_info *info;
	int err;

	sock_owned_by_me(sk);
	err = check_sysctl_tcp_authopt();
	if (err)
		return err;

	err = _copy_from_sockptr_tolerant((u8 *)&opt, sizeof(opt), optval, optlen);
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
	int err;

	memset(opt, 0, sizeof(*opt));
	sock_owned_by_me(sk);
	err = check_sysctl_tcp_authopt();
	if (err)
		return err;

	info = rcu_dereference_check(tp->authopt_info, lockdep_sock_is_held(sk));
	if (!info)
		return -ENOENT;

	opt->flags = info->flags & TCP_AUTHOPT_KNOWN_FLAGS;
	/* These keyids might be undefined, for example before connect.
	 * Reporting zero is not strictly correct because there are no reserved
	 * values.
	 */
	send_key = rcu_dereference_check(info->send_key, lockdep_sock_is_held(sk));
	if (send_key)
		opt->send_keyid = send_key->send_id;
	else
		opt->send_keyid = 0;
	opt->send_rnextkeyid = info->send_rnextkeyid;
	opt->recv_keyid = info->recv_keyid;
	opt->recv_rnextkeyid = info->recv_rnextkeyid;

	return 0;
}

#define TCP_AUTHOPT_KEY_KNOWN_FLAGS ( \
	TCP_AUTHOPT_KEY_DEL | \
	TCP_AUTHOPT_KEY_EXCLUDE_OPTS | \
	TCP_AUTHOPT_KEY_ADDR_BIND | \
	TCP_AUTHOPT_KEY_IFINDEX | \
	TCP_AUTHOPT_KEY_NOSEND | \
	TCP_AUTHOPT_KEY_NORECV)

int tcp_set_authopt_key(struct sock *sk, sockptr_t optval, unsigned int optlen)
{
	struct tcp_authopt_key opt;
	struct tcp_authopt_info *info;
	struct tcp_authopt_key_info *key_info, *old_key_info;
	struct netns_tcp_authopt *net = sock_net_tcp_authopt(sk);
	struct tcp_authopt_alg_imp *alg;
	int l3index = 0;
	int err;

	sock_owned_by_me(sk);
	err = check_sysctl_tcp_authopt();
	if (err)
		return err;
	if (!ns_capable(sock_net(sk)->user_ns, CAP_NET_ADMIN))
		return -EPERM;

	err = _copy_from_sockptr_tolerant((u8 *)&opt, sizeof(opt), optval, optlen);
	if (err)
		return err;

	if (opt.flags & ~TCP_AUTHOPT_KEY_KNOWN_FLAGS)
		return -EINVAL;

	if (opt.keylen > TCP_AUTHOPT_MAXKEYLEN)
		return -EINVAL;

	/* Delete is a special case: */
	if (opt.flags & TCP_AUTHOPT_KEY_DEL) {
		mutex_lock(&net->mutex);
		key_info = tcp_authopt_key_lookup_exact(sk, net, &opt);
		if (key_info) {
			tcp_authopt_key_del(net, key_info);
			err = 0;
		} else {
			err = -ENOENT;
		}
		mutex_unlock(&net->mutex);
		return err;
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

	/* check ifindex is valid (zero is always valid) */
	if (opt.flags & TCP_AUTHOPT_KEY_IFINDEX && opt.ifindex) {
		struct net_device *dev;

		rcu_read_lock();
		dev = dev_get_by_index_rcu(sock_net(sk), opt.ifindex);
		if (dev && netif_is_l3_master(dev))
			l3index = dev->ifindex;
		rcu_read_unlock();

		if (!l3index)
			return -EINVAL;
	}

	key_info = sock_kmalloc(sk, sizeof(*key_info), GFP_KERNEL | __GFP_ZERO);
	if (!key_info)
		return -ENOMEM;
	mutex_lock(&net->mutex);
	kref_init(&key_info->ref);
	/* If an old key exists with exact ID then remove and replace.
	 * RCU-protected readers might observe both and pick any.
	 */
	old_key_info = tcp_authopt_key_lookup_exact(sk, net, &opt);
	if (old_key_info)
		tcp_authopt_key_del(net, old_key_info);
	key_info->flags = opt.flags & TCP_AUTHOPT_KEY_KNOWN_FLAGS;
	key_info->send_id = opt.send_id;
	key_info->recv_id = opt.recv_id;
	key_info->alg_id = opt.alg;
	key_info->alg = alg;
	key_info->keylen = opt.keylen;
	memcpy(key_info->key, opt.key, opt.keylen);
	memcpy(&key_info->addr, &opt.addr, sizeof(key_info->addr));
	key_info->l3index = l3index;
	hlist_add_head_rcu(&key_info->node, &net->head);
	mutex_unlock(&net->mutex);

	return 0;
}

static int tcp_authopt_get_isn(struct sock *sk,
			       struct tcp_authopt_info *info,
			       struct sk_buff *skb,
			       int input,
			       __be32 *sisn,
			       __be32 *disn)
{
	struct tcphdr *th = tcp_hdr(skb);

	/* Special cases for SYN and SYN/ACK */
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

	if (sk->sk_state == TCP_NEW_SYN_RECV) {
		struct tcp_request_sock *rsk = (struct tcp_request_sock *)sk;

		if (WARN_ONCE(!input, "Caller passed wrong socket"))
			return -EINVAL;
		*sisn = htonl(rsk->rcv_isn);
		*disn = htonl(rsk->snt_isn);
		return 0;
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
		if (WARN_ONCE(!input, "Caller passed wrong socket"))
			return -EINVAL;
		*sisn = 0;
		*disn = 0;
		return 0;
	}
	if (WARN_ONCE(!info, "caller did not pass tcp_authopt_info\n"))
		return -EINVAL;
	/* Initial sequence numbers for ESTABLISHED connections from info */
	if (input) {
		*sisn = htonl(info->dst_isn);
		*disn = htonl(info->src_isn);
	} else {
		*sisn = htonl(info->src_isn);
		*disn = htonl(info->dst_isn);
	}
	return 0;
}

/* compute_sne - Calculate Sequence Number Extension
 *
 * Give old upper/lower 32bit values and a new lower 32bit value determine the
 * new value of the upper 32 bit. The new sequence number can be 2^31 before or
 * after prev_seq but TCP window scaling should limit this further.
 *
 * For correct accounting the stored SNE value should be only updated together
 * with the SEQ.
 */
static u32 compute_sne(u32 sne, u32 prev_seq, u32 seq)
{
	if (before(seq, prev_seq)) {
		if (seq > prev_seq)
			--sne;
	} else {
		if (seq < prev_seq)
			++sne;
	}

	return sne;
}

/* Update rcv_sne, must be called immediately before rcv_nxt update */
void __tcp_authopt_update_rcv_sne(struct tcp_sock *tp,
				  struct tcp_authopt_info *info, u32 seq)
{
	info->rcv_sne = compute_sne(info->rcv_sne, tp->rcv_nxt, seq);
}

/* Update snd_sne, must be called immediately before snd_nxt update */
void __tcp_authopt_update_snd_sne(struct tcp_sock *tp,
				  struct tcp_authopt_info *info, u32 seq)
{
	info->snd_sne = compute_sne(info->snd_sne, tp->snd_nxt, seq);
}

/* Compute SNE for a specific packet (by seq). */
static int compute_packet_sne(struct sock *sk, struct tcp_authopt_info *info,
			      u32 seq, bool input, __be32 *sne)
{
	u32 rcv_nxt, snd_nxt;

	// For TCP_NEW_SYN_RECV we have no tcp_authopt_info but tcp_request_sock holds ISN.
	if (sk->sk_state == TCP_NEW_SYN_RECV) {
		struct tcp_request_sock *rsk = tcp_rsk((struct request_sock *)sk);

		if (input)
			*sne = htonl(compute_sne(0, rsk->rcv_isn, seq));
		else
			*sne = htonl(compute_sne(0, rsk->snt_isn, seq));
		return 0;
	}

	/* TCP_LISTEN only receives SYN */
	if (sk->sk_state == TCP_LISTEN && input)
		return 0;

	/* TCP_SYN_SENT only sends SYN and receives SYN/ACK
	 * For the input case rcv_nxt is initialized after the packet is
	 * validated so tcp_sk(sk)->rcv_nxt is not initialized.
	 */
	if (sk->sk_state == TCP_SYN_SENT)
		return 0;

	if (sk->sk_state == TCP_TIME_WAIT) {
		rcv_nxt = tcp_twsk(sk)->tw_rcv_nxt;
		snd_nxt = tcp_twsk(sk)->tw_snd_nxt;
	} else {
		if (WARN_ONCE(!sk_fullsock(sk),
			      "unexpected minisock sk=%p state=%d", sk,
			      sk->sk_state))
			return -EINVAL;
		rcv_nxt = tcp_sk(sk)->rcv_nxt;
		snd_nxt = tcp_sk(sk)->snd_nxt;
	}

	if (WARN_ONCE(!info, "unexpected missing info for sk=%p sk_state=%d", sk, sk->sk_state))
		return -EINVAL;

	if (input)
		*sne = htonl(compute_sne(info->rcv_sne, rcv_nxt, seq));
	else
		*sne = htonl(compute_sne(info->snd_sne, snd_nxt, seq));

	return 0;
}

/* Feed one buffer into ahash
 * The buffer is assumed to be DMA-able
 */
static int crypto_ahash_buf(struct ahash_request *req, u8 *buf, uint len)
{
	struct scatterlist sg;

	sg_init_one(&sg, buf, len);
	ahash_request_set_crypt(req, &sg, NULL, len);

	return crypto_ahash_update(req);
}

/** Called to create accepted sockets.
 *
 *  Need to copy authopt info from listen socket.
 */
int __tcp_authopt_openreq(struct sock *newsk, const struct sock *oldsk, struct request_sock *req)
{
	struct tcp_authopt_info *old_info;
	struct tcp_authopt_info *new_info;

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
	/* Caller is tcp_create_openreq_child and already initializes snd_nxt/rcv_nxt */
	new_info->snd_sne = compute_sne(0, new_info->src_isn, tcp_sk(newsk)->snd_nxt);
	new_info->rcv_sne = compute_sne(0, new_info->dst_isn, tcp_sk(newsk)->rcv_nxt);
	sk_gso_disable(newsk);
	rcu_assign_pointer(tcp_sk(newsk)->authopt_info, new_info);

	return 0;
}

void __tcp_authopt_finish_connect(struct sock *sk, struct sk_buff *skb,
				  struct tcp_authopt_info *info)
{
	info->src_isn = ntohl(tcp_hdr(skb)->ack_seq) - 1;
	info->dst_isn = ntohl(tcp_hdr(skb)->seq);
	info->snd_sne = compute_sne(0, info->src_isn, tcp_sk(sk)->snd_nxt);
	info->rcv_sne = compute_sne(0, info->dst_isn, tcp_sk(sk)->rcv_nxt);
}

/* feed traffic key into ahash */
static int tcp_authopt_ahash_traffic_key(struct tcp_authopt_alg_pool *pool,
					 struct sock *sk,
					 struct sk_buff *skb,
					 struct tcp_authopt_info *info,
					 bool input,
					 bool ipv6)
{
	struct tcphdr *th = tcp_hdr(skb);
	int err;
	__be32 sisn, disn;
	__be16 digestbits = htons(crypto_ahash_digestsize(pool->tfm) * 8);
	/* For ahash const data buffers don't work so ensure header is on stack */
	char traffic_key_context_header[7] = "\x01TCP-AO";

	// RFC5926 section 3.1.1.1
	err = crypto_ahash_buf(pool->req, traffic_key_context_header, 7);
	if (err)
		return err;

	/* Addresses from packet on input and from sk_common on output
	 * This is because on output MAC is computed before prepending IP header
	 */
	if (input) {
		if (ipv6)
			err = crypto_ahash_buf(pool->req, (u8 *)&ipv6_hdr(skb)->saddr, 32);
		else
			err = crypto_ahash_buf(pool->req, (u8 *)&ip_hdr(skb)->saddr, 8);
		if (err)
			return err;
	} else {
		if (ipv6) {
#if IS_ENABLED(CONFIG_IPV6)
			err = crypto_ahash_buf(pool->req, (u8 *)&sk->sk_v6_rcv_saddr, 16);
			if (err)
				return err;
			err = crypto_ahash_buf(pool->req, (u8 *)&sk->sk_v6_daddr, 16);
			if (err)
				return err;
#else
			return -EINVAL;
#endif
		} else {
			err = crypto_ahash_buf(pool->req, (u8 *)&sk->sk_rcv_saddr, 4);
			if (err)
				return err;
			err = crypto_ahash_buf(pool->req, (u8 *)&sk->sk_daddr, 4);
			if (err)
				return err;
		}
	}

	/* TCP ports from header */
	err = crypto_ahash_buf(pool->req, (u8 *)&th->source, 4);
	if (err)
		return err;
	err = tcp_authopt_get_isn(sk, info, skb, input, &sisn, &disn);
	if (err)
		return err;
	err = crypto_ahash_buf(pool->req, (u8 *)&sisn, 4);
	if (err)
		return err;
	err = crypto_ahash_buf(pool->req, (u8 *)&disn, 4);
	if (err)
		return err;
	err = crypto_ahash_buf(pool->req, (u8 *)&digestbits, 2);
	if (err)
		return err;

	return 0;
}

/* Convert a variable-length key to a 16-byte fixed-length key for AES-CMAC
 * This is described in RFC5926 section 3.1.1.2
 */
static int aes_setkey_derived(struct crypto_ahash *tfm, struct ahash_request *req,
			      u8 *key, size_t keylen)
{
	static const u8 zeros[16] = {0};
	struct scatterlist sg;
	u8 derived_key[16];
	int err;

	if (WARN_ON_ONCE(crypto_ahash_digestsize(tfm) != sizeof(derived_key)))
		return -EINVAL;
	err = crypto_ahash_setkey(tfm, zeros, sizeof(zeros));
	if (err)
		return err;
	err = crypto_ahash_init(req);
	if (err)
		return err;
	sg_init_one(&sg, key, keylen);
	ahash_request_set_crypt(req, &sg, derived_key, keylen);
	err = crypto_ahash_digest(req);
	if (err)
		return err;
	return crypto_ahash_setkey(tfm, derived_key, sizeof(derived_key));
}

static int tcp_authopt_setkey(struct tcp_authopt_alg_pool *pool, struct tcp_authopt_key_info *key)
{
	if (key->alg_id == TCP_AUTHOPT_ALG_AES_128_CMAC_96 && key->keylen != 16)
		return aes_setkey_derived(pool->tfm, pool->req, key->key, key->keylen);
	else
		return crypto_ahash_setkey(pool->tfm, key->key, key->keylen);
}

static int tcp_authopt_get_traffic_key(struct sock *sk,
				       struct sk_buff *skb,
				       struct tcp_authopt_key_info *key,
				       struct tcp_authopt_info *info,
				       bool input,
				       bool ipv6,
				       u8 *traffic_key)
{
	struct tcp_authopt_alg_pool *pool;
	int err;

	pool = tcp_authopt_get_kdf_pool(key);
	if (IS_ERR(pool))
		return PTR_ERR(pool);

	err = tcp_authopt_setkey(pool, key);
	if (err)
		goto out;
	err = crypto_ahash_init(pool->req);
	if (err)
		goto out;

	err = tcp_authopt_ahash_traffic_key(pool, sk, skb, info, input, ipv6);
	if (err)
		goto out;

	ahash_request_set_crypt(pool->req, NULL, traffic_key, 0);
	err = crypto_ahash_final(pool->req);
	if (err)
		return err;

out:
	tcp_authopt_put_kdf_pool(key, pool);
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

static int tcp_v4_authopt_get_traffic_key_noskb(struct tcp_authopt_key_info *key,
						__be32 saddr,
						__be32 daddr,
						__be16 sport,
						__be16 dport,
						__be32 sisn,
						__be32 disn,
						u8 *traffic_key)
{
	int err;
	struct tcp_authopt_alg_pool *pool;
	struct tcp_v4_authopt_context_data data;
	char traffic_key_context_header[7] = "\x01TCP-AO";

	BUILD_BUG_ON(sizeof(data) != 22);

	pool = tcp_authopt_get_kdf_pool(key);
	if (IS_ERR(pool))
		return PTR_ERR(pool);

	err = tcp_authopt_setkey(pool, key);
	if (err)
		goto out;
	err = crypto_ahash_init(pool->req);
	if (err)
		goto out;

	// RFC5926 section 3.1.1.1
	// Separate to keep alignment semi-sane
	err = crypto_ahash_buf(pool->req, traffic_key_context_header, 7);
	if (err)
		return err;
	data.saddr = saddr;
	data.daddr = daddr;
	data.sport = sport;
	data.dport = dport;
	data.sisn = sisn;
	data.disn = disn;
	data.digestbits = htons(crypto_ahash_digestsize(pool->tfm) * 8);

	err = crypto_ahash_buf(pool->req, (u8 *)&data, sizeof(data));
	if (err)
		goto out;
	ahash_request_set_crypt(pool->req, NULL, traffic_key, 0);
	err = crypto_ahash_final(pool->req);
	if (err)
		goto out;

out:
	tcp_authopt_put_kdf_pool(key, pool);
	return err;
}

static int crypto_ahash_buf_zero(struct ahash_request *req, int len)
{
	u8 zeros[TCP_AUTHOPT_MACLEN] = {0};
	int buflen, err;

	/* In practice this is always called with len exactly 12.
	 * Even on input we drop unusual signature sizes early.
	 */
	while (len) {
		buflen = min_t(int, len, sizeof(zeros));
		err = crypto_ahash_buf(req, zeros, buflen);
		if (err)
			return err;
		len -= buflen;
	}

	return 0;
}

static int tcp_authopt_hash_tcp4_pseudoheader(struct tcp_authopt_alg_pool *pool,
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
	return crypto_ahash_buf(pool->req, (u8 *)&phdr, sizeof(phdr));
}

#if IS_ENABLED(CONFIG_IPV6)
static int tcp_authopt_hash_tcp6_pseudoheader(struct tcp_authopt_alg_pool *pool,
					      struct in6_addr *saddr,
					      struct in6_addr *daddr,
					      u32 plen)
{
	int err;
	__be32 buf[2];

	buf[0] = htonl(plen);
	buf[1] = htonl(IPPROTO_TCP);

	err = crypto_ahash_buf(pool->req, (u8 *)saddr, sizeof(*saddr));
	if (err)
		return err;
	err = crypto_ahash_buf(pool->req, (u8 *)daddr, sizeof(*daddr));
	if (err)
		return err;
	return crypto_ahash_buf(pool->req, (u8 *)&buf, sizeof(buf));
}
#endif

/** Hash tcphdr options.
 *
 * If include_options is false then only the TCPOPT_AUTHOPT option itself is hashed
 * Point to AO inside TH is passed by the caller
 */
static int tcp_authopt_hash_opts(struct tcp_authopt_alg_pool *pool,
				 struct tcphdr *th,
				 struct tcphdr_authopt *aoptr,
				 bool include_options)
{
	int err;
	/* start of options */
	u8 *tcp_opts = (u8 *)(th + 1);
	/* start of options */
	u8 *aobuf = (u8 *)aoptr;
	u8 aolen = aoptr->len;

	if (WARN_ONCE(aoptr->num != TCPOPT_AUTHOPT, "Bad aoptr\n"))
		return -EINVAL;

	if (include_options) {
		/* end of options */
		u8 *tcp_data = ((u8 *)th) + th->doff * 4;

		err = crypto_ahash_buf(pool->req, tcp_opts, aobuf - tcp_opts + 4);
		if (err)
			return err;
		err = crypto_ahash_buf_zero(pool->req, aolen - 4);
		if (err)
			return err;
		err = crypto_ahash_buf(pool->req, aobuf + aolen, tcp_data - (aobuf + aolen));
		if (err)
			return err;
	} else {
		err = crypto_ahash_buf(pool->req, aobuf, 4);
		if (err)
			return err;
		err = crypto_ahash_buf_zero(pool->req, aolen - 4);
		if (err)
			return err;
	}

	return 0;
}

static int tcp_authopt_hash_packet(struct tcp_authopt_alg_pool *pool,
				   struct sock *sk,
				   struct sk_buff *skb,
				   struct tcphdr_authopt *aoptr,
				   struct tcp_authopt_info *info,
				   bool input,
				   bool ipv6,
				   bool include_options,
				   u8 *macbuf)
{
	struct tcphdr *th = tcp_hdr(skb);
	__be32 sne = 0;
	int err;

	err = compute_packet_sne(sk, info, ntohl(th->seq), input, &sne);
	if (err)
		return err;

	err = crypto_ahash_init(pool->req);
	if (err)
		return err;

	err = crypto_ahash_buf(pool->req, (u8 *)&sne, 4);
	if (err)
		return err;

	if (ipv6) {
#if IS_ENABLED(CONFIG_IPV6)
		struct in6_addr *saddr;
		struct in6_addr *daddr;

		if (input) {
			saddr = &ipv6_hdr(skb)->saddr;
			daddr = &ipv6_hdr(skb)->daddr;
		} else {
			saddr = &sk->sk_v6_rcv_saddr;
			daddr = &sk->sk_v6_daddr;
		}
		err = tcp_authopt_hash_tcp6_pseudoheader(pool, saddr, daddr, skb->len);
		if (err)
			return err;
#else
		return -EINVAL;
#endif
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
		err = tcp_authopt_hash_tcp4_pseudoheader(pool, saddr, daddr, skb->len);
		if (err)
			return err;
	}

	// TCP header with checksum set to zero
	{
		struct tcphdr hashed_th = *th;

		hashed_th.check = 0;
		err = crypto_ahash_buf(pool->req, (u8 *)&hashed_th, sizeof(hashed_th));
		if (err)
			return err;
	}

	// TCP options
	err = tcp_authopt_hash_opts(pool, th, aoptr, include_options);
	if (err)
		return err;

	// Rest of SKB->data
	err = tcp_sig_hash_skb_data(pool->req, skb, th->doff << 2);
	if (err)
		return err;

	ahash_request_set_crypt(pool->req, NULL, macbuf, 0);
	return crypto_ahash_final(pool->req);
}

/* __tcp_authopt_calc_mac - Compute packet MAC using key
 *
 * The macbuf output buffer must be large enough to fit the digestsize of the
 * underlying transform before truncation.
 * This means TCP_AUTHOPT_MAXMACBUF, not TCP_AUTHOPT_MACLEN
 */
static int __tcp_authopt_calc_mac(struct sock *sk,
				  struct sk_buff *skb,
				  struct tcphdr_authopt *aoptr,
				  struct tcp_authopt_key_info *key,
				  struct tcp_authopt_info *info,
				  bool input,
				  char *macbuf)
{
	struct tcp_authopt_alg_pool *mac_pool;
	u8 traffic_key[TCP_AUTHOPT_MAX_TRAFFIC_KEY_LEN];
	int err;
	bool ipv6 = (sk->sk_family != AF_INET);

	if (sk->sk_family != AF_INET && sk->sk_family != AF_INET6)
		return -EINVAL;

	err = tcp_authopt_get_traffic_key(sk, skb, key, info, input, ipv6, traffic_key);
	if (err)
		return err;

	mac_pool = tcp_authopt_get_mac_pool(key);
	if (IS_ERR(mac_pool))
		return PTR_ERR(mac_pool);
	err = crypto_ahash_setkey(mac_pool->tfm, traffic_key, key->alg->traffic_key_len);
	if (err)
		goto out;
	err = crypto_ahash_init(mac_pool->req);
	if (err)
		return err;

	err = tcp_authopt_hash_packet(mac_pool,
				      sk,
				      skb,
				      aoptr,
				      info,
				      input,
				      ipv6,
				      !(key->flags & TCP_AUTHOPT_KEY_EXCLUDE_OPTS),
				      macbuf);

out:
	tcp_authopt_put_mac_pool(key, mac_pool);
	return err;
}

/* tcp_authopt_hash - fill in the mac
 *
 * The key must come from tcp_authopt_select_key.
 */
int tcp_authopt_hash(char *hash_location,
		     struct tcp_authopt_key_info *key,
		     struct tcp_authopt_info *info,
		     struct sock *sk,
		     struct sk_buff *skb)
{
	/* MAC inside option is truncated to 12 bytes but crypto API needs output
	 * buffer to be large enough so we use a buffer on the stack.
	 */
	u8 macbuf[TCP_AUTHOPT_MAXMACBUF];
	int err;
	struct tcphdr_authopt *aoptr = (struct tcphdr_authopt *)(hash_location - 4);

	err = __tcp_authopt_calc_mac(sk, skb, aoptr, key, info, false, macbuf);
	if (err)
		goto fail;
	memcpy(hash_location, macbuf, TCP_AUTHOPT_MACLEN);

	return 0;

fail:
	/* If mac calculation fails and caller doesn't handle the error
	 * try to make it obvious inside the packet.
	 */
	memset(hash_location, 0, TCP_AUTHOPT_MACLEN);
	return err;
}
EXPORT_SYMBOL(tcp_authopt_hash);

/**
 * tcp_v4_authopt_hash_reply - Hash tcp+ipv4 header without SKB
 *
 * @hash_location: output buffer
 * @info: sending socket's tcp_authopt_info
 * @key: signing key, from tcp_authopt_select_key.
 * @saddr: source address
 * @daddr: destination address
 * @th: Pointer to TCP header and options
 */
int tcp_v4_authopt_hash_reply(char *hash_location,
			      struct tcp_authopt_info *info,
			      struct tcp_authopt_key_info *key,
			      __be32 saddr,
			      __be32 daddr,
			      struct tcphdr *th)
{
	struct tcp_authopt_alg_pool *pool;
	u8 macbuf[TCP_AUTHOPT_MAXMACBUF];
	u8 traffic_key[TCP_AUTHOPT_MAX_TRAFFIC_KEY_LEN];
	__be32 sne = 0;
	int err;

	/* Call special code path for computing traffic key without skb
	 * This can be called from tcp_v4_reqsk_send_ack so caching would be
	 * difficult here.
	 */
	err = tcp_v4_authopt_get_traffic_key_noskb(key, saddr, daddr,
						   th->source, th->dest,
						   htonl(info->src_isn), htonl(info->dst_isn),
						   traffic_key);
	if (err)
		goto out_err_traffic_key;

	/* Init mac shash */
	pool = tcp_authopt_get_mac_pool(key);
	if (IS_ERR(pool))
		return PTR_ERR(pool);
	err = crypto_ahash_setkey(pool->tfm, traffic_key, key->alg->traffic_key_len);
	if (err)
		goto out_err;
	err = crypto_ahash_init(pool->req);
	if (err)
		return err;

	err = crypto_ahash_buf(pool->req, (u8 *)&sne, 4);
	if (err)
		return err;

	err = tcp_authopt_hash_tcp4_pseudoheader(pool, saddr, daddr, th->doff * 4);
	if (err)
		return err;

	// TCP header with checksum set to zero. Caller ensures this.
	if (WARN_ON_ONCE(th->check != 0))
		goto out_err;
	err = crypto_ahash_buf(pool->req, (u8 *)th, sizeof(*th));
	if (err)
		goto out_err;

	// TCP options
	err = tcp_authopt_hash_opts(pool, th, (struct tcphdr_authopt *)(hash_location - 4),
				    !(key->flags & TCP_AUTHOPT_KEY_EXCLUDE_OPTS));
	if (err)
		goto out_err;

	ahash_request_set_crypt(pool->req, NULL, macbuf, 0);
	err = crypto_ahash_final(pool->req);
	if (err)
		goto out_err;
	memcpy(hash_location, macbuf, TCP_AUTHOPT_MACLEN);

	tcp_authopt_put_mac_pool(key, pool);
	return 0;

out_err:
	tcp_authopt_put_mac_pool(key, pool);
out_err_traffic_key:
	memset(hash_location, 0, TCP_AUTHOPT_MACLEN);
	return err;
}

/*
 * tcp_authopt_lookup_recv - lookup key for receive
 *
 * @sk: Receive socket
 * @skb: Packet, used to compare addr and iface
 * @net: Per-namespace information containing keys
 * @recv_id: Optional recv_id. If >= 0 then only return keys that match
 * @anykey: Set to true if any keys are present for the peer
 *
 * If anykey is false then authentication is not expected from the peer so we
 * should based on TCP_AUTHOPT_FLAG_REJECT_UNEXPECTED
 *
 * If anykey is true then a valid key is required.
 */
static struct tcp_authopt_key_info *tcp_authopt_lookup_recv(struct sock *sk,
							    struct sk_buff *skb,
							    struct netns_tcp_authopt *net,
							    int recv_id,
							    bool *anykey)
{
	struct tcp_authopt_key_info *result = NULL;
	struct tcp_authopt_key_info *key;
	int l3index = -1;

	*anykey = false;
	/* multiple matches will cause occasional failures */
	hlist_for_each_entry_rcu(key, &net->head, node, 0) {
		if (key->flags & TCP_AUTHOPT_KEY_ADDR_BIND &&
		    !tcp_authopt_key_match_skb_addr(key, skb))
			continue;
		if (key->flags & TCP_AUTHOPT_KEY_IFINDEX) {
			if (l3index < 0) {
				if (skb->protocol == htons(ETH_P_IP)) {
					l3index = inet_sdif(skb) ? inet_iif(skb) : 0;
				} else if (skb->protocol == htons(ETH_P_IPV6)) {
					l3index = inet6_sdif(skb) ? inet6_iif(skb) : 0;
				} else {
					WARN_ONCE(1, "unexpected skb->protocol=%x", skb->protocol);
					continue;
				}
			}

			if (l3index != key->l3index)
				continue;
		}
		*anykey = true;
		// If only keys with norecv flag are present still consider that
		if (key->flags & TCP_AUTHOPT_KEY_NORECV)
			continue;
		if (recv_id >= 0 && key->recv_id != recv_id)
			continue;
		if (better_key_match(result, key))
			result = key;
		else if (result)
			net_warn_ratelimited("ambiguous tcp authentication keys configured for recv\n");
	}

	return result;
}

/* Show a rate-limited message for authentication fail */
static void print_tcpao_notice(const char *msg, struct sk_buff *skb)
{
	struct iphdr *iph = (struct iphdr *)skb_network_header(skb);
	struct tcphdr *th = (struct tcphdr *)skb_transport_header(skb);

	if (iph->version == 4) {
		net_info_ratelimited("%s (%pI4, %d)->(%pI4, %d)\n", msg,
				     &iph->saddr, ntohs(th->source),
				     &iph->daddr, ntohs(th->dest));
	} else if (iph->version == 6) {
		struct ipv6hdr *ip6h = (struct ipv6hdr *)skb_network_header(skb);

		net_info_ratelimited("%s (%pI6, %d)->(%pI6, %d)\n", msg,
				     &ip6h->saddr, ntohs(th->source),
				     &ip6h->daddr, ntohs(th->dest));
	} else {
		WARN_ONCE(1, "%s unknown IP version\n", msg);
	}
}

int __tcp_authopt_inbound_check(struct sock *sk, struct sk_buff *skb,
				struct tcp_authopt_info *info, const u8 *_opt)
{
	struct netns_tcp_authopt *net = sock_net_tcp_authopt(sk);
	struct tcphdr_authopt *opt = (struct tcphdr_authopt *)_opt;
	struct tcp_authopt_key_info *key;
	bool anykey;
	u8 macbuf[TCP_AUTHOPT_MAXMACBUF];
	int err;

	key = tcp_authopt_lookup_recv(sk, skb, net, opt ? opt->keyid : -1, &anykey);

	/* nothing found or expected */
	if (!opt && !anykey)
		return 0;
	if (!opt && anykey) {
		NET_INC_STATS(sock_net(sk), LINUX_MIB_TCPAUTHOPTFAILURE);
		print_tcpao_notice("TCP Authentication Missing", skb);
		return -EINVAL;
	}
	if (opt && !anykey) {
		/* RFC5925 Section 7.3:
		 * A TCP-AO implementation MUST allow for configuration of the behavior
		 * of segments with TCP-AO but that do not match an MKT. The initial
		 * default of this configuration SHOULD be to silently accept such
		 * connections.
		 */
		if (info->flags & TCP_AUTHOPT_FLAG_REJECT_UNEXPECTED) {
			NET_INC_STATS(sock_net(sk), LINUX_MIB_TCPAUTHOPTFAILURE);
			print_tcpao_notice("TCP Authentication Unexpected: Rejected", skb);
			return -EINVAL;
		}
		print_tcpao_notice("TCP Authentication Unexpected: Accepted", skb);
		goto accept;
	}
	if (opt && !key) {
		/* Keys are configured for peer but with different keyid than packet */
		NET_INC_STATS(sock_net(sk), LINUX_MIB_TCPAUTHOPTFAILURE);
		print_tcpao_notice("TCP Authentication Failed", skb);
		return -EINVAL;
	}

	/* bad inbound key len */
	if (opt->len != TCPOLEN_AUTHOPT_OUTPUT)
		return -EINVAL;

	err = __tcp_authopt_calc_mac(sk, skb, opt, key, info, true, macbuf);
	if (err)
		return err;

	if (memcmp(macbuf, opt->mac, TCP_AUTHOPT_MACLEN)) {
		NET_INC_STATS(sock_net(sk), LINUX_MIB_TCPAUTHOPTFAILURE);
		print_tcpao_notice("TCP Authentication Failed", skb);
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
EXPORT_SYMBOL(__tcp_authopt_inbound_check);

static int tcp_authopt_init_net(struct net *full_net)
{
	struct netns_tcp_authopt *net = &full_net->tcp_authopt;

	mutex_init(&net->mutex);
	INIT_HLIST_HEAD(&net->head);

	return 0;
}

static void tcp_authopt_exit_net(struct net *full_net)
{
	struct netns_tcp_authopt *net = &full_net->tcp_authopt;
	struct tcp_authopt_key_info *key;
	struct hlist_node *n;

	mutex_lock(&net->mutex);

	hlist_for_each_entry_safe(key, n, &net->head, node) {
		hlist_del_rcu(&key->node);
		tcp_authopt_key_put(key);
	}

	mutex_unlock(&net->mutex);
}

static struct pernet_operations net_ops = {
	.init = tcp_authopt_init_net,
	.exit = tcp_authopt_exit_net,
};

static int __init tcp_authopt_init(void)
{
	return register_pernet_subsys(&net_ops);
}
late_initcall(tcp_authopt_init);
