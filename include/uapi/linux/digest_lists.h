/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (C) 2017-2021 Huawei Technologies Duesseldorf GmbH
 *
 * Author: Roberto Sassu <roberto.sassu@huawei.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 * File: digest_lists.h
 *      Digest list definitions exported to user space.
 */

#ifndef _UAPI__LINUX_DIGEST_LISTS_H
#define _UAPI__LINUX_DIGEST_LISTS_H

#include <linux/types.h>
#include <linux/hash_info.h>

enum compact_types { COMPACT_KEY, COMPACT_PARSER, COMPACT_FILE,
		     COMPACT_METADATA, COMPACT_DIGEST_LIST, COMPACT__LAST };

enum compact_modifiers { COMPACT_MOD_IMMUTABLE, COMPACT_MOD__LAST };

enum compact_actions { COMPACT_ACTION_IMA_MEASURED,
		       COMPACT_ACTION_IMA_APPRAISED,
		       COMPACT_ACTION_IMA_APPRAISED_DIGSIG,
		       COMPACT_ACTION__LAST };

enum ops { DIGEST_LIST_ADD, DIGEST_LIST_DEL, DIGEST_LIST_OP__LAST };

struct compact_list_hdr {
	__u8 version;
	__u8 _reserved;
	__le16 type;
	__le16 modifiers;
	__le16 algo;
	__le32 count;
	__le32 datalen;
} __packed;
#endif
