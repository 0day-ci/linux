/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) Microsoft Corporation. All rights reserved.
 */
#ifndef IPE_MODULES_H
#define IPE_MODULES_H

#include "ipe.h"
#include "ipe_module.h"

#include <linux/types.h>
#include <linux/rbtree.h>

const struct ipe_module *ipe_lookup_module(const char *key);
int ipe_register_module(struct ipe_module *m);

#endif /* IPE_MODULES_H */
