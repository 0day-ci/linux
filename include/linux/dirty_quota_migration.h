/* SPDX-License-Identifier: GPL-2.0 */
#ifndef DIRTY_QUOTA_MIGRATION_H
#define DIRTY_QUOTA_MIGRATION_H
#include <linux/kvm.h>

#ifndef KVM_DIRTY_QUOTA_PAGE_OFFSET
#define KVM_DIRTY_QUOTA_PAGE_OFFSET 64
#endif

struct vCPUDirtyQuotaContext {
	u64 dirty_counter;
	u64 dirty_quota;
};

int kvm_vcpu_dirty_quota_alloc(struct vCPUDirtyQuotaContext **vCPUdqctx);
struct page *kvm_dirty_quota_context_get_page(
		struct vCPUDirtyQuotaContext *vCPUdqctx, u32 offset);

#endif  /* DIRTY_QUOTA_MIGRATION_H */
