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

struct tcp_authopt_key_info *tcp_authopt_lookup_send(struct tcp_authopt_info *info,
						     const struct sock *addr_sk,
						     int send_id)
{
	struct tcp_authopt_key_info *result = NULL;
	struct tcp_authopt_key_info *key;

	hlist_for_each_entry_rcu(key, &info->head, node, 0) {
		if (send_id >= 0 && key->send_id != send_id)
			continue;
		if (key->flags & TCP_AUTHOPT_KEY_ADDR_BIND) {
			if (addr_sk->sk_family == AF_INET) {
				struct sockaddr_in *key_addr = (struct sockaddr_in *)&key->addr;
				const struct in_addr *daddr =
					(const struct in_addr *)&addr_sk->sk_daddr;

				if (WARN_ON(key_addr->sin_family != AF_INET))
					continue;
				if (memcmp(daddr, &key_addr->sin_addr, sizeof(*daddr)))
					continue;
			}
			if (addr_sk->sk_family == AF_INET6) {
				struct sockaddr_in6 *key_addr = (struct sockaddr_in6 *)&key->addr;
				const struct in6_addr *daddr = &addr_sk->sk_v6_daddr;

				if (WARN_ON(key_addr->sin6_family != AF_INET6))
					continue;
				if (memcmp(daddr, &key_addr->sin6_addr, sizeof(*daddr)))
					continue;
			}
		}
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
 */
struct tcp_authopt_key_info *tcp_authopt_select_key(const struct sock *sk,
						    const struct sock *addr_sk,
						    u8 *rnextkeyid)
{
	struct tcp_authopt_info *info;

	info = rcu_dereference(tcp_sk(sk)->authopt_info);
	if (!info)
		return NULL;

	return tcp_authopt_lookup_send(info, addr_sk, -1);
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
	key_info->maclen = alg->maclen;
	memcpy(&key_info->addr, &opt.addr, sizeof(key_info->addr));
	hlist_add_head_rcu(&key_info->node, &info->head);

	return 0;
}

static int tcp_authopt_clone_keys(struct sock *newsk,
				  const struct sock *oldsk,
				  struct tcp_authopt_info *new_info,
				  struct tcp_authopt_info *old_info)
{
	struct tcp_authopt_key_info *old_key;
	struct tcp_authopt_key_info *new_key;

	hlist_for_each_entry_rcu(old_key, &old_info->head, node, lockdep_sock_is_held(sk)) {
		new_key = sock_kmalloc(newsk, sizeof(*new_key), GFP_ATOMIC);
		if (!new_key)
			return -ENOMEM;
		memcpy(new_key, old_key, sizeof(*new_key));
		tcp_authopt_alg_incref(old_key->alg);
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

	new_info = kmalloc(sizeof(*new_info), GFP_ATOMIC | __GFP_ZERO);
	if (!new_info)
		return -ENOMEM;

	sk_nocaps_add(newsk, NETIF_F_GSO_MASK);
	new_info->src_isn = tcp_rsk(req)->snt_isn;
	new_info->dst_isn = tcp_rsk(req)->rcv_isn;
	INIT_HLIST_HEAD(&new_info->head);
	err = tcp_authopt_clone_keys(newsk, oldsk, new_info, old_info);
	if (err) {
		__tcp_authopt_info_free(newsk, new_info);
		return err;
	}
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

	/* Addresses from packet on input and from socket on output
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
			struct in6_addr *saddr;
			struct in6_addr *daddr;

			saddr = &sk->sk_v6_rcv_saddr;
			daddr = &sk->sk_v6_daddr;
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

	/* special cases for SYN and SYN/ACK */
	if (th->syn && !th->ack) {
		sisn = th->seq;
		disn = 0;
	} else if (th->syn && th->ack) {
		sisn = th->seq;
		disn = htonl(ntohl(th->ack_seq) - 1);
	} else {
		struct tcp_authopt_info *authopt_info;

		/* Fetching authopt_info like this means it's possible that authopt_info
		 * was deleted while we were hashing. If that happens we drop the packet
		 * which should be fine.
		 *
		 * A better solution might be to always pass info as a parameter, or
		 * compute traffic_key for established sockets separately.
		 */
		rcu_read_lock();
		authopt_info = rcu_dereference(tcp_sk(sk)->authopt_info);
		if (!authopt_info) {
			rcu_read_unlock();
			return -EINVAL;
		}
		/* Initial sequence numbers for ESTABLISHED connections from info */
		if (input) {
			sisn = htonl(authopt_info->dst_isn);
			disn = htonl(authopt_info->src_isn);
		} else {
			sisn = htonl(authopt_info->src_isn);
			disn = htonl(authopt_info->dst_isn);
		}
		rcu_read_unlock();
	}

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

	if (WARN_ON(crypto_shash_digestsize(tfm) != 16))
		return -EINVAL;
	err = crypto_shash_setkey(tfm, zeros, sizeof(zeros));
	if (err)
		return err;
	err = crypto_shash_tfm_digest(tfm, key, keylen, derived_key);
	if (err)
		return err;
	return crypto_shash_setkey(tfm, derived_key, sizeof(derived_key));
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
	if (WARN_ON(crypto_shash_digestsize(kdf_tfm) != key->alg->traffic_key_len)) {
		err = -EINVAL;
		goto out;
	}

	if (key->alg_id == TCP_AUTHOPT_ALG_AES_128_CMAC_96 && key->keylen != 16) {
		err = aes_setkey_derived(kdf_tfm, key->key, key->keylen);
		if (err)
			goto out;
	} else {
		err = crypto_shash_setkey(kdf_tfm, key->key, key->keylen);
		if (err)
			goto out;
	}

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
	//printk("traffic_key: %*phN\n", 20, traffic_key);

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
	u32 buf[2];

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
 * 			underlying transform before truncation. Please use TCP_AUTHOPT_MAXMACBUF
 */
int __tcp_authopt_calc_mac(struct sock *sk,
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
	if (WARN_ON(key->alg->traffic_key_len > sizeof(traffic_key)))
		return -ENOBUFS;

	err = tcp_authopt_get_traffic_key(sk, skb, key, input, ipv6, traffic_key);
	if (err)
		return err;

	mac_tfm = tcp_authopt_get_mac_shash(key);
	if (IS_ERR(mac_tfm))
		return PTR_ERR(mac_tfm);
	if (crypto_shash_digestsize(mac_tfm) > TCP_AUTHOPT_MAXMACBUF) {
		err = -EINVAL;
		goto out;
	}
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
	//printk("mac: %*phN\n", key->maclen, macbuf);

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

	if (WARN_ON(key->maclen > sizeof(macbuf)))
		return -ENOBUFS;

	err = __tcp_authopt_calc_mac(sk, skb, key, false, macbuf);
	if (err) {
		/* If mac calculation fails and caller doesn't handle the error
		 * try to make it obvious inside the packet.
		 */
		memset(hash_location, 0, key->maclen);
		return err;
	}
	memcpy(hash_location, macbuf, key->maclen);

	return 0;
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
		if (key->flags & TCP_AUTHOPT_KEY_ADDR_BIND) {
			if (sk->sk_family == AF_INET) {
				struct sockaddr_in *key_addr = (struct sockaddr_in *)&key->addr;
				struct iphdr *iph = (struct iphdr *)skb_network_header(skb);

				if (WARN_ON(key_addr->sin_family != AF_INET))
					continue;
				if (WARN_ON(iph->version != 4))
					continue;
				if (memcmp(&iph->saddr, &key_addr->sin_addr, sizeof(iph->saddr)))
					continue;
			}
			if (sk->sk_family == AF_INET6) {
				struct sockaddr_in6 *key_addr = (struct sockaddr_in6 *)&key->addr;
				struct ipv6hdr *iph = (struct ipv6hdr *)skb_network_header(skb);

				if (WARN_ON(key_addr->sin6_family != AF_INET6))
					continue;
				if (WARN_ON(iph->version != 6))
					continue;
				if (memcmp(&iph->saddr, &key_addr->sin6_addr, sizeof(iph->saddr)))
					continue;
			}
		}
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
	key = tcp_authopt_lookup_recv(sk, skb, info, opt ? opt->keyid : -1);

	/* nothing found or expected */
	if (!opt && !key)
		return 0;
	if (!opt && key) {
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
			net_info_ratelimited("TCP Authentication Unexpected: Rejected\n");
			return -EINVAL;
		} else {
			net_info_ratelimited("TCP Authentication Unexpected: Accepted\n");
			return 0;
		}
	}

	/* bad inbound key len */
	if (key->maclen + 4 != opt->len)
		return -EINVAL;

	err = __tcp_authopt_calc_mac(sk, skb, key, true, macbuf);
	if (err)
		return err;

	if (memcmp(macbuf, opt->mac, key->maclen)) {
		net_info_ratelimited("TCP Authentication Failed\n");
		return -EINVAL;
	}

	return 0;
}
