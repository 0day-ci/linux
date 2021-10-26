/* SPDX-License-Identifier: GPL-2.0 */
#ifndef DIRTY_QUOTA_MIGRATION_H
#define DIRTY_QUOTA_MIGRATION_H
#include <linux/kvm.h>

struct vCPUDirtyQuotaContext {
	u64 dirty_counter;
	u64 dirty_quota;
};

#endif  /* DIRTY_QUOTA_MIGRATION_H */
