/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PKS_H
#define _ASM_X86_PKS_H

#ifdef CONFIG_ARCH_ENABLE_SUPERVISOR_PKEYS

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
