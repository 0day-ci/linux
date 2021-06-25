/* SPDX-License-Identifier: GPL-2.0 */
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
 *      Exported functions for digest list management.
 */

#ifndef __DIGEST_LISTS_H
#define __DIGEST_LISTS_H

#include <crypto/hash_info.h>
#include <uapi/linux/digest_lists.h>

int digest_get_info(u8 *digest, enum hash_algo algo, enum compact_types type,
		    u16 *modifiers, u8 *actions);
#endif
