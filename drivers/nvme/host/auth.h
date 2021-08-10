/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 Hannes Reinecke, SUSE Software Solutions
 */

#ifndef _NVME_AUTH_H
#define _NVME_AUTH_H

#include <crypto/kpp.h>

const char *nvme_auth_dhgroup_name(int dhgroup_id);
int nvme_auth_dhgroup_pubkey_size(int dhgroup_id);
int nvme_auth_dhgroup_privkey_size(int dhgroup_id);
const char *nvme_auth_dhgroup_kpp(int dhgroup_id);
int nvme_auth_dhgroup_id(const char *dhgroup_name);

const char *nvme_auth_hmac_name(int hmac_id);
const char *nvme_auth_digest_name(int hmac_id);
int nvme_auth_hmac_id(const char *hmac_name);

unsigned char *nvme_auth_extract_secret(unsigned char *dhchap_secret,
					size_t *dhchap_key_len);
u8 *nvme_auth_transform_key(u8 *key, size_t key_len, u8 key_hash, char *nqn);
int nvme_auth_augmented_challenge(u8 hmac_id, u8 *skey, size_t skey_len,
				  u8 *challenge, u8 *aug, size_t hlen);
int nvme_auth_gen_privkey(struct crypto_kpp *dh_tfm, int dh_gid);
int nvme_auth_gen_pubkey(struct crypto_kpp *dh_tfm,
			 u8 *host_key, size_t host_key_len);
int nvme_auth_gen_shared_secret(struct crypto_kpp *dh_tfm,
				u8 *ctrl_key, size_t ctrl_key_len,
				u8 *sess_key, size_t sess_key_len);

#endif /* _NVME_AUTH_H */
