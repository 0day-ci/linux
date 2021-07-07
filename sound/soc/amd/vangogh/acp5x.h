/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * AMD ALSA SoC PCM Driver
 *
 * Copyright (C) 2021 Advanced Micro Devices, Inc. All rights reserved.
 */

#include "vg_chip_offset_byte.h"

#define ACP5x_PHY_BASE_ADDRESS 0x1240000
#define ACP_DEVICE_ID 0x15E2

static inline u32 acp_readl(void __iomem *base_addr)
{
	return readl(base_addr - ACP5x_PHY_BASE_ADDRESS);
}

static inline void acp_writel(u32 val, void __iomem *base_addr)
{
	writel(val, base_addr - ACP5x_PHY_BASE_ADDRESS);
}
