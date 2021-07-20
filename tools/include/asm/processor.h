/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __TOOLS_LINUX_ASM_PROCESSOR_H
#define __TOOLS_LINUX_ASM_PROCESSOR_H

#if defined(__i386__) || defined(__x86_64__)
/* REP NOP (PAUSE) is a good thing to insert into busy-wait loops. */
static __always_inline void rep_nop(void)
{
	asm volatile("rep; nop" ::: "memory");
}

static __always_inline void cpu_relax(void)
{
	rep_nop();
}
#elif defined(__aarch64__)
static inline void cpu_relax(void)
{
	asm volatile("yield" ::: "memory");
}
#else
#error "Architecture not supported"
#endif

#endif
