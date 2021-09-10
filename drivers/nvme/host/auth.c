// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Hannes Reinecke, SUSE Linux
 */

#include <linux/crc32.h>
#include <linux/base64.h>
#include <asm/unaligned.h>
#include <crypto/hash.h>
#include <crypto/dh.h>
#include <crypto/ffdhe.h>
#include "nvme.h"
#include "fabrics.h"
#include "auth.h"

static u32 nvme_dhchap_seqnum;

struct nvme_dhchap_queue_context {
	struct list_head entry;
	struct work_struct auth_work;
	struct nvme_ctrl *ctrl;
	struct crypto_shash *shash_tfm;
	struct crypto_kpp *dh_tfm;
	void *buf;
	size_t buf_size;
	int qid;
	int error;
	u32 s1;
	u32 s2;
	u16 transaction;
	u8 status;
	u8 hash_id;
	u8 hash_len;
	u8 dhgroup_id;
	u8 c1[64];
	u8 c2[64];
	u8 response[64];
	u8 *host_response;
	u8 *ctrl_key;
	int ctrl_key_len;
	u8 *host_key;
	int host_key_len;
	u8 *sess_key;
	int sess_key_len;
};

static struct nvme_auth_dhgroup_map {
	int id;
	const char name[16];
	const char kpp[16];
	int privkey_size;
	int pubkey_size;
} dhgroup_map[] = {
	{ .id = NVME_AUTH_DHCHAP_DHGROUP_NULL,
	  .name = "NULL", .kpp = "NULL",
	  .privkey_size = 0, .pubkey_size = 0 },
	{ .id = NVME_AUTH_DHCHAP_DHGROUP_2048,
	  .name = "ffdhe2048", .kpp = "dh",
	  .privkey_size = 256, .pubkey_size = 256 },
	{ .id = NVME_AUTH_DHCHAP_DHGROUP_3072,
	  .name = "ffdhe3072", .kpp = "dh",
	  .privkey_size = 384, .pubkey_size = 384 },
	{ .id = NVME_AUTH_DHCHAP_DHGROUP_4096,
	  .name = "ffdhe4096", .kpp = "dh",
	  .privkey_size = 512, .pubkey_size = 512 },
	{ .id = NVME_AUTH_DHCHAP_DHGROUP_6144,
	  .name = "ffdhe6144", .kpp = "dh",
	  .privkey_size = 768, .pubkey_size = 768 },
	{ .id = NVME_AUTH_DHCHAP_DHGROUP_8192,
	  .name = "ffdhe8192", .kpp = "dh",
	  .privkey_size = 1024, .pubkey_size = 1024 },
};

const char *nvme_auth_dhgroup_name(int dhgroup_id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dhgroup_map); i++) {
		if (dhgroup_map[i].id == dhgroup_id)
			return dhgroup_map[i].name;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(nvme_auth_dhgroup_name);

int nvme_auth_dhgroup_pubkey_size(int dhgroup_id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dhgroup_map); i++) {
		if (dhgroup_map[i].id == dhgroup_id)
			return dhgroup_map[i].pubkey_size;
	}
	return -1;
}
EXPORT_SYMBOL_GPL(nvme_auth_dhgroup_pubkey_size);

int nvme_auth_dhgroup_privkey_size(int dhgroup_id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dhgroup_map); i++) {
		if (dhgroup_map[i].id == dhgroup_id)
			return dhgroup_map[i].privkey_size;
	}
	return -1;
}
EXPORT_SYMBOL_GPL(nvme_auth_dhgroup_privkey_size);

const char *nvme_auth_dhgroup_kpp(int dhgroup_id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dhgroup_map); i++) {
		if (dhgroup_map[i].id == dhgroup_id)
			return dhgroup_map[i].kpp;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(nvme_auth_dhgroup_kpp);

int nvme_auth_dhgroup_id(const char *dhgroup_name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dhgroup_map); i++) {
		if (!strncmp(dhgroup_map[i].name, dhgroup_name,
			     strlen(dhgroup_map[i].name)))
			return dhgroup_map[i].id;
	}
	return -1;
}
EXPORT_SYMBOL_GPL(nvme_auth_dhgroup_id);

static struct nvme_dhchap_hash_map {
	int id;
	const char hmac[15];
	const char digest[15];
} hash_map[] = {
	{.id = NVME_AUTH_DHCHAP_SHA256,
	 .hmac = "hmac(sha256)", .digest = "sha256" },
	{.id = NVME_AUTH_DHCHAP_SHA384,
	 .hmac = "hmac(sha384)", .digest = "sha384" },
	{.id = NVME_AUTH_DHCHAP_SHA512,
	 .hmac = "hmac(sha512)", .digest = "sha512" },
};

const char *nvme_auth_hmac_name(int hmac_id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(hash_map); i++) {
		if (hash_map[i].id == hmac_id)
			return hash_map[i].hmac;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(nvme_auth_hmac_name);

const char *nvme_auth_digest_name(int hmac_id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(hash_map); i++) {
		if (hash_map[i].id == hmac_id)
			return hash_map[i].digest;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(nvme_auth_digest_name);

int nvme_auth_hmac_id(const char *hmac_name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(hash_map); i++) {
		if (!strncmp(hash_map[i].hmac, hmac_name,
			     strlen(hash_map[i].hmac)))
			return hash_map[i].id;
	}
	return -1;
}
EXPORT_SYMBOL_GPL(nvme_auth_hmac_id);

unsigned char *nvme_auth_extract_secret(unsigned char *secret, size_t *out_len)
{
	unsigned char *key;
	u32 crc;
	int key_len;
	size_t allocated_len;

	allocated_len = strlen(secret);
	key = kzalloc(allocated_len, GFP_KERNEL);
	if (!key)
		return ERR_PTR(-ENOMEM);

	key_len = base64_decode(secret, allocated_len, key);
	if (key_len != 36 && key_len != 52 &&
	    key_len != 68) {
		pr_debug("Invalid DH-HMAC-CHAP key len %d\n",
			 key_len);
		kfree_sensitive(key);
		return ERR_PTR(-EINVAL);
	}

	/* The last four bytes is the CRC in little-endian format */
	key_len -= 4;
	/*
	 * The linux implementation doesn't do pre- and post-increments,
	 * so we have to do it manually.
	 */
	crc = ~crc32(~0, key, key_len);

	if (get_unaligned_le32(key + key_len) != crc) {
		pr_debug("DH-HMAC-CHAP key crc mismatch (key %08x, crc %08x)\n",
		       get_unaligned_le32(key + key_len), crc);
		kfree_sensitive(key);
		return ERR_PTR(-EKEYREJECTED);
	}
	*out_len = key_len;
	return key;
}
EXPORT_SYMBOL_GPL(nvme_auth_extract_secret);

u8 *nvme_auth_transform_key(u8 *key, size_t key_len, u8 key_hash, char *nqn)
{
	const char *hmac_name = nvme_auth_hmac_name(key_hash);
	struct crypto_shash *key_tfm;
	struct shash_desc *shash;
	u8 *transformed_key;
	int ret;

	/* No key transformation required */
	if (key_hash == 0)
		return 0;

	hmac_name = nvme_auth_hmac_name(key_hash);
	if (!hmac_name) {
		pr_warn("Invalid key hash id %d\n", key_hash);
		return ERR_PTR(-EKEYREJECTED);
	}
	key_tfm = crypto_alloc_shash(hmac_name, 0, 0);
	if (IS_ERR(key_tfm))
		return (u8 *)key_tfm;

	shash = kmalloc(sizeof(struct shash_desc) +
			crypto_shash_descsize(key_tfm),
			GFP_KERNEL);
	if (!shash) {
		crypto_free_shash(key_tfm);
		return ERR_PTR(-ENOMEM);
	}
	transformed_key = kzalloc(crypto_shash_digestsize(key_tfm), GFP_KERNEL);
	if (!transformed_key) {
		ret = -ENOMEM;
		goto out_free_shash;
	}

	shash->tfm = key_tfm;
	ret = crypto_shash_setkey(key_tfm, key, key_len);
	if (ret < 0)
		goto out_free_shash;
	ret = crypto_shash_init(shash);
	if (ret < 0)
		goto out_free_shash;
	ret = crypto_shash_update(shash, nqn, strlen(nqn));
	if (ret < 0)
		goto out_free_shash;
	ret = crypto_shash_update(shash, "NVMe-over-Fabrics", 17);
	if (ret < 0)
		goto out_free_shash;
	ret = crypto_shash_final(shash, transformed_key);
out_free_shash:
	kfree(shash);
	crypto_free_shash(key_tfm);
	if (ret < 0) {
		kfree_sensitive(transformed_key);
		return ERR_PTR(ret);
	}
	return transformed_key;
}
EXPORT_SYMBOL_GPL(nvme_auth_transform_key);

static int nvme_auth_hash_skey(int hmac_id, u8 *skey, size_t skey_len, u8 *hkey)
{
	const char *digest_name;
	struct crypto_shash *tfm;
	int ret;

	digest_name = nvme_auth_digest_name(hmac_id);
	if (!digest_name) {
		pr_debug("%s: failed to get digest for %d\n", __func__,
			 hmac_id);
		return -EINVAL;
	}
	tfm = crypto_alloc_shash(digest_name, 0, 0);
	if (IS_ERR(tfm))
		return -ENOMEM;

	ret = crypto_shash_tfm_digest(tfm, skey, skey_len, hkey);
	if (ret < 0)
		pr_debug("%s: Failed to hash digest len %zu\n", __func__,
			 skey_len);

	crypto_free_shash(tfm);
	return ret;
}

int nvme_auth_augmented_challenge(u8 hmac_id, u8 *skey, size_t skey_len,
		u8 *challenge, u8 *aug, size_t hlen)
{
	struct crypto_shash *tfm;
	struct shash_desc *desc;
	u8 *hashed_key;
	const char *hmac_name;
	int ret;

	hashed_key = kmalloc(hlen, GFP_KERNEL);
	if (!hashed_key)
		return -ENOMEM;

	ret = nvme_auth_hash_skey(hmac_id, skey,
				  skey_len, hashed_key);
	if (ret < 0)
		goto out_free_key;

	hmac_name = nvme_auth_hmac_name(hmac_id);
	if (!hmac_name) {
		pr_warn("%s: invalid hash algoritm %d\n",
			__func__, hmac_id);
		ret = -EINVAL;
		goto out_free_key;
	}
	tfm = crypto_alloc_shash(hmac_name, 0, 0);
	if (IS_ERR(tfm)) {
		ret = PTR_ERR(tfm);
		goto out_free_key;
	}
	desc = kmalloc(sizeof(struct shash_desc) + crypto_shash_descsize(tfm),
		       GFP_KERNEL);
	if (!desc) {
		ret = -ENOMEM;
		goto out_free_hash;
	}
	desc->tfm = tfm;

	ret = crypto_shash_setkey(tfm, hashed_key, hlen);
	if (ret)
		goto out_free_desc;

	ret = crypto_shash_init(desc);
	if (ret)
		goto out_free_desc;

	ret = crypto_shash_update(desc, challenge, hlen);
	if (ret)
		goto out_free_desc;

	ret = crypto_shash_final(desc, aug);
out_free_desc:
	kfree_sensitive(desc);
out_free_hash:
	crypto_free_shash(tfm);
out_free_key:
	kfree_sensitive(hashed_key);
	return ret;
}
EXPORT_SYMBOL_GPL(nvme_auth_augmented_challenge);

int nvme_auth_gen_privkey(struct crypto_kpp *dh_tfm, int dh_gid)
{
	char *pkey;
	int ret, pkey_len;

	if (dh_gid == NVME_AUTH_DHCHAP_DHGROUP_2048 ||
	    dh_gid == NVME_AUTH_DHCHAP_DHGROUP_3072 ||
	    dh_gid == NVME_AUTH_DHCHAP_DHGROUP_4096 ||
	    dh_gid == NVME_AUTH_DHCHAP_DHGROUP_6144 ||
	    dh_gid == NVME_AUTH_DHCHAP_DHGROUP_8192) {
		struct dh p = {0};
		int bits = nvme_auth_dhgroup_pubkey_size(dh_gid) << 3;
		int dh_secret_len = 64;
		u8 *dh_secret = kzalloc(dh_secret_len, GFP_KERNEL);

		if (!dh_secret)
			return -ENOMEM;

		/*
		 * NVMe base spec v2.0: The DH value shall be set to the value
		 * of g^x mod p, where 'x' is a random number selected by the
		 * host that shall be at least 256 bits long.
		 *
		 * We will be using a 512 bit random number as private key.
		 * This is large enough to provide adequate security, but
		 * small enough such that we can trivially conform to
		 * NIST SB800-56A section 5.6.1.1.4 if
		 * we guarantee that the random number is not either
		 * all 0xff or all 0x00. But that should be guaranteed
		 * by the in-kernel RNG anyway.
		 */
		get_random_bytes(dh_secret, dh_secret_len);

		ret = crypto_ffdhe_params(&p, bits);
		if (ret) {
			kfree_sensitive(dh_secret);
			return ret;
		}

		p.key = dh_secret;
		p.key_size = dh_secret_len;

		pkey_len = crypto_dh_key_len(&p);
		pkey = kmalloc(pkey_len, GFP_KERNEL);
		if (!pkey) {
			kfree_sensitive(dh_secret);
			return -ENOMEM;
		}

		get_random_bytes(pkey, pkey_len);
		ret = crypto_dh_encode_key(pkey, pkey_len, &p);
		if (ret) {
			pr_debug("failed to encode private key, error %d\n",
				 ret);
			kfree_sensitive(dh_secret);
			goto out;
		}
	} else {
		pr_warn("invalid dh group %d\n", dh_gid);
		return -EINVAL;
	}
	ret = crypto_kpp_set_secret(dh_tfm, pkey, pkey_len);
	if (ret)
		pr_debug("failed to set private key, error %d\n", ret);
out:
	kfree_sensitive(pkey);
	return ret;
}
EXPORT_SYMBOL_GPL(nvme_auth_gen_privkey);

int nvme_auth_gen_pubkey(struct crypto_kpp *dh_tfm,
		u8 *host_key, size_t host_key_len)
{
	struct kpp_request *req;
	struct crypto_wait wait;
	struct scatterlist dst;
	int ret;

	req = kpp_request_alloc(dh_tfm, GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	crypto_init_wait(&wait);
	kpp_request_set_input(req, NULL, 0);
	sg_init_one(&dst, host_key, host_key_len);
	kpp_request_set_output(req, &dst, host_key_len);
	kpp_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				 crypto_req_done, &wait);

	ret = crypto_wait_req(crypto_kpp_generate_public_key(req), &wait);

	kpp_request_free(req);
	return ret;
}
EXPORT_SYMBOL_GPL(nvme_auth_gen_pubkey);

int nvme_auth_gen_shared_secret(struct crypto_kpp *dh_tfm,
		u8 *ctrl_key, size_t ctrl_key_len,
		u8 *sess_key, size_t sess_key_len)
{
	struct kpp_request *req;
	struct crypto_wait wait;
	struct scatterlist src, dst;
	int ret;

	req = kpp_request_alloc(dh_tfm, GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	crypto_init_wait(&wait);
	sg_init_one(&src, ctrl_key, ctrl_key_len);
	kpp_request_set_input(req, &src, ctrl_key_len);
	sg_init_one(&dst, sess_key, sess_key_len);
	kpp_request_set_output(req, &dst, sess_key_len);
	kpp_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				 crypto_req_done, &wait);

	ret = crypto_wait_req(crypto_kpp_compute_shared_secret(req), &wait);

	kpp_request_free(req);
	return ret;
}
EXPORT_SYMBOL_GPL(nvme_auth_gen_shared_secret);

static int nvme_auth_send(struct nvme_ctrl *ctrl, int qid,
		void *data, size_t tl)
{
	struct nvme_command cmd = {};
	blk_mq_req_flags_t flags = qid == NVME_QID_ANY ?
		0 : BLK_MQ_REQ_NOWAIT | BLK_MQ_REQ_RESERVED;
	struct request_queue *q = qid == NVME_QID_ANY ?
		ctrl->fabrics_q : ctrl->connect_q;
	int ret;

	cmd.auth_send.opcode = nvme_fabrics_command;
	cmd.auth_send.fctype = nvme_fabrics_type_auth_send;
	cmd.auth_send.secp = NVME_AUTH_DHCHAP_PROTOCOL_IDENTIFIER;
	cmd.auth_send.spsp0 = 0x01;
	cmd.auth_send.spsp1 = 0x01;
	cmd.auth_send.tl = tl;

	ret = __nvme_submit_sync_cmd(q, &cmd, NULL, data, tl, 0, qid,
				     0, flags);
	if (ret > 0)
		dev_dbg(ctrl->device,
			"%s: qid %d nvme status %d\n", __func__, qid, ret);
	else if (ret < 0)
		dev_dbg(ctrl->device,
			"%s: qid %d error %d\n", __func__, qid, ret);
	return ret;
}

static int nvme_auth_receive(struct nvme_ctrl *ctrl, int qid,
		void *buf, size_t al)
{
	struct nvme_command cmd = {};
	blk_mq_req_flags_t flags = qid == NVME_QID_ANY ?
		0 : BLK_MQ_REQ_NOWAIT | BLK_MQ_REQ_RESERVED;
	struct request_queue *q = qid == NVME_QID_ANY ?
		ctrl->fabrics_q : ctrl->connect_q;
	int ret;

	cmd.auth_receive.opcode = nvme_fabrics_command;
	cmd.auth_receive.fctype = nvme_fabrics_type_auth_receive;
	cmd.auth_receive.secp = NVME_AUTH_DHCHAP_PROTOCOL_IDENTIFIER;
	cmd.auth_receive.spsp0 = 0x01;
	cmd.auth_receive.spsp1 = 0x01;
	cmd.auth_receive.al = al;

	ret = __nvme_submit_sync_cmd(q, &cmd, NULL, buf, al, 0, qid,
				     0, flags);
	if (ret > 0) {
		dev_dbg(ctrl->device, "%s: qid %d nvme status %x\n",
			__func__, qid, ret);
		ret = -EIO;
	}
	if (ret < 0) {
		dev_dbg(ctrl->device, "%s: qid %d error %d\n",
			__func__, qid, ret);
		return ret;
	}

	return 0;
}

static int nvme_auth_receive_validate(struct nvme_ctrl *ctrl, int qid,
		struct nvmf_auth_dhchap_failure_data *data,
		u16 transaction, u8 expected_msg)
{
	dev_dbg(ctrl->device, "%s: qid %d auth_type %d auth_id %x\n",
		__func__, qid, data->auth_type, data->auth_id);

	if (data->auth_type == NVME_AUTH_COMMON_MESSAGES &&
	    data->auth_id == NVME_AUTH_DHCHAP_MESSAGE_FAILURE1) {
		return data->rescode_exp;
	}
	if (data->auth_type != NVME_AUTH_DHCHAP_MESSAGES ||
	    data->auth_id != expected_msg) {
		dev_warn(ctrl->device,
			 "qid %d invalid message %02x/%02x\n",
			 qid, data->auth_type, data->auth_id);
		return NVME_AUTH_DHCHAP_FAILURE_INCORRECT_MESSAGE;
	}
	if (le16_to_cpu(data->t_id) != transaction) {
		dev_warn(ctrl->device,
			 "qid %d invalid transaction ID %d\n",
			 qid, le16_to_cpu(data->t_id));
		return NVME_AUTH_DHCHAP_FAILURE_INCORRECT_MESSAGE;
	}
	return 0;
}

static int nvme_auth_set_dhchap_negotiate_data(struct nvme_ctrl *ctrl,
		struct nvme_dhchap_queue_context *chap)
{
	struct nvmf_auth_dhchap_negotiate_data *data = chap->buf;
	size_t size = sizeof(*data) + sizeof(union nvmf_auth_protocol);

	if (chap->buf_size < size) {
		chap->status = NVME_AUTH_DHCHAP_FAILURE_INCORRECT_PAYLOAD;
		return -EINVAL;
	}
	memset((u8 *)chap->buf, 0, size);
	data->auth_type = NVME_AUTH_COMMON_MESSAGES;
	data->auth_id = NVME_AUTH_DHCHAP_MESSAGE_NEGOTIATE;
	data->t_id = cpu_to_le16(chap->transaction);
	data->sc_c = 0; /* No secure channel concatenation */
	data->napd = 1;
	data->auth_protocol[0].dhchap.authid = NVME_AUTH_DHCHAP_AUTH_ID;
	data->auth_protocol[0].dhchap.halen = 3;
	data->auth_protocol[0].dhchap.dhlen = 6;
	data->auth_protocol[0].dhchap.idlist[0] = NVME_AUTH_DHCHAP_SHA256;
	data->auth_protocol[0].dhchap.idlist[1] = NVME_AUTH_DHCHAP_SHA384;
	data->auth_protocol[0].dhchap.idlist[2] = NVME_AUTH_DHCHAP_SHA512;
	data->auth_protocol[0].dhchap.idlist[3] = NVME_AUTH_DHCHAP_DHGROUP_NULL;
	data->auth_protocol[0].dhchap.idlist[4] = NVME_AUTH_DHCHAP_DHGROUP_2048;
	data->auth_protocol[0].dhchap.idlist[5] = NVME_AUTH_DHCHAP_DHGROUP_3072;
	data->auth_protocol[0].dhchap.idlist[6] = NVME_AUTH_DHCHAP_DHGROUP_4096;
	data->auth_protocol[0].dhchap.idlist[7] = NVME_AUTH_DHCHAP_DHGROUP_6144;
	data->auth_protocol[0].dhchap.idlist[8] = NVME_AUTH_DHCHAP_DHGROUP_8192;

	return size;
}

static int nvme_auth_process_dhchap_challenge(struct nvme_ctrl *ctrl,
		struct nvme_dhchap_queue_context *chap)
{
	struct nvmf_auth_dhchap_challenge_data *data = chap->buf;
	size_t size = sizeof(*data) + data->hl + data->dhvlen;
	const char *hmac_name;
	const char *kpp_name;

	if (chap->buf_size < size) {
		chap->status = NVME_AUTH_DHCHAP_FAILURE_INCORRECT_PAYLOAD;
		return NVME_SC_INVALID_FIELD;
	}

	hmac_name = nvme_auth_hmac_name(data->hashid);
	if (!hmac_name) {
		dev_warn(ctrl->device,
			 "qid %d: invalid HASH ID %d\n",
			 chap->qid, data->hashid);
		chap->status = NVME_AUTH_DHCHAP_FAILURE_HASH_UNUSABLE;
		return -EPROTO;
	}
	if (chap->hash_id == data->hashid && chap->shash_tfm &&
	    !strcmp(crypto_shash_alg_name(chap->shash_tfm), hmac_name) &&
	    crypto_shash_digestsize(chap->shash_tfm) == data->hl) {
		dev_dbg(ctrl->device,
			"qid %d: reuse existing hash %s\n",
			chap->qid, hmac_name);
		goto select_kpp;
	}
	if (chap->shash_tfm) {
		crypto_free_shash(chap->shash_tfm);
		chap->hash_id = 0;
		chap->hash_len = 0;
	}
	chap->shash_tfm = crypto_alloc_shash(hmac_name, 0,
					     CRYPTO_ALG_ALLOCATES_MEMORY);
	if (IS_ERR(chap->shash_tfm)) {
		dev_warn(ctrl->device,
			 "qid %d: failed to allocate hash %s, error %ld\n",
			 chap->qid, hmac_name, PTR_ERR(chap->shash_tfm));
		chap->shash_tfm = NULL;
		chap->status = NVME_AUTH_DHCHAP_FAILURE_FAILED;
		return NVME_SC_AUTH_REQUIRED;
	}
	if (crypto_shash_digestsize(chap->shash_tfm) != data->hl) {
		dev_warn(ctrl->device,
			 "qid %d: invalid hash length %d\n",
			 chap->qid, data->hl);
		crypto_free_shash(chap->shash_tfm);
		chap->shash_tfm = NULL;
		chap->status = NVME_AUTH_DHCHAP_FAILURE_HASH_UNUSABLE;
		return NVME_SC_AUTH_REQUIRED;
	}
	if (chap->hash_id != data->hashid) {
		kfree(chap->host_response);
		chap->host_response = NULL;
	}
	chap->hash_id = data->hashid;
	chap->hash_len = data->hl;
	dev_dbg(ctrl->device, "qid %d: selected hash %s\n",
		chap->qid, hmac_name);
select_kpp:
	kpp_name = nvme_auth_dhgroup_kpp(data->dhgid);
	if (!kpp_name) {
		dev_warn(ctrl->device,
			 "qid %d: invalid DH group id %d\n",
			 chap->qid, data->dhgid);
		chap->status = NVME_AUTH_DHCHAP_FAILURE_DHGROUP_UNUSABLE;
		return -EPROTO;
	}

	if (data->dhgid != NVME_AUTH_DHCHAP_DHGROUP_NULL) {
		const char *gid_name = nvme_auth_dhgroup_name(data->dhgid);

		if (data->dhvlen == 0) {
			dev_warn(ctrl->device,
				 "qid %d: empty DH value\n",
				 chap->qid);
			chap->status = NVME_AUTH_DHCHAP_FAILURE_DHGROUP_UNUSABLE;
			return -EPROTO;
		}
		if (chap->dh_tfm && chap->dhgroup_id == data->dhgid) {
			dev_dbg(ctrl->device,
				"qid %d: reuse existing DH group %s\n",
				chap->qid, gid_name);
			goto skip_kpp;
		}
		chap->dh_tfm = crypto_alloc_kpp(kpp_name, 0, 0);
		if (IS_ERR(chap->dh_tfm)) {
			int ret = PTR_ERR(chap->dh_tfm);

			dev_warn(ctrl->device,
				 "qid %d: failed to initialize DH group %s\n",
				 chap->qid, gid_name);
			chap->status = NVME_AUTH_DHCHAP_FAILURE_DHGROUP_UNUSABLE;
			chap->dh_tfm = NULL;
			return ret;
		}
		/* Clear host key to avoid accidental reuse */
		kfree_sensitive(chap->host_key);
		chap->host_key_len = 0;
		dev_dbg(ctrl->device, "qid %d: selected DH group %s\n",
			chap->qid, gid_name);
	} else {
		if (data->dhvlen != 0) {
			dev_warn(ctrl->device,
				 "qid %d: invalid DH value for NULL DH\n",
				 chap->qid);
			chap->status = NVME_AUTH_DHCHAP_FAILURE_DHGROUP_UNUSABLE;
			return -EPROTO;
		}
		if (chap->dh_tfm) {
			crypto_free_kpp(chap->dh_tfm);
			chap->dh_tfm = NULL;
		}
	}
	chap->dhgroup_id = data->dhgid;
skip_kpp:
	chap->s1 = le32_to_cpu(data->seqnum);
	memcpy(chap->c1, data->cval, chap->hash_len);
	if (data->dhvlen) {
		chap->ctrl_key = kmalloc(data->dhvlen, GFP_KERNEL);
		if (!chap->ctrl_key)
			return -ENOMEM;
		chap->ctrl_key_len = data->dhvlen;
		memcpy(chap->ctrl_key, data->cval + chap->hash_len,
		       data->dhvlen);
		dev_dbg(ctrl->device, "ctrl public key %*ph\n",
			 (int)chap->ctrl_key_len, chap->ctrl_key);
	}

	return 0;
}

static int nvme_auth_set_dhchap_reply_data(struct nvme_ctrl *ctrl,
		struct nvme_dhchap_queue_context *chap)
{
	struct nvmf_auth_dhchap_reply_data *data = chap->buf;
	size_t size = sizeof(*data);

	size += 2 * chap->hash_len;
	if (ctrl->opts->dhchap_bidi) {
		get_random_bytes(chap->c2, chap->hash_len);
		chap->s2 = nvme_dhchap_seqnum++;
	} else
		memset(chap->c2, 0, chap->hash_len);

	if (chap->host_key_len)
		size += chap->host_key_len;

	if (chap->buf_size < size) {
		chap->status = NVME_AUTH_DHCHAP_FAILURE_INCORRECT_PAYLOAD;
		return -EINVAL;
	}
	memset(chap->buf, 0, size);
	data->auth_type = NVME_AUTH_DHCHAP_MESSAGES;
	data->auth_id = NVME_AUTH_DHCHAP_MESSAGE_REPLY;
	data->t_id = cpu_to_le16(chap->transaction);
	data->hl = chap->hash_len;
	data->dhvlen = chap->host_key_len;
	data->seqnum = cpu_to_le32(chap->s2);
	memcpy(data->rval, chap->response, chap->hash_len);
	if (ctrl->opts->dhchap_bidi) {
		dev_dbg(ctrl->device, "%s: qid %d ctrl challenge %*ph\n",
			__func__, chap->qid,
			chap->hash_len, chap->c2);
		data->cvalid = 1;
		memcpy(data->rval + chap->hash_len, chap->c2,
		       chap->hash_len);
	}
	if (chap->host_key_len) {
		dev_dbg(ctrl->device, "%s: qid %d host public key %*ph\n",
			__func__, chap->qid,
			chap->host_key_len, chap->host_key);
		memcpy(data->rval + 2 * chap->hash_len, chap->host_key,
		       chap->host_key_len);
	}
	return size;
}

static int nvme_auth_process_dhchap_success1(struct nvme_ctrl *ctrl,
		struct nvme_dhchap_queue_context *chap)
{
	struct nvmf_auth_dhchap_success1_data *data = chap->buf;
	size_t size = sizeof(*data);

	if (ctrl->opts->dhchap_bidi)
		size += chap->hash_len;


	if (chap->buf_size < size) {
		chap->status = NVME_AUTH_DHCHAP_FAILURE_INCORRECT_PAYLOAD;
		return NVME_SC_INVALID_FIELD;
	}

	if (data->hl != chap->hash_len) {
		dev_warn(ctrl->device,
			 "qid %d: invalid hash length %d\n",
			 chap->qid, data->hl);
		chap->status = NVME_AUTH_DHCHAP_FAILURE_HASH_UNUSABLE;
		return NVME_SC_INVALID_FIELD;
	}

	if (!data->rvalid)
		return 0;

	/* Validate controller response */
	if (memcmp(chap->response, data->rval, data->hl)) {
		dev_dbg(ctrl->device, "%s: qid %d ctrl response %*ph\n",
			__func__, chap->qid, chap->hash_len, data->rval);
		dev_dbg(ctrl->device, "%s: qid %d host response %*ph\n",
			__func__, chap->qid, chap->hash_len, chap->response);
		dev_warn(ctrl->device,
			 "qid %d: controller authentication failed\n",
			 chap->qid);
		chap->status = NVME_AUTH_DHCHAP_FAILURE_FAILED;
		return NVME_SC_AUTH_REQUIRED;
	}
	dev_info(ctrl->device,
		 "qid %d: controller authenticated\n",
		chap->qid);
	return 0;
}

static int nvme_auth_set_dhchap_success2_data(struct nvme_ctrl *ctrl,
		struct nvme_dhchap_queue_context *chap)
{
	struct nvmf_auth_dhchap_success2_data *data = chap->buf;
	size_t size = sizeof(*data);

	memset(chap->buf, 0, size);
	data->auth_type = NVME_AUTH_DHCHAP_MESSAGES;
	data->auth_id = NVME_AUTH_DHCHAP_MESSAGE_SUCCESS2;
	data->t_id = cpu_to_le16(chap->transaction);

	return size;
}

static int nvme_auth_set_dhchap_failure2_data(struct nvme_ctrl *ctrl,
		struct nvme_dhchap_queue_context *chap)
{
	struct nvmf_auth_dhchap_failure_data *data = chap->buf;
	size_t size = sizeof(*data);

	memset(chap->buf, 0, size);
	data->auth_type = NVME_AUTH_DHCHAP_MESSAGES;
	data->auth_id = NVME_AUTH_DHCHAP_MESSAGE_FAILURE2;
	data->t_id = cpu_to_le16(chap->transaction);
	data->rescode = NVME_AUTH_DHCHAP_FAILURE_REASON_FAILED;
	data->rescode_exp = chap->status;

	return size;
}

static int nvme_auth_dhchap_host_response(struct nvme_ctrl *ctrl,
		struct nvme_dhchap_queue_context *chap)
{
	SHASH_DESC_ON_STACK(shash, chap->shash_tfm);
	u8 buf[4], *challenge = chap->c1;
	int ret;

	dev_dbg(ctrl->device, "%s: qid %d host response seq %d transaction %d\n",
		__func__, chap->qid, chap->s1, chap->transaction);

	if (!chap->host_response) {
		chap->host_response = nvme_auth_transform_key(ctrl->dhchap_key,
					chap->hash_len, chap->hash_id,
					ctrl->opts->host->nqn);
		if (IS_ERR(chap->host_response)) {
			ret = PTR_ERR(chap->host_response);
			chap->host_response = NULL;
			return ret;
		}
	}
	ret = crypto_shash_setkey(chap->shash_tfm,
			chap->host_response, chap->hash_len);
	if (ret) {
		dev_warn(ctrl->device, "qid %d: failed to set key, error %d\n",
			 chap->qid, ret);
		goto out;
	}
	dev_dbg(ctrl->device,
		"%s: using key %*ph\n", __func__,
		(int)chap->hash_len, chap->host_response);
	if (chap->dh_tfm) {
		challenge = kmalloc(chap->hash_len, GFP_KERNEL);
		if (!challenge) {
			ret = -ENOMEM;
			goto out;
		}
		ret = nvme_auth_augmented_challenge(chap->hash_id,
						    chap->sess_key,
						    chap->sess_key_len,
						    chap->c1, challenge,
						    chap->hash_len);
		if (ret)
			goto out;
	}
	shash->tfm = chap->shash_tfm;
	ret = crypto_shash_init(shash);
	if (ret)
		goto out;
	ret = crypto_shash_update(shash, challenge, chap->hash_len);
	if (ret)
		goto out;
	put_unaligned_le32(chap->s1, buf);
	ret = crypto_shash_update(shash, buf, 4);
	if (ret)
		goto out;
	put_unaligned_le16(chap->transaction, buf);
	ret = crypto_shash_update(shash, buf, 2);
	if (ret)
		goto out;
	memset(buf, 0, sizeof(buf));
	ret = crypto_shash_update(shash, buf, 1);
	if (ret)
		goto out;
	ret = crypto_shash_update(shash, "HostHost", 8);
	if (ret)
		goto out;
	ret = crypto_shash_update(shash, ctrl->opts->host->nqn,
				  strlen(ctrl->opts->host->nqn));
	if (ret)
		goto out;
	ret = crypto_shash_update(shash, buf, 1);
	if (ret)
		goto out;
	ret = crypto_shash_update(shash, ctrl->opts->subsysnqn,
			    strlen(ctrl->opts->subsysnqn));
	if (ret)
		goto out;
	ret = crypto_shash_final(shash, chap->response);
out:
	if (challenge != chap->c1)
		kfree(challenge);
	return ret;
}

static int nvme_auth_dhchap_ctrl_response(struct nvme_ctrl *ctrl,
		struct nvme_dhchap_queue_context *chap)
{
	SHASH_DESC_ON_STACK(shash, chap->shash_tfm);
	u8 *ctrl_response;
	u8 buf[4], *challenge = chap->c2;
	int ret;

	ctrl_response = nvme_auth_transform_key(ctrl->dhchap_key,
				chap->hash_len, chap->hash_id,
				ctrl->opts->subsysnqn);
	if (IS_ERR(ctrl_response)) {
		ret = PTR_ERR(ctrl_response);
		return ret;
	}
	ret = crypto_shash_setkey(chap->shash_tfm,
			ctrl_response, ctrl->dhchap_key_len);
	if (ret) {
		dev_warn(ctrl->device, "qid %d: failed to set key, error %d\n",
			 chap->qid, ret);
		goto out;
	}
	dev_dbg(ctrl->device,
		"%s: using key %*ph\n", __func__,
		(int)ctrl->dhchap_key_len, ctrl_response);

	if (chap->dh_tfm) {
		challenge = kmalloc(chap->hash_len, GFP_KERNEL);
		if (!challenge) {
			ret = -ENOMEM;
			goto out;
		}
		ret = nvme_auth_augmented_challenge(chap->hash_id,
						    chap->sess_key,
						    chap->sess_key_len,
						    chap->c2, challenge,
						    chap->hash_len);
		if (ret)
			goto out;
	}
	dev_dbg(ctrl->device, "%s: qid %d host response seq %d transaction %d\n",
		__func__, chap->qid, chap->s2, chap->transaction);
	dev_dbg(ctrl->device, "%s: qid %d challenge %*ph\n",
		__func__, chap->qid, chap->hash_len, challenge);
	dev_dbg(ctrl->device, "%s: qid %d subsysnqn %s\n",
		__func__, chap->qid, ctrl->opts->subsysnqn);
	dev_dbg(ctrl->device, "%s: qid %d hostnqn %s\n",
		__func__, chap->qid, ctrl->opts->host->nqn);
	shash->tfm = chap->shash_tfm;
	ret = crypto_shash_init(shash);
	if (ret)
		goto out;
	ret = crypto_shash_update(shash, challenge, chap->hash_len);
	if (ret)
		goto out;
	put_unaligned_le32(chap->s2, buf);
	ret = crypto_shash_update(shash, buf, 4);
	if (ret)
		goto out;
	put_unaligned_le16(chap->transaction, buf);
	ret = crypto_shash_update(shash, buf, 2);
	if (ret)
		goto out;
	memset(buf, 0, 4);
	ret = crypto_shash_update(shash, buf, 1);
	if (ret)
		goto out;
	ret = crypto_shash_update(shash, "Controller", 10);
	if (ret)
		goto out;
	ret = crypto_shash_update(shash, ctrl->opts->subsysnqn,
				  strlen(ctrl->opts->subsysnqn));
	if (ret)
		goto out;
	ret = crypto_shash_update(shash, buf, 1);
	if (ret)
		goto out;
	ret = crypto_shash_update(shash, ctrl->opts->host->nqn,
				  strlen(ctrl->opts->host->nqn));
	if (ret)
		goto out;
	ret = crypto_shash_final(shash, chap->response);
out:
	if (challenge != chap->c2)
		kfree(challenge);
	return ret;
}

int nvme_auth_generate_key(struct nvme_ctrl *ctrl)
{
	int ret;
	u8 key_hash;

	if (!ctrl->opts->dhchap_secret)
		return 0;

	if (ctrl->dhchap_key && ctrl->dhchap_key_len)
		/* Key already set */
		return 0;

	if (sscanf(ctrl->opts->dhchap_secret, "DHHC-1:%hhd:%*s:",
		   &key_hash) != 1)
		return -EINVAL;

	/* Pass in the secret without the 'DHHC-1:XX:' prefix */
	ctrl->dhchap_key = nvme_auth_extract_secret(ctrl->opts->dhchap_secret + 10,
					     &ctrl->dhchap_key_len);
	if (IS_ERR(ctrl->dhchap_key)) {
		ret = PTR_ERR(ctrl->dhchap_key);
		ctrl->dhchap_key = NULL;
		return ret;
	}
	return ret;
}
EXPORT_SYMBOL_GPL(nvme_auth_generate_key);

static int nvme_auth_dhchap_exponential(struct nvme_ctrl *ctrl,
		struct nvme_dhchap_queue_context *chap)
{
	int ret;

	if (chap->host_key && chap->host_key_len) {
		dev_dbg(ctrl->device,
			"qid %d: reusing host key\n", chap->qid);
		goto gen_sesskey;
	}
	ret = nvme_auth_gen_privkey(chap->dh_tfm, chap->dhgroup_id);
	if (ret < 0) {
		chap->status = NVME_AUTH_DHCHAP_FAILURE_INCORRECT_PAYLOAD;
		return ret;
	}

	chap->host_key_len =
		nvme_auth_dhgroup_pubkey_size(chap->dhgroup_id);

	chap->host_key = kzalloc(chap->host_key_len, GFP_KERNEL);
	if (!chap->host_key) {
		chap->host_key_len = 0;
		chap->status = NVME_AUTH_DHCHAP_FAILURE_FAILED;
		return -ENOMEM;
	}
	ret = nvme_auth_gen_pubkey(chap->dh_tfm,
				   chap->host_key, chap->host_key_len);
	if (ret) {
		dev_dbg(ctrl->device,
			"failed to generate public key, error %d\n", ret);
		kfree(chap->host_key);
		chap->host_key = NULL;
		chap->host_key_len = 0;
		chap->status = NVME_AUTH_DHCHAP_FAILURE_INCORRECT_PAYLOAD;
		return ret;
	}

gen_sesskey:
	chap->sess_key_len = chap->host_key_len;
	chap->sess_key = kmalloc(chap->sess_key_len, GFP_KERNEL);
	if (!chap->sess_key) {
		chap->sess_key_len = 0;
		chap->status = NVME_AUTH_DHCHAP_FAILURE_FAILED;
		return -ENOMEM;
	}

	ret = nvme_auth_gen_shared_secret(chap->dh_tfm,
					  chap->ctrl_key, chap->ctrl_key_len,
					  chap->sess_key, chap->sess_key_len);
	if (ret) {
		dev_dbg(ctrl->device,
			"failed to generate shared secret, error %d\n", ret);
		kfree_sensitive(chap->sess_key);
		chap->sess_key = NULL;
		chap->sess_key_len = 0;
		chap->status = NVME_AUTH_DHCHAP_FAILURE_INCORRECT_PAYLOAD;
		return ret;
	}
	dev_dbg(ctrl->device, "shared secret %*ph\n",
		(int)chap->sess_key_len, chap->sess_key);
	return 0;
}

static void nvme_auth_reset(struct nvme_dhchap_queue_context *chap)
{
	kfree_sensitive(chap->ctrl_key);
	chap->ctrl_key = NULL;
	chap->ctrl_key_len = 0;
	kfree_sensitive(chap->sess_key);
	chap->sess_key = NULL;
	chap->sess_key_len = 0;
	chap->status = 0;
	chap->error = 0;
	chap->s1 = 0;
	chap->s2 = 0;
	chap->transaction = 0;
	memset(chap->c1, 0, sizeof(chap->c1));
	memset(chap->c2, 0, sizeof(chap->c2));
}

static void __nvme_auth_free(struct nvme_dhchap_queue_context *chap)
{
	if (chap->shash_tfm)
		crypto_free_shash(chap->shash_tfm);
	if (chap->dh_tfm)
		crypto_free_kpp(chap->dh_tfm);
	kfree_sensitive(chap->ctrl_key);
	kfree_sensitive(chap->host_key);
	kfree_sensitive(chap->sess_key);
	kfree_sensitive(chap->host_response);
	kfree(chap->buf);
	kfree(chap);
}

static void __nvme_auth_work(struct work_struct *work)
{
	struct nvme_dhchap_queue_context *chap =
		container_of(work, struct nvme_dhchap_queue_context, auth_work);
	struct nvme_ctrl *ctrl = chap->ctrl;
	size_t tl;
	int ret = 0;

	chap->transaction = ctrl->transaction++;

	/* DH-HMAC-CHAP Step 1: send negotiate */
	dev_dbg(ctrl->device, "%s: qid %d send negotiate\n",
		__func__, chap->qid);
	ret = nvme_auth_set_dhchap_negotiate_data(ctrl, chap);
	if (ret < 0) {
		chap->error = ret;
		return;
	}
	tl = ret;
	ret = nvme_auth_send(ctrl, chap->qid, chap->buf, tl);
	if (ret) {
		chap->error = ret;
		return;
	}

	/* DH-HMAC-CHAP Step 2: receive challenge */
	dev_dbg(ctrl->device, "%s: qid %d receive challenge\n",
		__func__, chap->qid);

	memset(chap->buf, 0, chap->buf_size);
	ret = nvme_auth_receive(ctrl, chap->qid, chap->buf, chap->buf_size);
	if (ret) {
		dev_warn(ctrl->device,
			 "qid %d failed to receive challenge, %s %d\n",
			 chap->qid, ret < 0 ? "error" : "nvme status", ret);
		chap->error = ret;
		return;
	}
	ret = nvme_auth_receive_validate(ctrl, chap->qid, chap->buf, chap->transaction,
					 NVME_AUTH_DHCHAP_MESSAGE_CHALLENGE);
	if (ret) {
		chap->status = ret;
		chap->error = NVME_SC_AUTH_REQUIRED;
		return;
	}

	ret = nvme_auth_process_dhchap_challenge(ctrl, chap);
	if (ret) {
		/* Invalid challenge parameters */
		goto fail2;
	}

	if (chap->ctrl_key_len) {
		dev_dbg(ctrl->device,
			"%s: qid %d DH exponential\n",
			__func__, chap->qid);
		ret = nvme_auth_dhchap_exponential(ctrl, chap);
		if (ret)
			goto fail2;
	}

	dev_dbg(ctrl->device, "%s: qid %d host response\n",
		__func__, chap->qid);
	ret = nvme_auth_dhchap_host_response(ctrl, chap);
	if (ret)
		goto fail2;

	/* DH-HMAC-CHAP Step 3: send reply */
	dev_dbg(ctrl->device, "%s: qid %d send reply\n",
		__func__, chap->qid);
	ret = nvme_auth_set_dhchap_reply_data(ctrl, chap);
	if (ret < 0)
		goto fail2;

	tl = ret;
	ret = nvme_auth_send(ctrl, chap->qid, chap->buf, tl);
	if (ret)
		goto fail2;

	/* DH-HMAC-CHAP Step 4: receive success1 */
	dev_dbg(ctrl->device, "%s: qid %d receive success1\n",
		__func__, chap->qid);

	memset(chap->buf, 0, chap->buf_size);
	ret = nvme_auth_receive(ctrl, chap->qid, chap->buf, chap->buf_size);
	if (ret) {
		dev_warn(ctrl->device,
			 "qid %d failed to receive success1, %s %d\n",
			 chap->qid, ret < 0 ? "error" : "nvme status", ret);
		chap->error = ret;
		return;
	}
	ret = nvme_auth_receive_validate(ctrl, chap->qid,
					 chap->buf, chap->transaction,
					 NVME_AUTH_DHCHAP_MESSAGE_SUCCESS1);
	if (ret) {
		chap->status = ret;
		chap->error = NVME_SC_AUTH_REQUIRED;
		return;
	}

	if (ctrl->opts->dhchap_bidi) {
		dev_dbg(ctrl->device,
			"%s: qid %d controller response\n",
			__func__, chap->qid);
		ret = nvme_auth_dhchap_ctrl_response(ctrl, chap);
		if (ret)
			goto fail2;
	}

	ret = nvme_auth_process_dhchap_success1(ctrl, chap);
	if (ret < 0) {
		/* Controller authentication failed */
		goto fail2;
	}

	/* DH-HMAC-CHAP Step 5: send success2 */
	dev_dbg(ctrl->device, "%s: qid %d send success2\n",
		__func__, chap->qid);
	tl = nvme_auth_set_dhchap_success2_data(ctrl, chap);
	ret = nvme_auth_send(ctrl, chap->qid, chap->buf, tl);
	if (!ret) {
		chap->error = 0;
		return;
	}

fail2:
	dev_dbg(ctrl->device, "%s: qid %d send failure2, status %x\n",
		__func__, chap->qid, chap->status);
	tl = nvme_auth_set_dhchap_failure2_data(ctrl, chap);
	ret = nvme_auth_send(ctrl, chap->qid, chap->buf, tl);
	if (!ret)
		ret = -EPROTO;
	chap->error = ret;
}

int nvme_auth_negotiate(struct nvme_ctrl *ctrl, int qid)
{
	struct nvme_dhchap_queue_context *chap;

	if (!ctrl->dhchap_key || !ctrl->dhchap_key_len) {
		dev_warn(ctrl->device, "qid %d: no key\n", qid);
		return -ENOKEY;
	}

	mutex_lock(&ctrl->dhchap_auth_mutex);
	/* Check if the context is already queued */
	list_for_each_entry(chap, &ctrl->dhchap_auth_list, entry) {
		if (chap->qid == qid) {
			mutex_unlock(&ctrl->dhchap_auth_mutex);
			queue_work(nvme_wq, &chap->auth_work);
			return 0;
		}
	}
	chap = kzalloc(sizeof(*chap), GFP_KERNEL);
	if (!chap) {
		mutex_unlock(&ctrl->dhchap_auth_mutex);
		return -ENOMEM;
	}
	chap->qid = qid;
	chap->ctrl = ctrl;

	/*
	 * Allocate a large enough buffer for the entire negotiation:
	 * 4k should be enough to ffdhe8192.
	 */
	chap->buf_size = 4096;
	chap->buf = kzalloc(chap->buf_size, GFP_KERNEL);
	if (!chap->buf) {
		mutex_unlock(&ctrl->dhchap_auth_mutex);
		kfree(chap);
		return -ENOMEM;
	}

	INIT_WORK(&chap->auth_work, __nvme_auth_work);
	list_add(&chap->entry, &ctrl->dhchap_auth_list);
	mutex_unlock(&ctrl->dhchap_auth_mutex);
	queue_work(nvme_wq, &chap->auth_work);
	return 0;
}
EXPORT_SYMBOL_GPL(nvme_auth_negotiate);

int nvme_auth_wait(struct nvme_ctrl *ctrl, int qid)
{
	struct nvme_dhchap_queue_context *chap;
	int ret;

	mutex_lock(&ctrl->dhchap_auth_mutex);
	list_for_each_entry(chap, &ctrl->dhchap_auth_list, entry) {
		if (chap->qid != qid)
			continue;
		mutex_unlock(&ctrl->dhchap_auth_mutex);
		flush_work(&chap->auth_work);
		ret = chap->error;
		nvme_auth_reset(chap);
		return ret;
	}
	mutex_unlock(&ctrl->dhchap_auth_mutex);
	return -ENXIO;
}
EXPORT_SYMBOL_GPL(nvme_auth_wait);

/* Assumes that the controller is in state RESETTING */
static void nvme_dhchap_auth_work(struct work_struct *work)
{
	struct nvme_ctrl *ctrl =
		container_of(work, struct nvme_ctrl, dhchap_auth_work);
	int ret, q;

	nvme_stop_queues(ctrl);
	/* Authenticate admin queue first */
	ret = nvme_auth_negotiate(ctrl, NVME_QID_ANY);
	if (ret) {
		dev_warn(ctrl->device,
			 "qid 0: error %d setting up authentication\n", ret);
		goto out;
	}
	ret = nvme_auth_wait(ctrl, NVME_QID_ANY);
	if (ret) {
		dev_warn(ctrl->device,
			 "qid 0: authentication failed\n");
		goto out;
	}
	dev_info(ctrl->device, "qid 0: authenticated\n");

	for (q = 1; q < ctrl->queue_count; q++) {
		ret = nvme_auth_negotiate(ctrl, q);
		if (ret) {
			dev_warn(ctrl->device,
				 "qid %d: error %d setting up authentication\n",
				 q, ret);
			goto out;
		}
	}
out:
	/*
	 * Failure is a soft-state; credentials remain valid until
	 * the controller terminates the connection.
	 */
	if (nvme_change_ctrl_state(ctrl, NVME_CTRL_LIVE))
		nvme_start_queues(ctrl);
}

void nvme_auth_init_ctrl(struct nvme_ctrl *ctrl)
{
	INIT_LIST_HEAD(&ctrl->dhchap_auth_list);
	INIT_WORK(&ctrl->dhchap_auth_work, nvme_dhchap_auth_work);
	mutex_init(&ctrl->dhchap_auth_mutex);
	nvme_auth_generate_key(ctrl);
}
EXPORT_SYMBOL_GPL(nvme_auth_init_ctrl);

void nvme_auth_stop(struct nvme_ctrl *ctrl)
{
	struct nvme_dhchap_queue_context *chap = NULL, *tmp;

	cancel_work_sync(&ctrl->dhchap_auth_work);
	mutex_lock(&ctrl->dhchap_auth_mutex);
	list_for_each_entry_safe(chap, tmp, &ctrl->dhchap_auth_list, entry)
		cancel_work_sync(&chap->auth_work);
	mutex_unlock(&ctrl->dhchap_auth_mutex);
}
EXPORT_SYMBOL_GPL(nvme_auth_stop);

void nvme_auth_free(struct nvme_ctrl *ctrl)
{
	struct nvme_dhchap_queue_context *chap = NULL, *tmp;

	mutex_lock(&ctrl->dhchap_auth_mutex);
	list_for_each_entry_safe(chap, tmp, &ctrl->dhchap_auth_list, entry) {
		list_del_init(&chap->entry);
		flush_work(&chap->auth_work);
		__nvme_auth_free(chap);
	}
	mutex_unlock(&ctrl->dhchap_auth_mutex);
	kfree(ctrl->dhchap_key);
	ctrl->dhchap_key = NULL;
	ctrl->dhchap_key_len = 0;
}
EXPORT_SYMBOL_GPL(nvme_auth_free);
