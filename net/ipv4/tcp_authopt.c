// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/kernel.h>
#include <net/tcp.h>
#include <net/tcp_authopt.h>
#include <crypto/hash.h>
#include <trace/events/tcp.h>

/* All current algorithms have a mac length of 12 but crypto API digestsize can be larger */
#define TCP_AUTHOPT_MAXMACBUF	20
#define TCP_AUTHOPT_MAX_TRAFFIC_KEY_LEN	20

struct tcp_authopt_alg_imp {
	/* Name of algorithm in crypto-api */
	const char *alg_name;
	/* One of the TCP_AUTHOPT_ALG_* constants from uapi */
	u8 alg_id;
	/* Length of traffic key */
	u8 traffic_key_len;
	/* Length of mac in TCP option */
	u8 maclen;

	/* shared crypto_shash */
	spinlock_t lock;
	int ref_cnt;
	struct crypto_shash *tfm;
};

static struct tcp_authopt_alg_imp tcp_authopt_alg_list[] = {
	{
		.alg_id = TCP_AUTHOPT_ALG_HMAC_SHA_1_96,
		.alg_name = "hmac(sha1)",
		.traffic_key_len = 20,
		.maclen = 12,
		.lock = __SPIN_LOCK_UNLOCKED(tcp_authopt_alg_list[0].lock),
	},
	{
		.alg_id = TCP_AUTHOPT_ALG_AES_128_CMAC_96,
		.alg_name = "cmac(aes)",
		.traffic_key_len = 16,
		.maclen = 12,
		.lock = __SPIN_LOCK_UNLOCKED(tcp_authopt_alg_list[1].lock),
	},
};

/* get a pointer to the tcp_authopt_alg instance or NULL if id invalid */
static inline struct tcp_authopt_alg_imp *tcp_authopt_alg_get(int alg_num)
{
	if (alg_num <= 0 || alg_num > 2)
		return NULL;
	return &tcp_authopt_alg_list[alg_num - 1];
}

/* Mark an algorithm as in-use from user context */
static int tcp_authopt_alg_require(struct tcp_authopt_alg_imp *alg)
{
	struct crypto_shash *tfm = NULL;
	bool need_init = false;

	might_sleep();

	/* If we're the first user then we need to initialize shash but we might lose the race. */
	spin_lock_bh(&alg->lock);
	WARN_ON(alg->ref_cnt < 0);
	if (alg->ref_cnt == 0)
		need_init = true;
	else
		++alg->ref_cnt;
	spin_unlock_bh(&alg->lock);

	/* Already initialized */
	if (!need_init)
		return 0;

	tfm = crypto_alloc_shash(alg->alg_name, 0, 0);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	spin_lock_bh(&alg->lock);
	if (alg->ref_cnt == 0)
		/* race won */
		alg->tfm = tfm;
	else
		/* race lost, free tfm later */
		need_init = false;
	++alg->ref_cnt;
	spin_unlock_bh(&alg->lock);

	if (!need_init)
		crypto_free_shash(tfm);
	else
		pr_info("initialized tcp-ao %s", alg->alg_name);

	return 0;
}

static void tcp_authopt_alg_release(struct tcp_authopt_alg_imp *alg)
{
	struct crypto_shash *tfm_to_free = NULL;

	spin_lock_bh(&alg->lock);
	--alg->ref_cnt;
	WARN_ON(alg->ref_cnt < 0);
	if (alg->ref_cnt == 0) {
		tfm_to_free = alg->tfm;
		alg->tfm = NULL;
	}
	spin_unlock_bh(&alg->lock);

	if (tfm_to_free) {
		pr_info("released tcp-ao %s", alg->alg_name);
		crypto_free_shash(tfm_to_free);
	}
}

/* increase reference count on an algorithm that is already in use */
static void tcp_authopt_alg_incref(struct tcp_authopt_alg_imp *alg)
{
	spin_lock_bh(&alg->lock);
	WARN_ON(alg->ref_cnt <= 0);
	++alg->ref_cnt;
	spin_unlock_bh(&alg->lock);
}

static struct crypto_shash *tcp_authopt_alg_get_tfm(struct tcp_authopt_alg_imp *alg)
{
	spin_lock_bh(&alg->lock);
	WARN_ON(alg->ref_cnt < 0);
	return alg->tfm;
}

static void tcp_authopt_alg_put_tfm(struct tcp_authopt_alg_imp *alg, struct crypto_shash *tfm)
{
	WARN_ON(tfm != alg->tfm);
	spin_unlock_bh(&alg->lock);
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
	if (a1->ss_family == AF_INET && memcmp(
			&((struct sockaddr_in *)a1)->sin_addr,
			&((struct sockaddr_in *)a2)->sin_addr,
			sizeof(struct in_addr)))
		return false;
	if (a1->ss_family == AF_INET6 && memcmp(
			&((struct sockaddr_in6 *)a1)->sin6_addr,
			&((struct sockaddr_in6 *)a2)->sin6_addr,
			sizeof(struct in6_addr)))
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

static struct tcp_authopt_info *__tcp_authopt_info_get_or_create(struct sock *sk)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct tcp_authopt_info *info;

	info = rcu_dereference_check(tp->authopt_info, lockdep_sock_is_held(sk));
	if (info)
		return info;

	info = kmalloc(sizeof(*info), GFP_KERNEL | __GFP_ZERO);
	if (!info)
		return ERR_PTR(-ENOMEM);

	sk_nocaps_add(sk, NETIF_F_GSO_MASK);
	INIT_HLIST_HEAD(&info->head);
	rcu_assign_pointer(tp->authopt_info, info);

	return info;
}

#define TCP_AUTHOPT_KNOWN_FLAGS ( \
	TCP_AUTHOPT_FLAG_REJECT_UNEXPECTED)

int tcp_set_authopt(struct sock *sk, sockptr_t optval, unsigned int optlen)
{
	struct tcp_authopt opt;
	struct tcp_authopt_info *info;

	sock_owned_by_me(sk);

	/* If userspace optlen is too short fill the rest with zeros */
	if (optlen > sizeof(opt))
		return -EINVAL;
	memset(&opt, 0, sizeof(opt));
	if (copy_from_sockptr(&opt, optval, optlen))
		return -EFAULT;

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
		return -EINVAL;

	opt->flags = info->flags & TCP_AUTHOPT_KNOWN_FLAGS;

	return 0;
}

static void tcp_authopt_key_free_rcu(struct rcu_head *rcu)
{
	struct tcp_authopt_key_info *key = container_of(rcu, struct tcp_authopt_key_info, rcu);

	tcp_authopt_alg_release(key->alg);
	kfree(key);
}

static void tcp_authopt_key_del(struct sock *sk,
				struct tcp_authopt_info *info,
				struct tcp_authopt_key_info *key)
{
	hlist_del_rcu(&key->node);
	atomic_sub(sizeof(*key), &sk->sk_omem_alloc);
	call_rcu(&key->rcu, tcp_authopt_key_free_rcu);
}

/* free info and keys but don't touch tp->authopt_info */
static void __tcp_authopt_info_free(struct sock *sk, struct tcp_authopt_info *info)
{
	struct hlist_node *n;
	struct tcp_authopt_key_info *key;

	hlist_for_each_entry_safe(key, n, &info->head, node)
		tcp_authopt_key_del(sk, info, key);
	kfree_rcu(info, rcu);
}

/* free everything and clear tcp_sock.authopt_info to NULL */
void tcp_authopt_clear(struct sock *sk)
{
	struct tcp_authopt_info *info;

	info = rcu_dereference_protected(tcp_sk(sk)->authopt_info, lockdep_sock_is_held(sk));
	if (info) {
		__tcp_authopt_info_free(sk, info);
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
	struct tcp_authopt_key_info *key_info;
	struct tcp_authopt_alg_imp *alg;
	int err;

	sock_owned_by_me(sk);

	/* If userspace optlen is too short fill the rest with zeros */
	if (optlen > sizeof(opt))
		return -EINVAL;
	memset(&opt, 0, sizeof(opt));
	if (copy_from_sockptr(&opt, optval, optlen))
		return -EFAULT;

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
	WARN_ON(alg->alg_id != opt.alg);
	err = tcp_authopt_alg_require(alg);
	if (err)
		return err;

	/* If an old key exists with exact ID then remove and replace.
	 * RCU-protected readers might observe both and pick any.
	 */
	key_info = tcp_authopt_key_lookup_exact(sk, info, &opt);
	if (key_info)
		tcp_authopt_key_del(sk, info, key_info);
	key_info = sock_kmalloc(sk, sizeof(*key_info), GFP_KERNEL | __GFP_ZERO);
	if (!key_info) {
		tcp_authopt_alg_release(alg);
		return -ENOMEM;
	}
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
