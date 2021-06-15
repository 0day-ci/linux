/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2021 Hisilicon Limited Permission is hereby granted, free of
 * charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Code herein communicates with and accesses proprietary hardware which is
 * licensed intellectual property (IP) belonging to Siemens Digital Industries
 * Software Ltd.
 *
 * Siemens Digital Industries Software Ltd. asserts and reserves all rights to
 * their intellectual property. This paragraph may not be removed or modified
 * in any way without permission from Siemens Digital Industries Software Ltd.
 */

#ifndef _ULTRASOC_SMB_H
#define _ULTRASOC_SMB_H

#include <linux/coresight.h>
#include <linux/list.h>
#include <linux/miscdevice.h>

#include "ultrasoc.h"

#define SMB_GLOBAL_CFG		0X0
#define SMB_GLOBAL_EN		0X4
#define SMB_GLOBAL_INT		0X8
#define SMB_LB_CFG_LO		0X40
#define SMB_LB_CFG_HI		0X44
#define SMB_LB_INT_CTRL		0X48
#define SMB_LB_INT_STS		0X4C
#define SMB_LB_BASE_LO		0X50
#define SMB_LB_BASE_HI		0X54
#define SMB_LB_LIMIT		0X58
#define SMB_LB_RD_ADDR		0X5C
#define SMB_LB_WR_ADDR		0X60
#define SMB_LB_PURGE		0X64

#define SMB_MSG_LC(lc)		((lc & 0x3) << 2)
#define SMB_BST_LEN(len)	(((len - 1) & 0xff) << 4)
/* idle message injection timer period */
#define SMB_IDLE_PRD(period)	(((period - 216) & 0xf) << 12)
#define SMB_MEM_WR(credit, rate) (((credit & 0x3) << 16) | ((rate & 0xf) << 18))
#define SMB_MEM_RD(credit, rate) (((credit & 0x3) << 22) | ((rate & 0xf) << 24))
#define HISI_SMB_GLOBAL_CFG                                                    \
	(SMB_MSG_LC(0) | SMB_IDLE_PRD(231) | SMB_MEM_WR(0x3, 0x0) |            \
	 SMB_MEM_RD(0x3, 0x6) | SMB_BST_LEN(16))

#define SMB_INT_ENABLE		BIT(0)
#define SMB_INT_TYPE_PULSE	BIT(1)
#define SMB_INT_POLARITY_HIGH	BIT(2)
#define HISI_SMB_GLB_INT_CFG	(SMB_INT_ENABLE | SMB_INT_TYPE_PULSE |         \
				SMB_INT_POLARITY_HIGH)

/* logic buffer config register low 32b */
#define SMB_BUF_ENABLE			BIT(0)
#define SMB_BUF_SINGLE_END		BIT(1)
#define SMB_BUF_INIT			BIT(8)
#define SMB_BUF_CONTINUOUS		BIT(11)
#define SMB_FLOW_MASK			GENMASK(19, 16)
#define SMB_BUF_CFG_STREAMING						       \
	(SMB_BUF_INIT | SMB_BUF_CONTINUOUS | SMB_FLOW_MASK)
#define SMB_BUF_WRITE_BASE		GENMASK(31, 0)

/* logic buffer config register high 32b */
#define SMB_MSG_FILTER(lower, upper)	((lower & 0xff) | ((upper & 0xff) << 8))
#define SMB_BUF_INT_ENABLE		BIT(0)
#define SMB_BUF_NOTE_NOT_EMPTY		BIT(8)
#define SMB_BUF_NOTE_BLOCK_AVAIL	BIT(9)
#define SMB_BUF_NOTE_TRIGGERED		BIT(10)
#define SMB_BUF_NOTE_FULL		BIT(11)
#define HISI_SMB_BUF_INT_CFG						\
	(SMB_BUF_INT_ENABLE | SMB_BUF_NOTE_NOT_EMPTY |			\
	   SMB_BUF_NOTE_BLOCK_AVAIL | SMB_BUF_NOTE_TRIGGERED |		\
	    SMB_BUF_NOTE_FULL)

struct smb_data_buffer {
	/* memory buffer for hardware write */
	u32 buf_cfg_mode;
	bool lost;
	void __iomem *buf_base;
	u64 buf_base_phys;
	u64 buf_size;
	u64 to_copy;
	u32 rd_offset;
};

struct smb_drv_data {
	void __iomem *base;
	struct device *dev;
	struct ultrasoc_com *com;
	struct smb_data_buffer smb_db;
	/* to register ultrasoc smb as a coresight sink device. */
	struct coresight_device	*csdev;
	spinlock_t		spinlock;
	local_t			reading;
	pid_t			pid;
	u32			mode;
	struct miscdevice miscdev;
};

#define SMB_MSG_ALIGH_SIZE 0x400

static inline struct smb_data_buffer *
	dev_get_smb_data_buffer(struct device *dev)
{
	struct smb_drv_data *drvdata = dev_get_drvdata(dev);

	if (drvdata)
		return &drvdata->smb_db;

	return NULL;
}

/*
 * Coresight doesn't export the following
 * structures(cs_mode,cs_buffers,etm_event_data),
 * so we redefine a copy here.
 */
enum cs_mode {
	CS_MODE_DISABLED,
	CS_MODE_SYSFS,
	CS_MODE_PERF,
};

struct cs_buffers {
	unsigned int		cur;
	unsigned int		nr_pages;
	unsigned long		offset;
	local_t			data_size;
	bool			snapshot;
	void			**data_pages;
};

struct etm_event_data {
	struct work_struct work;
	cpumask_t mask;
	void *snk_config;
	struct list_head * __percpu *path;
};

#if IS_ENABLED(CONFIG_CORESIGHT)
int etm_perf_symlink(struct coresight_device *csdev, bool link);
int etm_perf_add_symlink_sink(struct coresight_device *csdev);
void etm_perf_del_symlink_sink(struct coresight_device *csdev);
static inline void *etm_perf_sink_config(struct perf_output_handle *handle)
{
	struct etm_event_data *data = perf_get_aux(handle);

	if (data)
		return data->snk_config;
	return NULL;
}
#else
static inline int etm_perf_symlink(struct coresight_device *csdev, bool link)
{ return -EINVAL; }
int etm_perf_add_symlink_sink(struct coresight_device *csdev)
{ return -EINVAL; }
void etm_perf_del_symlink_sink(struct coresight_device *csdev) {}
static inline void *etm_perf_sink_config(struct perf_output_handle *handle)
{
	return NULL;
}

#endif /* CONFIG_CORESIGHT */

#endif
