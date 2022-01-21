// SPDX-License-Identifier: GPL-2.0-only
/*
 * Bayhub Technologies, Inc. BH201 SDHCI bridge IC for
 * VENDOR SDHCI platform driver source file
 *
 * Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 */

struct sdhci_bht_host {
};

static void bht_signal_voltage_on_off(struct sdhci_host *host, u32 on_off)
{
}

static void sdhci_bht_parse(struct mmc_host *mmc_host)
{
}

static void sdhci_bht_resource_free(struct sdhci_msm_host *vendor_host)
{
}

static void mmc_rescan_bht(struct work_struct *work)
{
}

static int sdhci_bht_execute_tuning(struct mmc_host *mmc, u32 opcode)
{
	int ret = 0;

	return ret;
}

