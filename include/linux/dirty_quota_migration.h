/* SPDX-License-Identifier: GPL-2.0 */
#ifndef DIRTY_QUOTA_MIGRATION_H
#define DIRTY_QUOTA_MIGRATION_H
#include <linux/kvm.h>

/**
 * vCPUDirtyQuotaContext:  dirty quota context of a vCPU
 *
 * @dirty_counter:	number of pages dirtied by the vCPU
 * @dirty_quota:	limit on the number of pages the vCPU can dirty
 */
struct vCPUDirtyQuotaContext {
	u64 dirty_counter;
	u64 dirty_quota;
};

#endif  /* DIRTY_QUOTA_MIGRATION_H */
