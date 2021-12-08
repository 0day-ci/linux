// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/kernel.h>
#include <net/tcp.h>
#include <net/tcp_authopt.h>
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
	ahash_request_free(pool->req);
	pool->req = NULL;
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

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return ERR_PTR(-ENOMEM);

	sk_gso_disable(sk);
	INIT_HLIST_HEAD(&info->head);
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

/* Free key nicely, for living sockets */
static void tcp_authopt_key_del(struct sock *sk,
				struct tcp_authopt_info *info,
				struct tcp_authopt_key_info *key)
{
	sock_owned_by_me(sk);
	hlist_del_rcu(&key->node);
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

	err = _copy_from_sockptr_tolerant((u8 *)&opt, sizeof(opt), optval, optlen);
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
	old_key_info = tcp_authopt_key_lookup_exact(sk, info, &opt);
	if (old_key_info)
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

	// RFC5926 section 3.1.1.1
	err = crypto_ahash_buf(pool->req, "\x01TCP-AO", 7);
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
			err = crypto_ahash_buf(pool->req, (u8 *)&sk->sk_v6_rcv_saddr, 16);
			if (err)
				return err;
			err = crypto_ahash_buf(pool->req, (u8 *)&sk->sk_v6_daddr, 16);
			if (err)
				return err;
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
				   bool input,
				   bool ipv6,
				   bool include_options,
				   u8 *macbuf)
{
	struct tcphdr *th = tcp_hdr(skb);
	int err;

	/* NOTE: SNE unimplemented */
	__be32 sne = 0;

	err = crypto_ahash_init(pool->req);
	if (err)
		return err;

	err = crypto_ahash_buf(pool->req, (u8 *)&sne, 4);
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
		err = tcp_authopt_hash_tcp6_pseudoheader(pool, saddr, daddr, skb->len);
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
				      input,
				      ipv6,
				      !(key->flags & TCP_AUTHOPT_KEY_EXCLUDE_OPTS),
				      macbuf);

out:
	tcp_authopt_put_mac_pool(key, mac_pool);
	return err;
}
