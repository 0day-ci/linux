// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021-2022, NVIDIA CORPORATION.  All rights reserved.
 */

#include <soc/tegra/mc.h>

#include <dt-bindings/memory/tegra234-mc.h>

#include "mc.h"

static const struct tegra_mc_client tegra234_mc_clients[] = {
	{
		.id = TEGRA234_MEMORY_CLIENT_SDMMCRAB,
		.name = "sdmmcrab",
		.sid = TEGRA234_SID_SDMMC4,
		.regs = {
			.sid = {
				.override = 0x318,
				.security = 0x31c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_SDMMCWAB,
		.name = "sdmmcwab",
		.sid = TEGRA234_SID_SDMMC4,
		.regs = {
			.sid = {
				.override = 0x338,
				.security = 0x33c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_BPMPR,
		.name = "bpmpr",
		.sid = TEGRA234_SID_BPMP,
		.regs = {
			.sid = {
				.override = 0x498,
				.security = 0x49c,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_BPMPW,
		.name = "bpmpw",
		.sid = TEGRA234_SID_BPMP,
		.regs = {
			.sid = {
				.override = 0x4a0,
				.security = 0x4a4,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_BPMPDMAR,
		.name = "bpmpdmar",
		.sid = TEGRA234_SID_BPMP,
		.regs = {
			.sid = {
				.override = 0x4a8,
				.security = 0x4ac,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_BPMPDMAW,
		.name = "bpmpdmaw",
		.sid = TEGRA234_SID_BPMP,
		.regs = {
			.sid = {
				.override = 0x4b0,
				.security = 0x4b4,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_APEDMAR,
		.name = "apedmar",
		.sid = TEGRA234_SID_APE,
		.regs = {
			.sid = {
				.override = 0x4f8,
				.security = 0x4fc,
			},
		},
	}, {
		.id = TEGRA234_MEMORY_CLIENT_APEDMAW,
		.name = "apedmaw",
		.sid = TEGRA234_SID_APE,
		.regs = {
			.sid = {
				.override = 0x500,
				.security = 0x504,
			},
		},
	},
};

static int tegra234_mc_get_channel(struct tegra_mc *mc, int *mc_channel)
{
	u32 g_intstatus;

	g_intstatus = mc_ch_readl(mc, MC_BROADCAST_CHANNEL,
				  MC_GLOBAL_INTSTATUS);

	switch (g_intstatus & mc->soc->int_channel_mask) {
	case BIT(8):
		*mc_channel = 0;
		break;

	case BIT(9):
		*mc_channel = 1;
		break;

	case BIT(10):
		*mc_channel = 2;
		break;

	case BIT(11):
		*mc_channel = 3;
		break;

	case BIT(12):
		*mc_channel = 4;
		break;

	case BIT(13):
		*mc_channel = 5;
		break;

	case BIT(14):
		*mc_channel = 6;
		break;

	case BIT(15):
		*mc_channel = 7;
		break;

	case BIT(25):
		*mc_channel = MC_BROADCAST_CHANNEL;
		break;

	default:
		pr_err("Unknown interrupt source\n");
		return -EINVAL;
	}

	return 0;
}

const struct tegra_mc_soc tegra234_mc_soc = {
	.num_clients = ARRAY_SIZE(tegra234_mc_clients),
	.clients = tegra234_mc_clients,
	.num_address_bits = 40,
	.num_channels = 16,
	.intmask = MC_INT_DECERR_ROUTE_SANITY |
		   MC_INT_DECERR_GENERALIZED_CARVEOUT | MC_INT_DECERR_MTS |
		   MC_INT_SECERR_SEC | MC_INT_DECERR_VPR |
		   MC_INT_SECURITY_VIOLATION | MC_INT_DECERR_EMEM,
	.has_addr_hi_reg = true,
	.ops = &tegra186_mc_ops,
	.int_channel_mask = 0x200ff00,
	.get_int_channel = tegra234_mc_get_channel,
};
