/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
 *
 * Authors: Cheng Xu <chengyou@linux.alibaba.com>
 *          Kai Shen <kaishen@linux.alibaba.com>
 * Copyright (c) 2020-2021, Alibaba Group.
 */

#ifndef __ERDMA_DEBUG_H__
#define __ERDMA_DEBUG_H__

#include <linux/uaccess.h>

extern void erdma_debugfs_init(void);
extern void erdma_debugfs_add_one(struct erdma_dev *dev);
extern void erdma_debugfs_remove_one(struct erdma_dev *dev);
extern void erdma_debugfs_exit(void);

#endif
