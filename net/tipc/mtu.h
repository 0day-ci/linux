/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2021 ZTE Corporation.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _TIPC_MTU_H
#define _TIPC_MTU_H

#include <linux/tipc.h>
#include "crypto.h"

#ifdef CONFIG_TIPC_CRYPTO
#define BUF_HEADROOM ALIGN(((LL_MAX_HEADER + 48) + EHDR_MAX_SIZE), 16)
#define BUF_TAILROOM (TIPC_AES_GCM_TAG_SIZE)
#define FB_MTU	(PAGE_SIZE - \
		 SKB_DATA_ALIGN(sizeof(struct skb_shared_info)) - \
		 SKB_DATA_ALIGN(BUF_HEADROOM + BUF_TAILROOM + 3))
#else
#define BUF_HEADROOM (LL_MAX_HEADER + 48)
#define BUF_TAILROOM 16
#define FB_MTU	(PAGE_SIZE - \
		 SKB_DATA_ALIGN(sizeof(struct skb_shared_info)) - \
		 SKB_DATA_ALIGN(BUF_HEADROOM + 3))
#endif

#endif
