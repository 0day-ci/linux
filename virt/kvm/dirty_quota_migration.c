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

struct page *kvm_dirty_quota_context_get_page(
		struct vCPUDirtyQuotaContext *vCPUdqctx, u32 offset)
{
	return vmalloc_to_page((void *)vCPUdqctx + offset * PAGE_SIZE);
}
