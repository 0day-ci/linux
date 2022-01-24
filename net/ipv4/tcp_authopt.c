// SPDX-License-Identifier: GPL-2.0-or-later

#include <net/tcp_authopt.h>
#include <net/ipv6.h>
#include <net/tcp.h>
#include <linux/kref.h>
#include <crypto/hash.h>

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
	if ((info->flags & TCP_AUTHOPT_KEY_ADDR_BIND) != (key->flags & TCP_AUTHOPT_KEY_ADDR_BIND))
		return false;
	if (info->flags & TCP_AUTHOPT_KEY_ADDR_BIND)
		if (!ipvx_addr_match(&info->addr, &key->addr))
			return false;

	return true;
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

	sk_gso_disable(sk);
	rcu_assign_pointer(tp->authopt_info, info);

	return info;
}

#define TCP_AUTHOPT_KNOWN_FLAGS ( \
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

int tcp_set_authopt(struct sock *sk, sockptr_t optval, unsigned int optlen)
{
	struct tcp_authopt opt;
	struct tcp_authopt_info *info;
	int err;

	sock_owned_by_me(sk);

	err = _copy_from_sockptr_tolerant((u8 *)&opt, sizeof(opt), optval, optlen);
	if (err)
		return err;

	if (opt.flags & ~TCP_AUTHOPT_KNOWN_FLAGS)
		return -EINVAL;

	info = __tcp_authopt_info_get_or_create(sk);
	if (IS_ERR(info))
		return PTR_ERR(info);

	info->flags = opt.flags & TCP_AUTHOPT_KNOWN_FLAGS;

	return 0;
}

int tcp_get_authopt_val(struct sock *sk, struct tcp_authopt *opt)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct tcp_authopt_info *info;

	sock_owned_by_me(sk);

	memset(opt, 0, sizeof(*opt));
	info = rcu_dereference_check(tp->authopt_info, lockdep_sock_is_held(sk));
	if (!info)
		return -ENOENT;

	opt->flags = info->flags & TCP_AUTHOPT_KNOWN_FLAGS;

	return 0;
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
	struct netns_tcp_authopt *net = sock_net_tcp_authopt(sk);
	struct tcp_authopt_alg_imp *alg;
	int err;

	sock_owned_by_me(sk);
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
	hlist_add_head_rcu(&key_info->node, &net->head);
	mutex_unlock(&net->mutex);

	return 0;
}

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
