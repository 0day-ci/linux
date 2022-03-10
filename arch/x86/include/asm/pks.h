/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_PKS_H
#define _ASM_X86_PKS_H

#ifdef CONFIG_ARCH_ENABLE_SUPERVISOR_PKEYS

void pks_setup(void);
void x86_pkrs_load(struct thread_struct *thread);

#else /* !CONFIG_ARCH_ENABLE_SUPERVISOR_PKEYS */

static inline void pks_setup(void) { }
static inline void x86_pkrs_load(struct thread_struct *thread) { }

#endif /* CONFIG_ARCH_ENABLE_SUPERVISOR_PKEYS */

#endif /* _ASM_X86_PKS_H */
