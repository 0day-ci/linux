/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __TOOLS_LINUX__CACHE_H
#define __TOOLS_LINUX__CACHE_H

#ifndef CONFIG_SMP
#define CONFIG_SMP	1
#endif

#ifndef SMP_CACHE_BYTES
#define SMP_CACHE_BYTES	64
#endif

#ifndef ____cacheline_aligned
#define ____cacheline_aligned __attribute__((__aligned__(SMP_CACHE_BYTES)))
#endif

#ifndef ____cacheline_aligned_in_smp
#ifdef CONFIG_SMP
#define ____cacheline_aligned_in_smp ____cacheline_aligned
#else
#define ____cacheline_aligned_in_smp
#endif /* CONFIG_SMP */
#endif

#endif /* __LINUX_CACHE_H */
