/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PKS_H
#define _ASM_X86_PKS_H

#include <asm/cpufeature.h>

#ifdef CONFIG_ARCH_ENABLE_SUPERVISOR_PKEYS

/**
 * pks_available() - Is PKS available on this system
 *
 * Return if PKS is currently supported and enabled on this system.
 */
static inline bool pks_available(void)
{
	return cpu_feature_enabled(X86_FEATURE_PKS);
}

void pks_setup(void);
void x86_pkrs_load(struct thread_struct *thread);
void pks_save_pt_regs(struct pt_regs *regs);
void pks_restore_pt_regs(struct pt_regs *regs);
void pks_show_regs(struct pt_regs *regs, const char *log_lvl);

bool pks_handle_key_fault(struct pt_regs *regs, unsigned long hw_error_code,
			  unsigned long address);

#else /* !CONFIG_ARCH_ENABLE_SUPERVISOR_PKEYS */

static inline void pks_setup(void) { }
static inline void x86_pkrs_load(struct thread_struct *thread) { }
static inline void pks_save_pt_regs(struct pt_regs *regs) { }
static inline void pks_restore_pt_regs(struct pt_regs *regs) { }
static inline void pks_show_regs(struct pt_regs *regs,
				 const char *log_lvl) { }

static inline bool pks_handle_key_fault(struct pt_regs *regs,
					unsigned long hw_error_code,
					unsigned long address)
{
	return false;
}

#endif /* CONFIG_ARCH_ENABLE_SUPERVISOR_PKEYS */

#endif /* _ASM_X86_PKS_H */
