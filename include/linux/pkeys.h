/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PKEYS_H
#define _LINUX_PKEYS_H

#include <linux/mm.h>

#define ARCH_DEFAULT_PKEY	0

#ifdef CONFIG_ARCH_HAS_PKEYS
#include <asm/pkeys.h>
#else /* ! CONFIG_ARCH_HAS_PKEYS */
#define arch_max_pkey() (1)
#define execute_only_pkey(mm) (0)
#define arch_override_mprotect_pkey(vma, prot, pkey) (0)
#define PKEY_DEDICATED_EXECUTE_ONLY 0
#define ARCH_VM_PKEY_FLAGS 0

static inline int vma_pkey(struct vm_area_struct *vma)
{
	return 0;
}

static inline bool mm_pkey_is_allocated(struct mm_struct *mm, int pkey)
{
	return (pkey == 0);
}

static inline int mm_pkey_alloc(struct mm_struct *mm)
{
	return -1;
}

static inline int mm_pkey_free(struct mm_struct *mm, int pkey)
{
	return -EINVAL;
}

static inline int arch_set_user_pkey_access(struct task_struct *tsk, int pkey,
			unsigned long init_val)
{
	return 0;
}

static inline bool arch_pkeys_enabled(void)
{
	return false;
}

#endif /* ! CONFIG_ARCH_HAS_PKEYS */

#ifdef CONFIG_ARCH_ENABLE_SUPERVISOR_PKEYS

#include <linux/pks-keys.h>
#include <linux/types.h>

#include <uapi/asm-generic/mman-common.h>

bool pks_available(void);
void pks_update_protection(int pkey, u32 protection);
void pks_update_exception(struct pt_regs *regs, int pkey, u32 protection);

/**
 * pks_mk_noaccess() - Disable all access to the domain
 * @pkey: the pkey for which the access should change.
 *
 * Disable all access to the domain specified by pkey.  This is not a global
 * update and only affects the current running thread.
 */
static inline void pks_mk_noaccess(int pkey)
{
	pks_update_protection(pkey, PKEY_DISABLE_ACCESS);
}

/**
 * pks_mk_readwrite() - Make the domain Read/Write
 * @pkey: the pkey for which the access should change.
 *
 * Allow all access, read and write, to the domain specified by pkey.  This is
 * not a global update and only affects the current running thread.
 */
static inline void pks_mk_readwrite(int pkey)
{
	pks_update_protection(pkey, PKEY_READ_WRITE);
}

typedef bool (*pks_key_callback)(struct pt_regs *regs, unsigned long address,
				 bool write);

#else /* !CONFIG_ARCH_ENABLE_SUPERVISOR_PKEYS */

static inline bool pks_available(void)
{
	return false;
}

static inline void pks_mk_noaccess(int pkey) {}
static inline void pks_mk_readwrite(int pkey) {}
static inline void pks_update_exception(struct pt_regs *regs,
					int pkey,
					u32 protection)
{ }

#endif /* CONFIG_ARCH_ENABLE_SUPERVISOR_PKEYS */

#endif /* _LINUX_PKEYS_H */
