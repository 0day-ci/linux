/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PKS_H
#define _ASM_X86_PKS_H

#ifdef CONFIG_ARCH_ENABLE_SUPERVISOR_PKEYS

void pks_setup(void);
void pks_write_current(void);
void pks_save_pt_regs(struct pt_regs *regs);
void pks_restore_pt_regs(struct pt_regs *regs);
void pks_dump_fault_info(struct pt_regs *regs);
bool pks_handle_key_fault(struct pt_regs *regs, unsigned long hw_error_code,
			  unsigned long address);

#else /* !CONFIG_ARCH_ENABLE_SUPERVISOR_PKEYS */

static inline void pks_setup(void) { }
static inline void pks_write_current(void) { }
static inline void pks_save_pt_regs(struct pt_regs *regs) { }
static inline void pks_restore_pt_regs(struct pt_regs *regs) { }
static inline void pks_dump_fault_info(struct pt_regs *regs) { }

static inline bool pks_handle_key_fault(struct pt_regs *regs,
					unsigned long hw_error_code,
					unsigned long address)
{
	return false;
}

#endif /* CONFIG_ARCH_ENABLE_SUPERVISOR_PKEYS */


#ifdef CONFIG_PKS_TEST

bool pks_test_callback(struct pt_regs *regs);
#define __static_or_pks_test
bool pks_test_fault_callback(struct pt_regs *regs, unsigned long address,
			     bool write);

#else /* !CONFIG_PKS_TEST */

#define __static_or_pks_test static

static inline bool pks_test_callback(struct pt_regs *regs)
{
	return false;
}

#endif /* CONFIG_PKS_TEST */

#endif /* _ASM_X86_PKS_H */
