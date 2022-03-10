/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PKS_H
#define _LINUX_PKS_H

#ifdef CONFIG_ARCH_ENABLE_SUPERVISOR_PKEYS

#include <linux/types.h>

#include <uapi/asm-generic/mman-common.h>

void pks_update_protection(u8 pkey, u8 protection);
void pks_update_exception(struct pt_regs *regs, u8 pkey, u8 protection);

/**
 * pks_set_noaccess() - Disable all access to the domain
 * @pkey: the pkey for which the access should change.
 *
 * Disable all access to the domain specified by pkey.  This is not a global
 * update and only affects the current running thread.
 */
static inline void pks_set_noaccess(u8 pkey)
{
	pks_update_protection(pkey, PKEY_DISABLE_ACCESS);
}

/**
 * pks_set_readwrite() - Make the domain Read/Write
 * @pkey: the pkey for which the access should change.
 *
 * Allow all access, read and write, to the domain specified by pkey.  This is
 * not a global update and only affects the current running thread.
 */
static inline void pks_set_readwrite(u8 pkey)
{
	pks_update_protection(pkey, PKEY_READ_WRITE);
}

typedef bool (*pks_key_callback)(struct pt_regs *regs, unsigned long address,
				 bool write);

#else /* !CONFIG_ARCH_ENABLE_SUPERVISOR_PKEYS */

static inline void pks_set_noaccess(u8 pkey) {}
static inline void pks_set_readwrite(u8 pkey) {}
static inline void pks_update_exception(struct pt_regs *regs,
					u8 pkey,
					u8 protection)
{ }

#endif /* CONFIG_ARCH_ENABLE_SUPERVISOR_PKEYS */

#ifdef CONFIG_PKS_TEST

#define __static_or_pks_test

bool pks_test_fault_callback(struct pt_regs *regs, unsigned long address,
			     bool write);

#else /* !CONFIG_PKS_TEST */

#define __static_or_pks_test static

#endif /* CONFIG_PKS_TEST */

#endif /* _LINUX_PKS_H */
