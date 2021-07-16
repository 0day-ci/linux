// SPDX-License-Identifier: GPL-2.0
/*
 * NVMe over Fabrics DH-HMAC-CHAP authentication.
 * Copyright (c) 2020 Hannes Reinecke, SUSE Software Solutions.
 * All rights reserved.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <crypto/hash.h>
#include <crypto/kpp.h>
#include <crypto/dh.h>
#include <crypto/ffdhe.h>
#include <linux/crc32.h>
#include <linux/base64.h>
#include <linux/ctype.h>
#include <linux/random.h>
#include <asm/unaligned.h>

#include "nvmet.h"
#include "../host/auth.h"

int nvmet_auth_set_host_key(struct nvmet_host *host, const char *secret)
{
	if (sscanf(secret, "DHHC-1:%hhd:%*s", &host->dhchap_key_hash) != 1)
		return -EINVAL;
	if (host->dhchap_key_hash > 3) {
		pr_warn("Invalid DH-HMAC-CHAP hash id %d\n",
			 host->dhchap_key_hash);
		return -EINVAL;
	}
	if (host->dhchap_key_hash > 0) {
		/* Validate selected hash algorithm */
		const char *hmac = nvme_auth_hmac_name(host->dhchap_key_hash);

		if (!crypto_has_shash(hmac, 0, 0)) {
			pr_warn("DH-HMAC-CHAP hash %s unsupported\n", hmac);
			host->dhchap_key_hash = -1;
			return -EAGAIN;
		}
		/* Use this hash as default */
		if (!host->dhchap_hash_id)
			host->dhchap_hash_id = host->dhchap_key_hash;
	}
	host->dhchap_secret = kstrdup(secret, GFP_KERNEL);
	if (!host->dhchap_secret)
		return -ENOMEM;
	/* Default to SHA256 */
	if (!host->dhchap_hash_id)
		host->dhchap_hash_id = NVME_AUTH_DHCHAP_HASH_SHA256;

	pr_debug("Using hash %s\n",
		 nvme_auth_hmac_name(host->dhchap_hash_id));
	return 0;
}

int nvmet_setup_dhgroup(struct nvmet_ctrl *ctrl, int dhgroup_id)
{
	int ret = -ENOTSUPP;

	if (dhgroup_id == NVME_AUTH_DHCHAP_DHGROUP_NULL)
		return 0;

	return ret;
}

int nvmet_setup_auth(struct nvmet_ctrl *ctrl, struct nvmet_req *req)
{
	int ret = 0;
	struct nvmet_host_link *p;
	struct nvmet_host *host = NULL;
	const char *hash_name;

	down_read(&nvmet_config_sem);
	if (ctrl->subsys->type == NVME_NQN_DISC)
		goto out_unlock;

	list_for_each_entry(p, &ctrl->subsys->hosts, entry) {
		pr_debug("check %s\n", nvmet_host_name(p->host));
		if (strcmp(nvmet_host_name(p->host), ctrl->hostnqn))
			continue;
		host = p->host;
		break;
	}
	if (!host) {
		pr_debug("host %s not found\n", ctrl->hostnqn);
		ret = -EPERM;
		goto out_unlock;
	}
	if (!host->dhchap_secret) {
		pr_debug("No authentication provided\n");
		goto out_unlock;
	}

	hash_name = nvme_auth_hmac_name(host->dhchap_hash_id);
	if (!hash_name) {
		pr_debug("Hash ID %d invalid\n", host->dhchap_hash_id);
		ret = -EINVAL;
		goto out_unlock;
	}
	ctrl->shash_tfm = crypto_alloc_shash(hash_name, 0,
					     CRYPTO_ALG_ALLOCATES_MEMORY);
	if (IS_ERR(ctrl->shash_tfm)) {
		pr_debug("failed to allocate shash %s\n", hash_name);
		ret = PTR_ERR(ctrl->shash_tfm);
		ctrl->shash_tfm = NULL;
		goto out_unlock;
	}

	ctrl->dhchap_key = nvme_auth_extract_secret(host->dhchap_secret,
						    &ctrl->dhchap_key_len);
	if (IS_ERR(ctrl->dhchap_key)) {
		pr_debug("failed to extract host key, error %d\n", ret);
		ret = PTR_ERR(ctrl->dhchap_key);
		ctrl->dhchap_key = NULL;
		goto out_free_hash;
	}
	if (host->dhchap_key_hash) {
		struct crypto_shash *key_tfm;

		hash_name = nvme_auth_hmac_name(host->dhchap_key_hash);
		key_tfm = crypto_alloc_shash(hash_name, 0, 0);
		if (IS_ERR(key_tfm)) {
			ret = PTR_ERR(key_tfm);
			goto out_free_hash;
		} else {
			SHASH_DESC_ON_STACK(shash, key_tfm);

			shash->tfm = key_tfm;
			ret = crypto_shash_setkey(key_tfm, ctrl->dhchap_key,
						  ctrl->dhchap_key_len);
			crypto_shash_init(shash);
			crypto_shash_update(shash, ctrl->subsys->subsysnqn,
					    strlen(ctrl->subsys->subsysnqn));
			crypto_shash_update(shash, "NVMe-over-Fabrics", 17);
			crypto_shash_final(shash, ctrl->dhchap_key);
			crypto_free_shash(key_tfm);
		}
	}
	pr_debug("%s: using key %*ph\n", __func__,
		 (int)ctrl->dhchap_key_len, ctrl->dhchap_key);
	ret = crypto_shash_setkey(ctrl->shash_tfm, ctrl->dhchap_key,
				  ctrl->dhchap_key_len);
out_free_hash:
	if (ret) {
		if (ctrl->dhchap_key) {
			kfree(ctrl->dhchap_key);
			ctrl->dhchap_key = NULL;
		}
		crypto_free_shash(ctrl->shash_tfm);
		ctrl->shash_tfm = NULL;
	}
out_unlock:
	up_read(&nvmet_config_sem);

	return ret;
}

void nvmet_auth_sq_free(struct nvmet_sq *sq)
{
	if (sq->dhchap_c1)
		kfree(sq->dhchap_c1);
	if (sq->dhchap_c2)
		kfree(sq->dhchap_c2);
	if (sq->dhchap_skey)
		kfree(sq->dhchap_skey);
}

void nvmet_reset_auth(struct nvmet_ctrl *ctrl)
{
	if (ctrl->shash_tfm) {
		crypto_free_shash(ctrl->shash_tfm);
		ctrl->shash_tfm = NULL;
	}
	if (ctrl->dh_tfm) {
		crypto_free_kpp(ctrl->dh_tfm);
		ctrl->dh_tfm = NULL;
	}
	if (ctrl->dhchap_key) {
		kfree(ctrl->dhchap_key);
		ctrl->dhchap_key = NULL;
	}
}

bool nvmet_check_auth_status(struct nvmet_req *req)
{
	if (req->sq->ctrl->shash_tfm &&
	    !req->sq->authenticated)
		return false;
	return true;
}

int nvmet_auth_host_hash(struct nvmet_req *req, u8 *response,
			 unsigned int shash_len)
{
	struct nvmet_ctrl *ctrl = req->sq->ctrl;
	SHASH_DESC_ON_STACK(shash, ctrl->shash_tfm);
	u8 *challenge = req->sq->dhchap_c1;
	u8 buf[4];
	int ret;

	if (ctrl->dh_gid != NVME_AUTH_DHCHAP_DHGROUP_NULL) {
		ret = -ENOTSUPP;
		goto out;
	}

	shash->tfm = ctrl->shash_tfm;
	ret = crypto_shash_init(shash);
	if (ret)
		goto out;
	ret = crypto_shash_update(shash, challenge, shash_len);
	if (ret)
		goto out;
	put_unaligned_le32(req->sq->dhchap_s1, buf);
	ret = crypto_shash_update(shash, buf, 4);
	if (ret)
		goto out;
	put_unaligned_le16(req->sq->dhchap_tid, buf);
	ret = crypto_shash_update(shash, buf, 2);
	if (ret)
		goto out;
	memset(buf, 0, 4);
	ret = crypto_shash_update(shash, buf, 1);
	if (ret)
		goto out;
	ret = crypto_shash_update(shash, "HostHost", 8);
	if (ret)
		goto out;
	ret = crypto_shash_update(shash, ctrl->hostnqn, strlen(ctrl->hostnqn));
	if (ret)
		goto out;
	ret = crypto_shash_update(shash, buf, 1);
	if (ret)
		goto out;
	ret = crypto_shash_update(shash, ctrl->subsysnqn,
				  strlen(ctrl->subsysnqn));
	if (ret)
		goto out;
	ret = crypto_shash_final(shash, response);
out:
	if (challenge != req->sq->dhchap_c1)
		kfree(challenge);
	return 0;
}

int nvmet_auth_ctrl_hash(struct nvmet_req *req, u8 *response,
			 unsigned int shash_len)
{
	struct nvmet_ctrl *ctrl = req->sq->ctrl;
	SHASH_DESC_ON_STACK(shash, ctrl->shash_tfm);
	u8 *challenge = req->sq->dhchap_c2;
	u8 buf[4];
	int ret;

	pr_debug("%s: ctrl %d hash seq %d transaction %u\n", __func__,
		 ctrl->cntlid, req->sq->dhchap_s2, req->sq->dhchap_tid);
	pr_debug("%s: ctrl %d challenge %*ph\n", __func__,
		 ctrl->cntlid, shash_len, req->sq->dhchap_c2);
	pr_debug("%s: ctrl %d subsysnqn %s\n", __func__,
		 ctrl->cntlid, ctrl->subsysnqn);
	pr_debug("%s: ctrl %d hostnqn %s\n", __func__,
		 ctrl->cntlid, ctrl->hostnqn);

	if (ctrl->dh_gid != NVME_AUTH_DHCHAP_DHGROUP_NULL) {
		ret = -ENOTSUPP;
		goto out;
	}

	shash->tfm = ctrl->shash_tfm;
	ret = crypto_shash_init(shash);
	if (ret)
		goto out;
	ret = crypto_shash_update(shash, challenge, shash_len);
	if (ret)
		goto out;
	put_unaligned_le32(req->sq->dhchap_s2, buf);
	ret = crypto_shash_update(shash, buf, 4);
	if (ret)
		goto out;
	put_unaligned_le16(req->sq->dhchap_tid, buf);
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
	ret = crypto_shash_update(shash, ctrl->subsysnqn,
			    strlen(ctrl->subsysnqn));
	if (ret)
		goto out;
	ret = crypto_shash_update(shash, buf, 1);
	if (ret)
		goto out;
	ret = crypto_shash_update(shash, ctrl->hostnqn, strlen(ctrl->hostnqn));
	if (ret)
		goto out;
	ret = crypto_shash_final(shash, response);
out:
	if (challenge != req->sq->dhchap_c2)
		kfree(challenge);
	return 0;
}

int nvmet_auth_ctrl_sesskey(struct nvmet_req *req,
			    u8 *pkey, int pkey_size)
{
	struct nvmet_ctrl *ctrl = req->sq->ctrl;
	struct kpp_request *kpp_req;
	struct crypto_wait wait;
	struct scatterlist src, dst;
	int ret;

	req->sq->dhchap_skey_len =
		nvme_auth_dhgroup_privkey_size(ctrl->dh_gid);
	req->sq->dhchap_skey = kzalloc(req->sq->dhchap_skey_len, GFP_KERNEL);
	if (!req->sq->dhchap_skey)
		return -ENOMEM;
	kpp_req = kpp_request_alloc(ctrl->dh_tfm, GFP_KERNEL);
	if (!kpp_req) {
		kfree(req->sq->dhchap_skey);
		req->sq->dhchap_skey = NULL;
		return -ENOMEM;
	}

	pr_debug("%s: host public key %*ph\n", __func__,
		 (int)pkey_size, pkey);
	crypto_init_wait(&wait);
	sg_init_one(&src, pkey, pkey_size);
	kpp_request_set_input(kpp_req, &src, pkey_size);
	sg_init_one(&dst, req->sq->dhchap_skey,
		req->sq->dhchap_skey_len);
	kpp_request_set_output(kpp_req, &dst, req->sq->dhchap_skey_len);
	kpp_request_set_callback(kpp_req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				 crypto_req_done, &wait);

	ret = crypto_wait_req(crypto_kpp_compute_shared_secret(kpp_req), &wait);
	kpp_request_free(kpp_req);
	if (ret)
		pr_debug("failed to compute shared secred, err %d\n", ret);
	else
		pr_debug("%s: shared secret %*ph\n", __func__,
			 (int)req->sq->dhchap_skey_len,
			 req->sq->dhchap_skey);

	return ret;
}
