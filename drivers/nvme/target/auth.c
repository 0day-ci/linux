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
			pr_err("DH-HMAC-CHAP hash %s unsupported\n", hmac);
			host->dhchap_key_hash = -1;
			return -ENOTSUPP;
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
		host->dhchap_hash_id = NVME_AUTH_DHCHAP_SHA256;

	pr_debug("Using hash %s\n",
		 nvme_auth_hmac_name(host->dhchap_hash_id));
	return 0;
}

int nvmet_setup_auth(struct nvmet_ctrl *ctrl)
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
	if (ctrl->shash_tfm &&
	    host->dhchap_hash_id == ctrl->shash_id) {
		pr_debug("Re-use existing hash ID %d\n",
			 ctrl->shash_id);
		ret = 0;
		goto out_unlock;
	}
	hash_name = nvme_auth_hmac_name(host->dhchap_hash_id);
	if (!hash_name) {
		pr_warn("Hash ID %d invalid\n", host->dhchap_hash_id);
		ret = -EINVAL;
		goto out_unlock;
	}
	ctrl->shash_tfm = crypto_alloc_shash(hash_name, 0,
					     CRYPTO_ALG_ALLOCATES_MEMORY);
	if (IS_ERR(ctrl->shash_tfm)) {
		pr_err("failed to allocate shash %s\n", hash_name);
		ret = PTR_ERR(ctrl->shash_tfm);
		ctrl->shash_tfm = NULL;
		goto out_unlock;
	}
	ctrl->shash_id = host->dhchap_hash_id;

	/* Skip the 'DHHC-1:XX:' prefix */
	ctrl->dhchap_key = nvme_auth_extract_secret(host->dhchap_secret + 10,
						    &ctrl->dhchap_key_len);
	if (IS_ERR(ctrl->dhchap_key)) {
		pr_debug("failed to extract host key, error %d\n", ret);
		ret = PTR_ERR(ctrl->dhchap_key);
		ctrl->dhchap_key = NULL;
		goto out_free_hash;
	}
	pr_debug("%s: using key %*ph\n", __func__,
		 (int)ctrl->dhchap_key_len, ctrl->dhchap_key);
out_free_hash:
	if (ret) {
		if (ctrl->dhchap_key) {
			kfree_sensitive(ctrl->dhchap_key);
			ctrl->dhchap_key = NULL;
		}
		crypto_free_shash(ctrl->shash_tfm);
		ctrl->shash_tfm = NULL;
		ctrl->shash_id = 0;
	}
out_unlock:
	up_read(&nvmet_config_sem);

	return ret;
}

void nvmet_auth_sq_free(struct nvmet_sq *sq)
{
	kfree(sq->dhchap_c1);
	sq->dhchap_c1 = NULL;
	kfree(sq->dhchap_c2);
	sq->dhchap_c2 = NULL;
	kfree(sq->dhchap_skey);
	sq->dhchap_skey = NULL;
}

void nvmet_destroy_auth(struct nvmet_ctrl *ctrl)
{
	if (ctrl->shash_tfm) {
		crypto_free_shash(ctrl->shash_tfm);
		ctrl->shash_tfm = NULL;
		ctrl->shash_id = 0;
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
	u8 *challenge = req->sq->dhchap_c1, *host_response;
	u8 buf[4];
	int ret;

	host_response = nvme_auth_transform_key(ctrl->dhchap_key,
				shash_len, ctrl->shash_id,
				ctrl->hostnqn);
	if (IS_ERR(host_response))
		return PTR_ERR(host_response);

	ret = crypto_shash_setkey(ctrl->shash_tfm, host_response, shash_len);
	if (ret) {
		kfree_sensitive(host_response);
		return ret;
	}
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
	kfree_sensitive(host_response);
	return 0;
}

int nvmet_auth_ctrl_hash(struct nvmet_req *req, u8 *response,
			 unsigned int shash_len)
{
	struct nvmet_ctrl *ctrl = req->sq->ctrl;
	SHASH_DESC_ON_STACK(shash, ctrl->shash_tfm);
	u8 *challenge = req->sq->dhchap_c2, *ctrl_response;
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

	ctrl_response = nvme_auth_transform_key(ctrl->dhchap_key,
				shash_len, ctrl->shash_id,
				ctrl->subsysnqn);
	if (IS_ERR(ctrl_response))
		return PTR_ERR(ctrl_response);

	ret = crypto_shash_setkey(ctrl->shash_tfm, ctrl_response, shash_len);
	if (ret) {
		kfree_sensitive(ctrl_response);
		return ret;
	}
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
	kfree_sensitive(ctrl_response);
	return 0;
}
