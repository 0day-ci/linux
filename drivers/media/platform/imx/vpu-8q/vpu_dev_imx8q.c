// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020-2021 NXP
 */

#include <linux/init.h>
#include <linux/device.h>
#include <linux/ioctl.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include "vpu.h"
#include "vpu_imx8q.h"

int vpu_imx8q_setup_dec(struct vpu_dev *vpu)
{
	const off_t offset = DEC_MFD_XREG_SLV_BASE + MFD_BLK_CTRL;

	vpu_writel(vpu, offset + MFD_BLK_CTRL_MFD_SYS_CLOCK_ENABLE_SET, 0x1f);
	vpu_writel(vpu, offset + MFD_BLK_CTRL_MFD_SYS_RESET_SET, 0xffffffff);

	return 0;
}

int vpu_imx8q_setup_enc(struct vpu_dev *vpu)
{
	return 0;
}

int vpu_imx8q_setup(struct vpu_dev *vpu)
{
	const off_t offset = SCB_XREG_SLV_BASE + SCB_SCB_BLK_CTRL;
	u32 read_data;

	read_data = vpu_readl(vpu, offset + 0x108);

	vpu_writel(vpu, offset + SCB_BLK_CTRL_SCB_CLK_ENABLE_SET, 0x1);
	vpu_writel(vpu, offset + 0x190, 0xffffffff);
	vpu_writel(vpu, offset + SCB_BLK_CTRL_XMEM_RESET_SET, 0xffffffff);
	vpu_writel(vpu, offset + SCB_BLK_CTRL_SCB_CLK_ENABLE_SET, 0xE);
	vpu_writel(vpu, offset + SCB_BLK_CTRL_CACHE_RESET_SET, 0x7);
	vpu_writel(vpu, XMEM_CONTROL, 0x102);

	read_data = vpu_readl(vpu, offset + 0x108);

	return 0;
}

static int vpu_imx8q_reset_enc(struct vpu_dev *vpu)
{
	return 0;
}

static int vpu_imx8q_reset_dec(struct vpu_dev *vpu)
{
	const off_t offset = DEC_MFD_XREG_SLV_BASE + MFD_BLK_CTRL;

	vpu_writel(vpu, offset + MFD_BLK_CTRL_MFD_SYS_RESET_CLR, 0xffffffff);

	return 0;
}

int vpu_imx8q_reset(struct vpu_dev *vpu)
{
	const off_t offset = SCB_XREG_SLV_BASE + SCB_SCB_BLK_CTRL;

	vpu_writel(vpu, offset + SCB_BLK_CTRL_CACHE_RESET_CLR, 0x7);
	vpu_imx8q_reset_enc(vpu);
	vpu_imx8q_reset_dec(vpu);

	return 0;
}
