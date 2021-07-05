/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __TOOLS_LINUX_ASM_PROCESSOR_H
#define __TOOLS_LINUX_ASM_PROCESSOR_H

#include <pthread.h>

#if defined(__i386__) || defined(__x86_64__)
#include "../../arch/x86/include/asm/vdso/processor.h"
#elif defined(__arm__)
#include "../../arch/arm/include/asm/vdso/processor.h"
#elif defined(__aarch64__)
#include "../../arch/arm64/include/asm/vdso/processor.h"
#elif defined(__powerpc__)
#include "../../arch/powerpc/include/vdso/processor.h"
#elif defined(__s390__)
#include "../../arch/s390/include/vdso/processor.h"
#elif defined(__sh__)
#include "../../arch/sh/include/asm/processor.h"
#elif defined(__sparc__)
#include "../../arch/sparc/include/asm/processor.h"
#elif defined(__alpha__)
#include "../../arch/alpha/include/asm/processor.h"
#elif defined(__mips__)
#include "../../arch/mips/include/asm/vdso/processor.h"
#elif defined(__ia64__)
#include "../../arch/ia64/include/asm/processor.h"
#elif defined(__xtensa__)
#include "../../arch/xtensa/include/asm/processor.h"
#elif defined(__nds32__)
#include "../../arch/nds32/include/asm/processor.h"
#else
#define cpu_relax()	sched_yield()
#endif

#endif
