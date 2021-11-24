/* SPDX-License-Identifier: GPL-2.0+ */
/*
*
* Zhouyi AI Accelerator driver
*
* Copyright (C) 2020 Arm (China) Ltd.
* Copyright (C) 2021 Cai Huoqing
*/

/**
 * @file zynpu.h
 * Header of the zynpu device struct
 */

#ifndef _ZYNPU_H_
#define _ZYNPU_H_

#include <linux/device.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include "zynpu_irq.h"
#include "zynpu_io.h"
#include "zynpu_job_manager.h"
#include "zynpu_mm.h"
#include "zhouyi.h"

#define ZYNPU_ERRCODE_NO_ERROR	   0
#define ZYNPU_ERRCODE_NO_MEMORY	  1
#define ZYNPU_ERRCODE_INTERNAL_NULLPTR   2
#define ZYNPU_ERRCODE_INVALID_ARGS       3
#define ZYNPU_ERRCODE_CREATE_KOBJ_ERR    4
#define ZYNPU_ERRCODE_ITEM_NOT_FOUND     5

#define IPUIOC_MAGIC 'A'

#define IPUIOC_QUERYCAP	  _IOR(IPUIOC_MAGIC,  0, struct zynpu_cap)
#define IPUIOC_REQBUF	    _IOWR(IPUIOC_MAGIC, 1, struct buf_request)
#define IPUIOC_RUNJOB	    _IOWR(IPUIOC_MAGIC, 2, struct user_job)
#define IPUIOC_FREEBUF	   _IOW(IPUIOC_MAGIC,  3, struct buf_desc)
#define IPUIOC_REQSHMMAP	 _IOR(IPUIOC_MAGIC,  4, __u64)
#define IPUIOC_REQIO	     _IOWR(IPUIOC_MAGIC, 5, struct zynpu_io_req)
#define IPUIOC_QUERYSTATUS       _IOWR(IPUIOC_MAGIC, 6, struct job_status_query)
#define IPUIOC_KILL_TIMEOUT_JOB  _IOW(IPUIOC_MAGIC,  7, __u32)

enum zynpu_version {
	ZYNPU_VERSION_ZHOUYI_V1 = 1,
	ZYNPU_VERSION_ZHOUYI_V2
};

/**
 * struct zynpu_core - a general struct describe a hardware ZYNPU core
 *
 * @version: ZYNPU hardware version
 * @freq_in_MHz: ZYNPU core working frequency
 * @max_sched_num: maximum number of jobs can be scheduled in pipeline
 * @base0: IO region of this ZYNPU core
 * @irq_obj: interrupt object of this core
 */
struct zynpu_core {
	int version;
	int freq_in_MHz;
	int max_sched_num;
	struct io_region *base0;
	struct zynpu_irq_object *irq_obj;
	struct device *dev;
};

/**
 * struct zynpu_io_operation - a struct contains ZYNPU hardware operation methods
 *
 * @enable_interrupt: Enable all ZYNPU interrupts
 * @disable_interrupt: Disable all ZYNPU interrupts
 * @trigger: trigger ZYNPU to run a job
 * @is_idle: Is ZYNPU hardware idle or not
 * @read_status_reg: Read status register value
 * @print_hw_id_info: Print ZYNPU version ID registers information
 * @query_capability: Query ZYNPU hardware capability information
 * @io_rw: Direct IO read/write operations
 */
struct zynpu_io_operation {
	void (*enable_interrupt)(struct zynpu_core* core);
	void (*disable_interrupt)(struct zynpu_core* core);
	int  (*trigger)(struct zynpu_core* core, struct user_job_desc* udesc, int tid);
	bool (*is_idle)(struct zynpu_core* core);
	int  (*read_status_reg)(struct zynpu_core* core);
	void (*print_hw_id_info)(struct zynpu_core* core);
	int  (*query_capability)(struct zynpu_core* core, struct zynpu_cap* cap);
	void (*io_rw)(struct zynpu_core* core, struct zynpu_io_req* io_req);
	int  (*upper_half)(void* data);
	void (*bottom_half)(void* data);
};

struct zynpu_priv {
	int board;
	int version;
	struct zynpu_core *core0;
	struct zynpu_io_operation* core_ctrl;
	int   open_num;
	struct device *dev;
	struct file_operations zynpu_fops;
	struct miscdevice *misc;
	struct mutex lock;
	struct zynpu_job_manager job_manager;
	struct zynpu_memory_manager mm;
	struct kobject *sys_kobj;
	int is_suspend;
};

/*
 * @brief register ZYNPU fops operations into fops struct
 *
 * @param fops: file_operations struct pointer
 *
 * @return ZYNPU_ERRCODE_NO_ERROR if successful; others if failed.
 */
int zynpu_fops_register(struct file_operations *fops);
/*
 * @brief initialize sysfs debug interfaces in probe
 *
 * @param zynpu_priv: zynpu_priv struct pointer
 *
 * @return 0 if successful; others if failed.
 */
int zynpu_create_sysfs(void *zynpu_priv);
/*
 * @brief de-initialize sysfs debug interfaces in remove
 *
 * @param zynpu_priv: zynpu_priv struct pointer
 */
void zynpu_destroy_sysfs(void *zynpu_priv);
/**
 * @brief initialize an input ZYNPU private data struct
 *
 * @param zynpu: pointer to ZYNPU private data struct
 * @param dev: device struct pointer
 *
 * @return 0 if successful; others if failed;
 */
int init_zynpu_priv(struct zynpu_priv *zynpu, struct device *dev);
/**
 * @brief initialize ZYNPU core info in the ZYNPU private data struct
 *
 * @param zynpu: pointer to ZYNPU private data struct
 * @param irqnum: ZYNPU interrupt number
 * @param base: ZYNPU external registers phsical base address
 * @param size: ZYNPU external registers address remap size
 *
 * @return 0 if successful; others if failed;
 */
int zynpu_priv_init_core(struct zynpu_priv *zynpu, int irqnum, u64 base, u64 size);
/**
 * @brief initialize the SoC info in the ZYNPU private data struct
 *
 * @param zynpu: pointer to ZYNPU private data struct
 * @param base: SoC registers phsical base address
 * @param size: SoC external registers address remap size
 *
 * @return 0 if successful; others if failed;
 */
int zynpu_priv_init_soc(struct zynpu_priv *zynpu, u64 base, u64 size);
/**
 * @brief add a reserved memory region into the ZYNPU private data struct
 *
 * @param zynpu: pointer to ZYNPU private data struct
 * @param base: memory region start physical address
 * @param size: memory region length size
 * @param type: ZYNPU memory type
 *
 * @return 0 if successful; others if failed;
 */
int zynpu_priv_add_mem_region(struct zynpu_priv *zynpu, u64 base, u64 size,
	enum zynpu_mem_type type);
/**
 * @brief get ZYNPU hardware version number wrapper
 *
 * @param zynpu: pointer to ZYNPU private data struct
 *
 * @return version
 */
int zynpu_priv_get_version(struct zynpu_priv *zynpu);
/**
 * @brief enable interrupt wrapper
 *
 * @param zynpu: pointer to ZYNPU private data struct
 *
 * @return void
 */
void zynpu_priv_enable_interrupt(struct zynpu_priv *zynpu);
/**
 * @brief disable interrupt wrapper
 *
 * @param zynpu: pointer to ZYNPU private data struct
 *
 * @return void
 */
void zynpu_priv_disable_interrupt(struct zynpu_priv *zynpu);
/**
 * @brief disable interrupt wrapper
 *
 * @param zynpu:  pointer to ZYNPU private data struct
 * @param udesc: descriptor of a job to be triggered on ZYNPU
 * @param tid:   user thread ID
 *
 * @return 0 if successful; others if failed;
 */
int zynpu_priv_trigger(struct zynpu_priv *zynpu, struct user_job_desc *udesc, int tid);
/**
 * @brief check if ZYNPU is idle wrapper
 *
 * @param zynpu: pointer to ZYNPU private data struct
 *
 * @return 1 if ZYNPU is in IDLE state
 */
bool zynpu_priv_is_idle(struct zynpu_priv *zynpu);
/**
 * @brief query ZYNPU capability wrapper
 *
 * @param zynpu: pointer to ZYNPU private data struct
 * @param cap:  pointer to the capability struct
 *
 * @return 0 if successful; others if failed;
 */
int zynpu_priv_query_capability(struct zynpu_priv *zynpu, struct zynpu_cap *cap);
/**
 * @brief ZYNPU external register read/write wrapper
 *
 * @param zynpu: pointer to ZYNPU private data struct
 * @param io_req:  pointer to the io_req struct
 *
 * @return void
 */
void zynpu_priv_io_rw(struct zynpu_priv *zynpu, struct zynpu_io_req *io_req);
/**
 * @brief print ZYNPU hardware ID information wrapper
 *
 * @param zynpu: pointer to ZYNPU private data struct
 *
 * @return void
 */
void zynpu_priv_print_hw_id_info(struct zynpu_priv *zynpu);
/**
 * @brief deinit an ZYNPU private data struct
 *
 * @param zynpu: pointer to ZYNPU private data struct
 *
 * @return 0 if successful; others if failed;
 */
int deinit_zynpu_priv(struct zynpu_priv *zynpu);

#endif /* _ZYNPU_H_ */