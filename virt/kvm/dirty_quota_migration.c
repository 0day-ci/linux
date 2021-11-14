// SPDX-License-Identifier: GPL-2.0-only
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/dirty_quota_migration.h>

int kvm_vcpu_dirty_quota_alloc(struct vCPUDirtyQuotaContext **vCPUdqctx)
{
	u64 size = sizeof(struct vCPUDirtyQuotaContext);
	*vCPUdqctx = vmalloc(size);
	if (!(*vCPUdqctx))
		return -ENOMEM;
	memset((*vCPUdqctx), 0, size);
	return 0;
}
