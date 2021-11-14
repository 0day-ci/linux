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

#if (KVM_DIRTY_QUOTA_PAGE_OFFSET == 0)
/*
 * If KVM_DIRTY_QUOTA_PAGE_OFFSET is not defined by the arch, exclude
 * dirty_quota_migration.o by defining these nop functions for the arch.
 */
static inline int kvm_vcpu_dirty_quota_alloc(struct vCPUDirtyQuotaContext **vCPUdqctx)
{
	return 0;
}

static inline struct page *kvm_dirty_quota_context_get_page(
		struct vCPUDirtyQuotaContext *vCPUdqctx, u32 offset)
{
	return NULL;
}

static inline bool is_dirty_quota_full(struct vCPUDirtyQuotaContext *vCPUdqctx)
{
	return true;
}

#else /* KVM_DIRTY_QUOTA_PAGE_OFFSET == 0 */

int kvm_vcpu_dirty_quota_alloc(struct vCPUDirtyQuotaContext **vCPUdqctx);
struct page *kvm_dirty_quota_context_get_page(
		struct vCPUDirtyQuotaContext *vCPUdqctx, u32 offset);
bool is_dirty_quota_full(struct vCPUDirtyQuotaContext *vCPUdqctx);

#endif /* KVM_DIRTY_QUOTA_PAGE_OFFSET == 0 */

#endif  /* DIRTY_QUOTA_MIGRATION_H */
