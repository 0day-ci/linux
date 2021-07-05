/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __TOOLS_LINUX_ASM_CACHE_H
#define __TOOLS_LINUX_ASM_CACHE_H

#include <generated/autoconf.h>

#if defined(__i386__) || defined(__x86_64__)
#define L1_CACHE_SHIFT	(CONFIG_X86_L1_CACHE_SHIFT)
#elif defined(__arm__)
#define L1_CACHE_SHIFT	(CONFIG_ARM_L1_CACHE_SHIFT)
#elif defined(__aarch64__)
#define L1_CACHE_SHIFT	(6)
#elif defined(__powerpc__)

/* bytes per L1 cache line */
#if defined(CONFIG_PPC_8xx)
#define L1_CACHE_SHIFT	4
#elif defined(CONFIG_PPC_E500MC)
#define L1_CACHE_SHIFT	6
#elif defined(CONFIG_PPC32)
#if defined(CONFIG_PPC_47x)
#define L1_CACHE_SHIFT	7
#else
#define L1_CACHE_SHIFT	5
#endif
#else /* CONFIG_PPC64 */
#define L1_CACHE_SHIFT	7
#endif

#elif defined(__sparc__)
#define L1_CACHE_SHIFT 5
#elif defined(__alpha__)

#if defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_EV6)
#define L1_CACHE_SHIFT	6
#else
/* Both EV4 and EV5 are write-through, read-allocate,
   direct-mapped, physical.
*/
#define L1_CACHE_SHIFT	5
#endif

#elif defined(__mips__)
#define L1_CACHE_SHIFT	CONFIG_MIPS_L1_CACHE_SHIFT
#elif defined(__ia64__)
#define L1_CACHE_SHIFT	CONFIG_IA64_L1_CACHE_SHIFT
#elif defined(__nds32__)
#define L1_CACHE_SHIFT	5
#else
#define L1_CACHE_SHIFT	5
#endif

#define L1_CACHE_BYTES	(1 << L1_CACHE_SHIFT)

#endif
