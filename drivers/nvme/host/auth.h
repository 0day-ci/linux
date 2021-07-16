/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 Hannes Reinecke, SUSE Software Solutions
 */

#ifndef _NVME_AUTH_H
#define _NVME_AUTH_H

const char *nvme_auth_dhgroup_name(int dhgroup_id);
int nvme_auth_dhgroup_pubkey_size(int dhgroup_id);
int nvme_auth_dhgroup_privkey_size(int dhgroup_id);
const char *nvme_auth_dhgroup_kpp(int dhgroup_id);
int nvme_auth_dhgroup_id(const char *dhgroup_name);

const char *nvme_auth_hmac_name(int hmac_id);
const char *nvme_auth_digest_name(int hmac_id);
int nvme_auth_hmac_id(const char *hmac_name);
int nvme_auth_hmac_len(int hmac_len);

unsigned char *nvme_auth_extract_secret(unsigned char *dhchap_secret,
					size_t *dhchap_key_len);

#endif /* _NVME_AUTH_H */
