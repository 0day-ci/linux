/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) Microsoft Corporation. All rights reserved.
 */

#ifndef IPE_H
#define IPE_H

#define pr_fmt(fmt) "IPE " fmt "\n"

#include "ctx.h"

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/lsm_hooks.h>

extern struct lsm_blob_sizes ipe_blobs;

#endif /* IPE_H */
