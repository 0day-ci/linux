/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2020-2021 NXP
 */

#ifndef _IMX_VPU_LOG_H
#define _IMX_VPU_LOG_H

#define LVL_ERR		(1 << 0)
#define LVL_WARN	(1 << 1)
#define LVL_INFO	(1 << 2)
#define LVL_DEBUG	(1 << 3)
#define LVL_IRQ		(1 << 4)
#define LVL_CMD		(1 << 5)
#define LVL_EVT		(1 << 6)
#define LVL_CTRL	(1 << 7)
#define LVL_TS		(1 << 8)
#define LVL_FLOW	(1 << 13)

extern unsigned int vpu_dbg_level;

#ifdef TAG
#define vpu_dbg(level, fmt, arg...) \
	do { \
		if ((vpu_dbg_level & (level)) || ((level) & LVL_ERR)) \
			pr_info("[VPU "TAG"]"fmt, ## arg); \
	} while (0)
#else
#define vpu_dbg(level, fmt, arg...) \
	do { \
		if ((vpu_dbg_level & (level)) || ((level) & LVL_ERR)) \
			pr_info("[VPU]"fmt, ## arg); \
	} while (0)
#endif

#define vpu_err(fmt, arg...)	vpu_dbg(LVL_ERR, fmt, ##arg)
#define inst_dbg(inst, level, fmt, arg...)		\
		vpu_dbg(level, "[%d:%d] "fmt, inst->core->id, inst->id, ## arg)
#define inst_err(inst, fmt, arg...)	inst_dbg(inst, LVL_ERR, fmt, ## arg)
#define core_dbg(core, level, fmt, arg...)		\
		vpu_dbg(level, "[%d] %s "fmt, core->id, vpu_core_type_desc(core->type), ## arg)
#define core_err(core, fmt, arg...)	core_dbg(core, LVL_ERR, fmt, ## arg)

#endif
