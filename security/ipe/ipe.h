/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) Microsoft Corporation. All rights reserved.
 */

#ifndef IPE_H
#define IPE_H

#define pr_fmt(fmt) "IPE " fmt "\n"

#include "ctx.h"
#include "ipe_parser.h"
#include "modules/ipe_module.h"

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/blk_types.h>
#include <linux/lsm_hooks.h>

extern struct lsm_blob_sizes ipe_blobs;
extern struct ipe_parser __start_ipe_parsers[], __end_ipe_parsers[];
extern struct ipe_module __start_ipe_modules[], __end_ipe_modules[];

struct ipe_bdev *ipe_bdev(struct block_device *b);

#endif /* IPE_H */
