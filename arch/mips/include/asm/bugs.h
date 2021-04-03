/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This is included by init/main.c to check for architecture-dependent bugs.
 *
 * Copyright (C) 2007  Maciej W. Rozycki
 *
 * Needs:
 *	void check_bugs(void);
 */
#ifndef _ASM_BUGS_H
#define _ASM_BUGS_H

#include <linux/bug.h>
#include <linux/delay.h>
#include <linux/smp.h>

#include <asm/cpu.h>
#include <asm/cpu-info.h>
#include <asm/mips-boards/launch.h>

extern int daddiu_bug;

extern void check_bugs64_early(void);

extern void check_bugs32(void);
extern void check_bugs64(void);

static inline void check_bugs_early(void)
{
	if (IS_ENABLED(CONFIG_CPU_R4X00_BUGS64))
		check_bugs64_early();
}

static inline void check_bugs(void)
{
	unsigned int cpu = smp_processor_id();

	cpu_data[cpu].udelay_val = loops_per_jiffy;
	check_bugs32();

	if (IS_ENABLED(CONFIG_CPU_R4X00_BUGS64))
		check_bugs64();
}

static inline int r4k_daddiu_bug(void)
{
	if (!IS_ENABLED(CONFIG_CPU_R4X00_BUGS64))
		return 0;

	WARN_ON(daddiu_bug < 0);
	return daddiu_bug != 0;
}

static inline void cm_gcr_pcores_bug(unsigned int *ncores)
{
	struct cpulaunch *launch;

	if (!IS_ENABLED(CONFIG_SOC_MT7621) || !ncores)
		return;

	/*
	 * Ralink MT7621S SoC is single core, but GCR_CONFIG always reports 2 cores.
	 * Use legacy amon method to detect if the second core is missing.
	 */
	launch = (struct cpulaunch *)CKSEG0ADDR(CPULAUNCH);
	launch += 2; /* MT7621 has 2 VPEs per core */
	if (!(launch->flags & LAUNCH_FREADY))
		*ncores = 1;
}

#endif /* _ASM_BUGS_H */
