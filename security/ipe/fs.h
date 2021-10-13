/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) Microsoft Corporation. All rights reserved.
 */

#ifndef IPE_FS_H
#define IPE_FS_H

void ipe_soft_del_policyfs(struct ipe_policy *p);
int ipe_new_policyfs_node(struct ipe_context *ctx, struct ipe_policy *p);
void ipe_del_policyfs_node(struct ipe_policy *p);

#endif /* IPE_FS_H */
