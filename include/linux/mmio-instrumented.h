/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _LINUX_MMIO_INSTRUMENTED_H
#define _LINUX_MMIO_INSTRUMENTED_H

#include <linux/tracepoint-defs.h>

/*
 * Tracepoint and MMIO logging symbols should not be visible at EL2(HYP) as
 * there is no way to execute them and any such MMIO access from EL2 will
 * explode instantly (Words of Marc Zyngier). So introduce a generic flag
 * __DISABLE_TRACE_MMIO__ to disable MMIO tracing in nVHE and other drivers
 * if required.
 */
#if IS_ENABLED(CONFIG_TRACE_MMIO_ACCESS) && !(defined(__DISABLE_TRACE_MMIO__))
DECLARE_TRACEPOINT(rwmmio_write);
DECLARE_TRACEPOINT(rwmmio_read);

void log_write_mmio(const char *width, volatile void __iomem *addr);
void log_read_mmio(const char *width, const volatile void __iomem *addr);

#define __raw_write(v, a, _l)	({				\
	volatile void __iomem *_a = (a);			\
	if (tracepoint_enabled(rwmmio_write))			\
		log_write_mmio(__stringify(write##_l), _a);	\
	arch_raw_write##_l((v), _a);				\
	})

#define __raw_writeb(v, a)	__raw_write((v), a, b)
#define __raw_writew(v, a)	__raw_write((v), a, w)
#define __raw_writel(v, a)	__raw_write((v), a, l)
#define __raw_writeq(v, a)	__raw_write((v), a, q)

#define __raw_read(a, _l, _t)    ({				\
	_t __a;							\
	const volatile void __iomem *_a = (a);			\
	if (tracepoint_enabled(rwmmio_read))			\
		log_read_mmio(__stringify(read##_l), _a);	\
	__a = arch_raw_read##_l(_a);				\
	__a;							\
	})

#define __raw_readb(a)		__raw_read((a), b, u8)
#define __raw_readw(a)		__raw_read((a), w, u16)
#define __raw_readl(a)		__raw_read((a), l, u32)
#define __raw_readq(a)		__raw_read((a), q, u64)

#else

#define __raw_writeb(v, a)	arch_raw_writeb(v, a)
#define __raw_writew(v, a)	arch_raw_writew(v, a)
#define __raw_writel(v, a)	arch_raw_writel(v, a)
#define __raw_writeq(v, a)	arch_raw_writeq(v, a)

#define __raw_readb(a)		arch_raw_readb(a)
#define __raw_readw(a)		arch_raw_readw(a)
#define __raw_readl(a)		arch_raw_readl(a)
#define __raw_readq(a)		arch_raw_readq(a)

static inline void log_write_mmio(const char *width,
				  volatile void __iomem *addr) {}
static inline void log_read_mmio(const char *width,
				 const volatile void __iomem *addr) {}

#endif /* CONFIG_TRACE_MMIO_ACCESS */

#endif /* _LINUX_MMIO_INSTRUMENTED_H */
