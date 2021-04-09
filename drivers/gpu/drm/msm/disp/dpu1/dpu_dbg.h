/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 */

#ifndef DPU_DBG_H_
#define DPU_DBG_H_

#include <drm/drm_atomic_helper.h>
#include <drm/drm_device.h>
#include "../../../drm_crtc_internal.h"
#include <drm/drm_print.h>
#include <drm/drm_atomic.h>
#include <linux/debugfs.h>
#include <linux/list.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/ktime.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/dma-buf.h>
#include <linux/slab.h>
#include <linux/list_sort.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/kthread.h>
#include <linux/devcoredump.h>
#include <stdarg.h>
#include "dpu_hw_catalog.h"
#include "dpu_kms.h"
#include "dsi.h"

#define DPU_DBG_DUMP_DATA_LIMITER (NULL)

enum dpu_dbg_dump_flag {
	DPU_DBG_DUMP_IN_LOG = BIT(0),
	DPU_DBG_DUMP_IN_MEM = BIT(1),
	DPU_DBG_DUMP_IN_COREDUMP = BIT(2),
};

#define DPU_DBG_BASE_MAX		10

#define DEFAULT_PANIC		0
#define DEFAULT_REGDUMP		DPU_DBG_DUMP_IN_MEM
#define ROW_BYTES		16
#define RANGE_NAME_LEN		40
#define REG_BASE_NAME_LEN	80

/* debug option to print the registers in logs */
#define DPU_DBG_DUMP_IN_CONSOLE 0

/* print debug ranges in groups of 4 u32s */
#define REG_DUMP_ALIGN		16

struct dpu_mdp_regs {
	u32 **ctl;
	u32 **sspp;
	u32 *top;
	u32 **pp;
	u32 **intf;
	u32 **dspp;
};

/**
 * struct dpu_dbg_base - sde debug base structure
 * @evtlog: event log instance
 * @reg_base_list: list of register dumping regions
 * @dev: device pointer
 * @drm_dev: drm device pointer
 * @mutex: mutex to serialize access to serialze dumps, debugfs access
 * @dsi_ctrl_regs: array storing dsi controller registers
 * @dp_ctrl_regs: array storing dp controller registers
 * @mdp_regs: pointer to struct containing mdp register dump
 * @reg_dump_method: whether to dump registers into memory, kernel log, or both
 * @coredump_pending: coredump is pending read from userspace
 * @atomic_state: atomic state duplicated at the time of the error
 * @dump_worker: kworker thread which runs the dump work
 * @dump_work: kwork which dumps the registers and drm state
 * @timestamp: timestamp at which the coredump was captured
 * @dpu_dbg_printer: drm printer handle used to take drm snapshot
 */
struct dpu_dbg_base {
	struct device *dev;
	struct drm_device *drm_dev;
	struct mutex mutex;

	u32 **dsi_ctrl_regs;

	u32 *dp_ctrl_regs;

	struct dpu_mdp_regs *mdp_regs;

	char *blk_names[DPU_DBG_BASE_MAX];

	u32 reg_dump_method;

	bool coredump_pending;

	struct drm_atomic_state *atomic_state;

	struct kthread_worker *dump_worker;
	struct kthread_work dump_work;
	ktime_t timestamp;

	struct drm_printer *dpu_dbg_printer;
};

/**
 * DPU_DBG_DUMP - trigger dumping of all dpu_dbg facilities
 * @va_args:	list of named register dump ranges and regions to dump
 *              currently "mdp", "dsi" and "dp" are supported to dump
 *              mdp, dsi and dp register space respectively
 */
#define DPU_DBG_DUMP(drm_dev, ...) dpu_dbg_dump(drm_dev, __func__, \
		##__VA_ARGS__, DPU_DBG_DUMP_DATA_LIMITER)

/**
 * dpu_dbg_init - initialize global sde debug facilities: evtlog, regdump
 * @dev:		device handle
 * Returns:		0 or -ERROR
 */
int dpu_dbg_init(struct drm_device *drm_dev);

/**
 * dpu_dbg_destroy - destroy the global sde debug facilities
 * Returns:	none
 */
void dpu_dbg_destroy(struct drm_device *drm_dev);

/**
 * dpu_dbg_dump - trigger dumping of all dpu_dbg facilities
 * @name:	string indicating origin of dump
 * @va_args:	list of named register dump ranges and regions to dump
 *              currently "mdp", "dsi" and "dp" are supported to dump
 *              mdp, dsi and dp register space respectively
 *
 * Returns:	none
 */
void dpu_dbg_dump(struct drm_device *drm_dev, const char *name, ...);

/**
 * dpu_dbg_dump_regs - utility to store the register dumps in the specified memory
 * @reg:	memory where the registers need to be dumped
 * @len:	size of the register space which needs to be dumped
 * @base_addr:  base address of the module which needs to be dumped
 * @dump_op: op specifying if the dump needs to be in memory, in log or in coredump
 * @p: handle to drm_printer
 * Returns:	none
 */
void dpu_dbg_dump_regs(u32 **reg, u32 len, void __iomem *base_addr,
		u32 dump_op, struct drm_printer *p);

/**
 * dpu_dbg_get - get the handle to dpu_dbg struct from the drm device
 * @drm:	    handle to drm device

 * Returns:	handle to the dpu_dbg_base struct
 */
struct dpu_dbg_base *dpu_dbg_get(struct drm_device *drm);

/**
 * dpu_dbg_print_regs - print out the module registers to either log or drm printer
 * @drm:	    handle to drm device

 * Returns:	none
 */
void dpu_dbg_print_regs(struct drm_device *dev, u8 reg_dump_method);

/**
 * dpu_dbg_dump_blks - utility to dump out the registers as per their names
 * @dpu_dbg:	    handle to dpu_dbg_base struct

 * Returns:	none
 */
void dpu_dbg_dump_blks(struct dpu_dbg_base *dpu_dbg);

/**
 * dpu_dbg_init_blk_info - allocate memory for hw blocks based on hw catalog
 * @drm:	    handle to drm device

 * Returns:	none
 */
void dpu_dbg_init_blk_info(struct drm_device *dev);

/**
 * dpu_dbg_free_blk_mem - free the memory after the coredump has been read
 * @drm:	    handle to drm device

 * Returns: none
 */
void dpu_dbg_free_blk_mem(struct drm_device *drm_dev);

/**
 * dpu_dbg_is_drm_printer_needed - checks if a valid drm printer is needed for this dump type
 * @dpu_dbg:	handle to the dpu_dbg_base struct
 * Returns: none
 */
bool dpu_dbg_is_drm_printer_needed(struct dpu_dbg_base *dpu_dbg);

#endif /* DPU_DBG_H_ */
