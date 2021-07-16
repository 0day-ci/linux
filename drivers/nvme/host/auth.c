// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Hannes Reinecke, SUSE Linux
 */

#include <linux/crc32.h>
#include <linux/base64.h>
#include <asm/unaligned.h>
#include <crypto/hash.h>
#include <crypto/kpp.h>
#include <crypto/dh.h>
#include <crypto/ffdhe.h>
#include <crypto/ecdh.h>
#include <crypto/curve25519.h>
#include "nvme.h"
#include "fabrics.h"
#include "auth.h"

static u32 nvme_dhchap_seqnum;

struct nvme_dhchap_context {
	struct crypto_shash *shash_tfm;
	struct crypto_shash *digest_tfm;
	struct crypto_kpp *dh_tfm;
	unsigned char *key;
	size_t key_len;
	int qid;
	u32 s1;
	u32 s2;
	u16 transaction;
	u8 status;
	u8 hash_id;
	u8 hash_len;
	u8 dhgroup_id;
	u16 dhgroup_size;
	u8 c1[64];
	u8 c2[64];
	u8 response[64];
	u8 *ctrl_key;
	int ctrl_key_len;
	u8 *host_key;
	int host_key_len;
	u8 *sess_key;
	int sess_key_len;
};

struct nvme_auth_dhgroup_map {
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
	{ .id = NVME_AUTH_DHCHAP_DHGROUP_ECDH,
	  .name = "ecdh", .kpp = "ecdh-nist-p256",
	  .privkey_size = 32, .pubkey_size = 64 },
	{ .id = NVME_AUTH_DHCHAP_DHGROUP_25519,
	  .name = "curve25519", .kpp = "curve25519",
	  .privkey_size = CURVE25519_KEY_SIZE,
	  .pubkey_size = CURVE25519_KEY_SIZE },
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

struct nvmet_dhchap_hash_map {
	int id;
	int hash_len;
	const char hmac[15];
	const char digest[15];
} hash_map[] = {
	{.id = NVME_AUTH_DHCHAP_HASH_SHA256,
	 .hash_len = 32,
	 .hmac = "hmac(sha256)", .digest = "sha256" },
	{.id = NVME_AUTH_DHCHAP_HASH_SHA384,
	 .hash_len = 48,
	 .hmac = "hmac(sha384)", .digest = "sha384" },
	{.id = NVME_AUTH_DHCHAP_HASH_SHA512,
	 .hash_len = 64,
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

int nvme_auth_hmac_len(int hmac_id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(hash_map); i++) {
		if (hash_map[i].id == hmac_id)
			return hash_map[i].hash_len;
	}
	return -1;
}
EXPORT_SYMBOL_GPL(nvme_auth_hmac_len);

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

unsigned char *nvme_auth_extract_secret(unsigned char *dhchap_secret,
					size_t *dhchap_key_len)
{
	unsigned char *dhchap_key;
	u32 crc;
	int key_len;
	size_t allocated_len;

	allocated_len = strlen(dhchap_secret) - 10;
	dhchap_key = kzalloc(allocated_len, GFP_KERNEL);
	if (!dhchap_key)
		return ERR_PTR(-ENOMEM);

	key_len = base64_decode(dhchap_secret + 10,
				allocated_len, dhchap_key);
	if (key_len != 36 && key_len != 52 &&
	    key_len != 68) {
		pr_debug("Invalid DH-HMAC-CHAP key len %d\n",
			 key_len);
		kfree(dhchap_key);
		return ERR_PTR(-EINVAL);
	}
	pr_debug("DH-HMAC-CHAP Key: %*ph\n",
		 (int)key_len, dhchap_key);

	/* The last four bytes is the CRC in little-endian format */
	key_len -= 4;
	/*
	 * The linux implementation doesn't do pre- and post-increments,
	 * so we have to do it manually.
	 */
	crc = ~crc32(~0, dhchap_key, key_len);

	if (get_unaligned_le32(dhchap_key + key_len) != crc) {
		pr_debug("DH-HMAC-CHAP crc mismatch (key %08x, crc %08x)\n",
		       get_unaligned_le32(dhchap_key + key_len), crc);
		kfree(dhchap_key);
		return ERR_PTR(-EKEYREJECTED);
	}
	*dhchap_key_len = key_len;
	return dhchap_key;
}
EXPORT_SYMBOL_GPL(nvme_auth_extract_secret);

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
	if (ret)
		dev_dbg(ctrl->device,
			"%s: qid %d error %d\n", __func__, qid, ret);
	return ret;
}

static int nvme_auth_receive(struct nvme_ctrl *ctrl, int qid,
			     void *buf, size_t al,
			     u16 transaction, u8 expected_msg )
{
	struct nvme_command cmd = {};
	struct nvmf_auth_dhchap_failure_data *data = buf;
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
	dev_dbg(ctrl->device, "%s: qid %d auth_type %d auth_id %x\n",
		__func__, qid, data->auth_type, data->auth_id);
	if (data->auth_type == NVME_AUTH_COMMON_MESSAGES &&
	    data->auth_id == NVME_AUTH_DHCHAP_MESSAGE_FAILURE1) {
		return data->reason_code_explanation;
	}
	if (data->auth_type != NVME_AUTH_DHCHAP_MESSAGES ||
	    data->auth_id != expected_msg) {
		dev_warn(ctrl->device,
			 "qid %d invalid message %02x/%02x\n",
			 qid, data->auth_type, data->auth_id);
		return NVME_AUTH_DHCHAP_FAILURE_INVALID_PAYLOAD;
	}
	if (le16_to_cpu(data->t_id) != transaction) {
		dev_warn(ctrl->device,
			 "qid %d invalid transaction ID %d\n",
			 qid, le16_to_cpu(data->t_id));
		return NVME_AUTH_DHCHAP_FAILURE_INVALID_PAYLOAD;
	}

	return 0;
}

static int nvme_auth_dhchap_negotiate(struct nvme_ctrl *ctrl,
				      struct nvme_dhchap_context *chap,
				      void *buf, size_t buf_size)
{
	struct nvmf_auth_dhchap_negotiate_data *data = buf;
	size_t size = sizeof(*data) + sizeof(union nvmf_auth_protocol);

	if (buf_size < size)
		return -EINVAL;

	memset((u8 *)buf, 0, size);
	data->auth_type = NVME_AUTH_COMMON_MESSAGES;
	data->auth_id = NVME_AUTH_DHCHAP_MESSAGE_NEGOTIATE;
	data->t_id = cpu_to_le16(chap->transaction);
	data->sc_c = 0; /* No secure channel concatenation */
	data->napd = 1;
	data->auth_protocol[0].dhchap.authid = NVME_AUTH_DHCHAP_AUTH_ID;
	data->auth_protocol[0].dhchap.halen = 3;
	data->auth_protocol[0].dhchap.dhlen = 8;
	data->auth_protocol[0].dhchap.idlist[0] = NVME_AUTH_DHCHAP_HASH_SHA256;
	data->auth_protocol[0].dhchap.idlist[1] = NVME_AUTH_DHCHAP_HASH_SHA384;
	data->auth_protocol[0].dhchap.idlist[2] = NVME_AUTH_DHCHAP_HASH_SHA512;
	data->auth_protocol[0].dhchap.idlist[3] = NVME_AUTH_DHCHAP_DHGROUP_NULL;
	data->auth_protocol[0].dhchap.idlist[4] = NVME_AUTH_DHCHAP_DHGROUP_2048;
	data->auth_protocol[0].dhchap.idlist[5] = NVME_AUTH_DHCHAP_DHGROUP_3072;
	data->auth_protocol[0].dhchap.idlist[6] = NVME_AUTH_DHCHAP_DHGROUP_4096;
	data->auth_protocol[0].dhchap.idlist[7] = NVME_AUTH_DHCHAP_DHGROUP_6144;
	data->auth_protocol[0].dhchap.idlist[8] = NVME_AUTH_DHCHAP_DHGROUP_8192;
	data->auth_protocol[0].dhchap.idlist[9] = NVME_AUTH_DHCHAP_DHGROUP_ECDH;
	data->auth_protocol[0].dhchap.idlist[10] = NVME_AUTH_DHCHAP_DHGROUP_25519;

	return size;
}

static int nvme_auth_dhchap_challenge(struct nvme_ctrl *ctrl,
				      struct nvme_dhchap_context *chap,
				      void *buf, size_t buf_size)
{
	struct nvmf_auth_dhchap_challenge_data *data = buf;
	size_t size = sizeof(*data) + data->hl + data->dhvlen;
	const char *gid_name;

	if (buf_size < size) {
		chap->status = NVME_AUTH_DHCHAP_FAILURE_INVALID_PAYLOAD;
		return -ENOMSG;
	}

	if (data->hashid != NVME_AUTH_DHCHAP_HASH_SHA256 &&
	    data->hashid != NVME_AUTH_DHCHAP_HASH_SHA384 &&
	    data->hashid != NVME_AUTH_DHCHAP_HASH_SHA512) {
		dev_warn(ctrl->device,
			 "qid %d: DH-HMAC-CHAP: invalid HASH ID %d\n",
			 chap->qid, data->hashid);
		chap->status = NVME_AUTH_DHCHAP_FAILURE_HASH_UNUSABLE;
		return -EPROTO;
	}
	gid_name = nvme_auth_dhgroup_kpp(data->dhgid);
	if (!gid_name) {
		dev_warn(ctrl->device,
			 "qid %d: DH-HMAC-CHAP: invalid DH group id %d\n",
			 chap->qid, data->dhgid);
		chap->status = NVME_AUTH_DHCHAP_FAILURE_DHGROUP_UNUSABLE;
		return -EPROTO;
	}
	if (data->dhgid != NVME_AUTH_DHCHAP_DHGROUP_NULL) {
		if (data->dhvlen == 0) {
			dev_warn(ctrl->device,
				 "qid %d: DH-HMAC-CHAP: empty DH value\n",
				 chap->qid);
			chap->status = NVME_AUTH_DHCHAP_FAILURE_DHGROUP_UNUSABLE;
			return -EPROTO;
		}
		chap->dh_tfm = crypto_alloc_kpp(gid_name, 0, 0);
		if (IS_ERR(chap->dh_tfm)) {
			dev_warn(ctrl->device,
				 "qid %d: DH-HMAC-CHAP: failed to initialize %s\n",
				 chap->qid, gid_name);
			chap->status = NVME_AUTH_DHCHAP_FAILURE_DHGROUP_UNUSABLE;
			chap->dh_tfm = NULL;
			return -EPROTO;
		}
		chap->dhgroup_id = data->dhgid;
	} else if (data->dhvlen != 0) {
		dev_warn(ctrl->device,
			 "qid %d: DH-HMAC-CHAP: invalid DH value for NULL DH\n",
			chap->qid);
		chap->status = NVME_AUTH_DHCHAP_FAILURE_DHGROUP_UNUSABLE;
		return -EPROTO;
	}
	dev_dbg(ctrl->device, "%s: qid %d requested hash id %d\n",
		__func__, chap->qid, data->hashid);
	if (nvme_auth_hmac_len(data->hashid) != data->hl) {
		dev_warn(ctrl->device,
			 "qid %d: DH-HMAC-CHAP: invalid hash length\n",
			chap->qid);
		chap->status = NVME_AUTH_DHCHAP_FAILURE_HASH_UNUSABLE;
		return -EPROTO;
	}
	chap->hash_id = data->hashid;
	chap->hash_len = data->hl;
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

static int nvme_auth_dhchap_reply(struct nvme_ctrl *ctrl,
				  struct nvme_dhchap_context *chap,
				  void *buf, size_t buf_size)
{
	struct nvmf_auth_dhchap_reply_data *data = buf;
	size_t size = sizeof(*data);

	size += 2 * chap->hash_len;
	if (ctrl->opts->dhchap_auth) {
		get_random_bytes(chap->c2, chap->hash_len);
		chap->s2 = nvme_dhchap_seqnum++;
	} else
		memset(chap->c2, 0, chap->hash_len);

	if (chap->host_key_len)
		size += chap->host_key_len;

	if (buf_size < size)
		return -EINVAL;

	memset(buf, 0, size);
	data->auth_type = NVME_AUTH_DHCHAP_MESSAGES;
	data->auth_id = NVME_AUTH_DHCHAP_MESSAGE_REPLY;
	data->t_id = cpu_to_le16(chap->transaction);
	data->hl = chap->hash_len;
	data->dhvlen = chap->host_key_len;
	data->seqnum = cpu_to_le32(chap->s2);
	memcpy(data->rval, chap->response, chap->hash_len);
	if (ctrl->opts->dhchap_auth) {
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

static int nvme_auth_dhchap_success1(struct nvme_ctrl *ctrl,
				     struct nvme_dhchap_context *chap,
				     void *buf, size_t buf_size)
{
	struct nvmf_auth_dhchap_success1_data *data = buf;
	size_t size = sizeof(*data);

	if (ctrl->opts->dhchap_auth)
		size += chap->hash_len;


	if (buf_size < size) {
		chap->status = NVME_AUTH_DHCHAP_FAILURE_INVALID_PAYLOAD;
		return -ENOMSG;
	}

	if (data->hl != chap->hash_len) {
		dev_warn(ctrl->device,
			 "qid %d: DH-HMAC-CHAP: invalid hash length %d\n",
			 chap->qid, data->hl);
		chap->status = NVME_AUTH_DHCHAP_FAILURE_HASH_UNUSABLE;
		return -EPROTO;
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
			 "qid %d: DH-HMAC-CHAP: controller authentication failed\n",
			 chap->qid);
		chap->status = NVME_AUTH_DHCHAP_FAILURE_INVALID_PAYLOAD;
		return -EPROTO;
	}
	dev_info(ctrl->device,
		 "qid %d: DH-HMAC-CHAP: controller authenticated\n",
		chap->qid);
	return 0;
}

static int nvme_auth_dhchap_success2(struct nvme_ctrl *ctrl,
				     struct nvme_dhchap_context *chap,
				     void *buf, size_t buf_size)
{
	struct nvmf_auth_dhchap_success2_data *data = buf;
	size_t size = sizeof(*data);

	memset(buf, 0, size);
	data->auth_type = NVME_AUTH_DHCHAP_MESSAGES;
	data->auth_id = NVME_AUTH_DHCHAP_MESSAGE_SUCCESS2;
	data->t_id = cpu_to_le16(chap->transaction);

	return size;
}

static int nvme_auth_dhchap_failure2(struct nvme_ctrl *ctrl,
				     struct nvme_dhchap_context *chap,
				     void *buf, size_t buf_size)
{
	struct nvmf_auth_dhchap_failure_data *data = buf;
	size_t size = sizeof(*data);

	memset(buf, 0, size);
	data->auth_type = NVME_AUTH_DHCHAP_MESSAGES;
	data->auth_id = NVME_AUTH_DHCHAP_MESSAGE_FAILURE2;
	data->t_id = cpu_to_le16(chap->transaction);
	data->reason_code = 1;
	data->reason_code_explanation = chap->status;

	return size;
}

int nvme_auth_select_hash(struct nvme_ctrl *ctrl,
			  struct nvme_dhchap_context *chap)
{
	const char *hash_name, *digest_name;
	int ret;

	hash_name = nvme_auth_hmac_name(chap->hash_id);
	if (!hash_name) {
		chap->status = NVME_AUTH_DHCHAP_FAILURE_NOT_USABLE;
		return -EPROTO;
	}
	chap->shash_tfm = crypto_alloc_shash(hash_name, 0,
					     CRYPTO_ALG_ALLOCATES_MEMORY);
	if (IS_ERR(chap->shash_tfm)) {
		chap->status = NVME_AUTH_DHCHAP_FAILURE_NOT_USABLE;
		chap->shash_tfm = NULL;
		return -EPROTO;
	}
	digest_name = nvme_auth_digest_name(chap->hash_id);
	if (!digest_name) {
		crypto_free_shash(chap->shash_tfm);
		chap->shash_tfm = NULL;
		return -EPROTO;
	}
	chap->digest_tfm = crypto_alloc_shash(digest_name, 0, 0);
	if (IS_ERR(chap->digest_tfm)) {
		chap->status = NVME_AUTH_DHCHAP_FAILURE_NOT_USABLE;
		crypto_free_shash(chap->shash_tfm);
		chap->shash_tfm = NULL;
		chap->digest_tfm = NULL;
		return -EPROTO;
	}
	if (!chap->key) {
		dev_warn(ctrl->device, "qid %d: cannot select hash, no key\n",
			 chap->qid);
		chap->status = NVME_AUTH_DHCHAP_FAILURE_NOT_USABLE;
		crypto_free_shash(chap->digest_tfm);
		crypto_free_shash(chap->shash_tfm);
		chap->shash_tfm = NULL;
		chap->digest_tfm = NULL;
		return -EINVAL;
	}
	ret = crypto_shash_setkey(chap->shash_tfm, chap->key, chap->key_len);
	if (ret) {
		chap->status = NVME_AUTH_DHCHAP_FAILURE_NOT_USABLE;
		crypto_free_shash(chap->digest_tfm);
		crypto_free_shash(chap->shash_tfm);
		chap->shash_tfm = NULL;
		chap->digest_tfm = NULL;
		return ret;
	}
	dev_dbg(ctrl->device, "qid %d: DH-HMAC_CHAP: selected hash %s\n",
		chap->qid, hash_name);
	return 0;
}

static int nvme_auth_augmented_challenge(struct nvme_dhchap_context *chap,
					 u8 *challenge, u8 *aug)
{
	struct crypto_shash *tfm;
	struct shash_desc *desc;
	u8 *hashed_key;
	const char *hash_name;
	int ret;

	hashed_key = kmalloc(chap->hash_len, GFP_KERNEL);
	if (!hashed_key)
		return -ENOMEM;

	ret = crypto_shash_tfm_digest(chap->digest_tfm, chap->sess_key,
				      chap->sess_key_len, hashed_key);
	if (ret < 0) {
		pr_debug("failed to hash session key, err %d\n", ret);
		kfree(hashed_key);
		return ret;
	}
	hash_name = crypto_shash_alg_name(chap->shash_tfm);
	if (!hash_name) {
		pr_debug("Invalid hash algoritm\n");
		return -EINVAL;
	}
	tfm = crypto_alloc_shash(hash_name, 0, 0);
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

	ret = crypto_shash_setkey(tfm, hashed_key, chap->hash_len);
	if (ret)
		goto out_free_desc;
	ret = crypto_shash_init(desc);
	if (ret)
		goto out_free_desc;
	crypto_shash_update(desc, challenge, chap->hash_len);
	crypto_shash_final(desc, aug);

out_free_desc:
	kfree_sensitive(desc);
out_free_hash:
	crypto_free_shash(tfm);
out_free_key:
	kfree(hashed_key);
	return ret;
}

static int nvme_auth_dhchap_host_response(struct nvme_ctrl *ctrl,
					  struct nvme_dhchap_context *chap)
{
	SHASH_DESC_ON_STACK(shash, chap->shash_tfm);
	u8 buf[4], *challenge = chap->c1;
	int ret;

	dev_dbg(ctrl->device, "%s: qid %d host response seq %d transaction %d\n",
		__func__, chap->qid, chap->s1, chap->transaction);
	if (chap->dh_tfm) {
		challenge = kmalloc(chap->hash_len, GFP_KERNEL);
		if (!challenge) {
			ret = -ENOMEM;
			goto out;
		}
		ret = nvme_auth_augmented_challenge(chap, chap->c1, challenge);
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
					  struct nvme_dhchap_context *chap)
{
	SHASH_DESC_ON_STACK(shash, chap->shash_tfm);
	u8 buf[4], *challenge = chap->c2;
	int ret;

	if (chap->dh_tfm) {
		challenge = kmalloc(chap->hash_len, GFP_KERNEL);
		if (!challenge) {
			ret = -ENOMEM;
			goto out;
		}
		ret = nvme_auth_augmented_challenge(chap, chap->c2,
						    challenge);
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

int nvme_auth_generate_key(struct nvme_ctrl *ctrl,
			   struct nvme_dhchap_context *chap)
{
	int ret;
	u8 key_hash;
	const char *hmac_name;
	struct crypto_shash *key_tfm;

	if (sscanf(ctrl->opts->dhchap_secret, "DHHC-1:%hhd:%*s:",
		   &key_hash) != 1)
		return -EINVAL;

	chap->key = nvme_auth_extract_secret(ctrl->opts->dhchap_secret,
					     &chap->key_len);
	if (IS_ERR(chap->key)) {
		ret = PTR_ERR(chap->key);
		chap->key = NULL;
		return ret;
	}

	if (key_hash == 0)
		return 0;

	hmac_name = nvme_auth_hmac_name(key_hash);
	if (!hmac_name) {
		pr_debug("Invalid key hash id %d\n", key_hash);
		return -EKEYREJECTED;
	}

	key_tfm = crypto_alloc_shash(hmac_name, 0, 0);
	if (IS_ERR(key_tfm)) {
		kfree(chap->key);
		chap->key = NULL;
		ret = PTR_ERR(key_tfm);
	} else {
		SHASH_DESC_ON_STACK(shash, key_tfm);

		shash->tfm = key_tfm;
		ret = crypto_shash_setkey(key_tfm, chap->key,
					  chap->key_len);
		if (ret < 0) {
			crypto_free_shash(key_tfm);
			kfree(chap->key);
			chap->key = NULL;
			return ret;
		}
		crypto_shash_init(shash);
		crypto_shash_update(shash, ctrl->opts->host->nqn,
				    strlen(ctrl->opts->host->nqn));
		crypto_shash_update(shash, "NVMe-over-Fabrics", 17);
		crypto_shash_final(shash, chap->key);
		crypto_free_shash(key_tfm);
	}
	return 0;
}

static int nvme_auth_dhchap_exponential(struct nvme_ctrl *ctrl,
					struct nvme_dhchap_context *chap)
{
	struct kpp_request *req;
	struct crypto_wait wait;
	struct scatterlist src, dst;
	u8 *pkey;
	int ret, pkey_len;

	if (chap->dhgroup_id == NVME_AUTH_DHCHAP_DHGROUP_2048 ||
	    chap->dhgroup_id == NVME_AUTH_DHCHAP_DHGROUP_3072 ||
	    chap->dhgroup_id == NVME_AUTH_DHCHAP_DHGROUP_4096 ||
	    chap->dhgroup_id == NVME_AUTH_DHCHAP_DHGROUP_6144 ||
	    chap->dhgroup_id == NVME_AUTH_DHCHAP_DHGROUP_8192) {
		struct dh p = {0};
		int pubkey_size = nvme_auth_dhgroup_pubkey_size(chap->dhgroup_id);

		ret = crypto_ffdhe_params(&p, pubkey_size << 3);
		if (ret) {
			dev_dbg(ctrl->device,
				"failed to generate ffdhe params, error %d\n",
				ret);
			return ret;
		}
		p.key = chap->key;
		p.key_size = chap->key_len;

		pkey_len = crypto_dh_key_len(&p);
		pkey = kzalloc(pkey_len, GFP_KERNEL);

		get_random_bytes(pkey, pkey_len);
		ret = crypto_dh_encode_key(pkey, pkey_len, &p);
		if (ret) {
			dev_dbg(ctrl->device,
				"failed to encode pkey, error %d\n", ret);
			kfree(pkey);
			return ret;
		}
		chap->host_key_len = pubkey_size;
		chap->sess_key_len = pubkey_size;
	} else if (chap->dhgroup_id == NVME_AUTH_DHCHAP_DHGROUP_ECDH) {
		struct ecdh p = {0};

		pkey_len = crypto_ecdh_key_len(&p);
		pkey = kzalloc(pkey_len, GFP_KERNEL);
		if (!pkey)
			return -ENOMEM;

		get_random_bytes(pkey, pkey_len);
		ret = crypto_ecdh_encode_key(pkey, pkey_len, &p);
		if (ret) {
			dev_dbg(ctrl->device,
				"failed to encode pkey, error %d\n", ret);
			kfree(pkey);
			return ret;
		}
		chap->host_key_len = 64;
		chap->sess_key_len = 32;
	} else if (chap->dhgroup_id == NVME_AUTH_DHCHAP_DHGROUP_25519) {
		pkey_len = CURVE25519_KEY_SIZE;
		pkey = kzalloc(pkey_len, GFP_KERNEL);
		if (!pkey)
			return -ENOMEM;
		get_random_bytes(pkey, pkey_len);
		chap->host_key_len = chap->sess_key_len = CURVE25519_KEY_SIZE;
	} else {
		dev_warn(ctrl->device, "Invalid DH group id %d\n",
			 chap->dhgroup_id);
		chap->status = NVME_AUTH_DHCHAP_FAILURE_INVALID_PAYLOAD;
		return -EINVAL;
	}

	ret = crypto_kpp_set_secret(chap->dh_tfm, pkey, pkey_len);
	if (ret) {
		dev_dbg(ctrl->dev, "failed to set secret, error %d\n", ret);
		kfree(pkey);
		return ret;
	}
	req = kpp_request_alloc(chap->dh_tfm, GFP_KERNEL);
	if (!req) {
		ret = -ENOMEM;
		goto out_free_exp;
	}

	chap->host_key = kzalloc(chap->host_key_len, GFP_KERNEL);
	if (!chap->host_key) {
		ret = -ENOMEM;
		goto out_free_req;
	}
	crypto_init_wait(&wait);
	kpp_request_set_input(req, NULL, 0);
	sg_init_one(&dst, chap->host_key, chap->host_key_len);
	kpp_request_set_output(req, &dst, chap->host_key_len);
	kpp_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				 crypto_req_done, &wait);

	ret = crypto_wait_req(crypto_kpp_generate_public_key(req), &wait);
	if (ret == -EOVERFLOW) {
		dev_dbg(ctrl->dev,
			"public key buffer too small, wants %d is %d\n",
			crypto_kpp_maxsize(chap->dh_tfm), chap->host_key_len);
		goto out_free_host;
	} else if (ret) {
		dev_dbg(ctrl->dev,
			"failed to generate public key, error %d\n", ret);
		goto out_free_host;
	}

	chap->sess_key = kmalloc(chap->sess_key_len, GFP_KERNEL);
	if (!chap->sess_key)
		goto out_free_host;

	crypto_init_wait(&wait);
	sg_init_one(&src, chap->ctrl_key, chap->ctrl_key_len);
	kpp_request_set_input(req, &src, chap->ctrl_key_len);
	sg_init_one(&dst, chap->sess_key, chap->sess_key_len);
	kpp_request_set_output(req, &dst, chap->sess_key_len);
	kpp_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				 crypto_req_done, &wait);

	ret = crypto_wait_req(crypto_kpp_compute_shared_secret(req), &wait);
	if (ret) {
		dev_dbg(ctrl->dev,
			"failed to generate shared secret, error %d\n", ret);
		kfree_sensitive(chap->sess_key);
		chap->sess_key = NULL;
		chap->sess_key_len = 0;
	} else
		dev_dbg(ctrl->dev, "shared secret %*ph\n",
			 (int)chap->sess_key_len, chap->sess_key);
out_free_host:
	if (ret) {
		kfree(chap->host_key);
		chap->host_key = NULL;
		chap->host_key_len = 0;
	}
out_free_req:
	kpp_request_free(req);
out_free_exp:
	kfree_sensitive(pkey);
	if (ret)
		chap->status = NVME_AUTH_DHCHAP_FAILURE_INVALID_PAYLOAD;
	return ret;
}

void nvme_auth_free(struct nvme_dhchap_context *chap)
{
	if (chap->shash_tfm)
		crypto_free_shash(chap->shash_tfm);
	if (chap->digest_tfm)
		crypto_free_shash(chap->digest_tfm);
	if (chap->dh_tfm)
		crypto_free_kpp(chap->dh_tfm);
	if (chap->key)
		kfree(chap->key);
	if (chap->ctrl_key)
		kfree(chap->ctrl_key);
	if (chap->host_key)
		kfree(chap->host_key);
	if (chap->sess_key)
		kfree(chap->sess_key);
	kfree(chap);
}

int nvme_auth_negotiate(struct nvme_ctrl *ctrl, int qid)
{
	struct nvme_dhchap_context *chap;
	void *buf;
	size_t buf_size, tl;
	int ret = 0;

	chap = kzalloc(sizeof(*chap), GFP_KERNEL);
	if (!chap)
		return -ENOMEM;
	chap->qid = qid;
	chap->transaction = ctrl->transaction++;

	ret = nvme_auth_generate_key(ctrl, chap);
	if (ret) {
		dev_dbg(ctrl->device, "%s: failed to generate key, error %d\n",
			__func__, ret);
		nvme_auth_free(chap);
		return ret;
	}

	/*
	 * Allocate a large enough buffer for the entire negotiation:
	 * 4k should be enough to ffdhe8192.
	 */
	buf_size = 4096;
	buf = kzalloc(buf_size, GFP_KERNEL);
	if (!buf) {
		ret = -ENOMEM;
		goto out;
	}

	/* DH-HMAC-CHAP Step 1: send negotiate */
	dev_dbg(ctrl->device, "%s: qid %d DH-HMAC-CHAP negotiate\n",
		__func__, qid);
	ret = nvme_auth_dhchap_negotiate(ctrl, chap, buf, buf_size);
	if (ret < 0)
		goto out;
	tl = ret;
	ret = nvme_auth_send(ctrl, qid, buf, tl);
	if (ret)
		goto out;

	memset(buf, 0, buf_size);
	ret = nvme_auth_receive(ctrl, qid, buf, buf_size, chap->transaction,
				NVME_AUTH_DHCHAP_MESSAGE_CHALLENGE);
	if (ret < 0) {
		dev_dbg(ctrl->device,
			"%s: qid %d DH-HMAC-CHAP failed to receive challenge\n",
			__func__, qid);
		goto out;
	}
	if (ret > 0) {
		chap->status = ret;
		goto fail1;
	}

	/* DH-HMAC-CHAP Step 2: receive challenge */
	dev_dbg(ctrl->device, "%s: qid %d DH-HMAC-CHAP challenge\n",
		__func__, qid);

	ret = nvme_auth_dhchap_challenge(ctrl, chap, buf, buf_size);
	if (ret) {
		/* Invalid parameters for negotiate */
		goto fail2;
	}

	dev_dbg(ctrl->device, "%s: qid %d DH-HMAC-CHAP select hash\n",
		__func__, qid);
	ret = nvme_auth_select_hash(ctrl, chap);
	if (ret)
		goto fail2;

	if (chap->ctrl_key_len) {
		dev_dbg(ctrl->device,
			"%s: qid %d DH-HMAC-DHAP DH exponential\n",
			__func__, qid);
		ret = nvme_auth_dhchap_exponential(ctrl, chap);
		if (ret)
			goto fail2;
	}

	dev_dbg(ctrl->device, "%s: qid %d DH-HMAC-CHAP host response\n",
		__func__, qid);
	ret = nvme_auth_dhchap_host_response(ctrl, chap);
	if (ret)
		goto fail2;

	/* DH-HMAC-CHAP Step 3: send reply */
	dev_dbg(ctrl->device, "%s: qid %d DH-HMAC-CHAP reply\n",
		__func__, qid);
	ret = nvme_auth_dhchap_reply(ctrl, chap, buf, buf_size);
	if (ret < 0)
		goto fail2;

	tl = ret;
	ret = nvme_auth_send(ctrl, qid, buf, tl);
	if (ret)
		goto fail2;

	memset(buf, 0, buf_size);
	ret = nvme_auth_receive(ctrl, qid, buf, buf_size, chap->transaction,
				NVME_AUTH_DHCHAP_MESSAGE_SUCCESS1);
	if (ret < 0) {
		dev_dbg(ctrl->device,
			"%s: qid %d DH-HMAC-CHAP failed to receive success1\n",
			__func__, qid);
		goto out;
	}
	if (ret > 0) {
		chap->status = ret;
		goto fail1;
	}

	if (ctrl->opts->dhchap_auth) {
		dev_dbg(ctrl->device,
			"%s: qid %d DH-HMAC-CHAP controller response\n",
			__func__, qid);
		ret = nvme_auth_dhchap_ctrl_response(ctrl, chap);
		if (ret)
			goto fail2;
	}

	/* DH-HMAC-CHAP Step 4: receive success1 */
	dev_dbg(ctrl->device, "%s: qid %d DH-HMAC-CHAP success1\n",
		__func__, qid);
	ret = nvme_auth_dhchap_success1(ctrl, chap, buf, buf_size);
	if (ret < 0) {
		/* Controller authentication failed */
		goto fail2;
	}
	tl = ret;
	/* DH-HMAC-CHAP Step 5: send success2 */
	dev_dbg(ctrl->device, "%s: qid %d DH-HMAC-CHAP success2\n",
		__func__, qid);
	tl = nvme_auth_dhchap_success2(ctrl, chap, buf, buf_size);
	ret = nvme_auth_send(ctrl, qid, buf, tl);
	if (!ret)
		goto out;

fail1:
	dev_dbg(ctrl->device, "%s: qid %d DH-HMAC-CHAP failure1, status %x\n",
		__func__, qid, chap->status);
	goto out;

fail2:
	dev_dbg(ctrl->device, "%s: qid %d DH-HMAC-CHAP failure2, status %x\n",
		__func__, qid, chap->status);
	tl = nvme_auth_dhchap_failure2(ctrl, chap, buf, buf_size);
	ret = nvme_auth_send(ctrl, qid, buf, tl);

out:
	if (!ret && chap->status)
		ret = -EPROTO;
	if (!ret) {
		ctrl->dhchap_hash = chap->hash_id;
		ctrl->dhchap_dhgroup = chap->dhgroup_id;
	}
	kfree(buf);
	nvme_auth_free(chap);
	return ret;
}
