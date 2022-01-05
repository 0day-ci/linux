// SPDX-License-Identifier: GPL-2.0-only
/*
 * Bayhub Technologies, Inc. BH201 SDHCI bridge IC for
 * VENDOR SDHCI platform driver source file
 *
 * Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 */

#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/delay.h>
#include <linux/mmc/mmc.h>
#include <linux/pm_runtime.h>
#include <linux/pm_opp.h>
#include <linux/slab.h>
#include <linux/iopoll.h>
#include <linux/regulator/consumer.h>
#include <linux/interconnect.h>
#include <linux/pinctrl/consumer.h>
#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/mmc/sd.h>
#include <linux/io.h>
#include <linux/of_gpio.h>
#include <linux/scatterlist.h>

#include "../core/sd_ops.h"
#include "../core/sdio_ops.h"
#include "../core/mmc_ops.h"
#include "../core/sd.h"
#include "../core/bus.h"
#include "../core/host.h"
#include "../core/card.h"
#include "../core/pwrseq.h"
#include "../core/core.h"

#include "sdhci-pltfm.h"
#include "cqhci.h"

#define	SD_FNC_AM_SDR50		0x2
#define	SD_FNC_AM_SDR104	0x3
#define BIT_PASS_MASK  (0x7ff)
#define SDR104_MANUAL_INJECT 0x3ff
#define SDR50_MANUAL_INJECT  0x77f

#define	TUNING_RING_IDX(x)  ((x) % TUNING_PHASE_SIZE)
#define	GET_IDX_VALUE(tb, x)  (tb & (1 << (x)))
#define	GENERATE_IDX_VALUE(x)  (1 << (x))
#define	GET_TUNING_RING_IDX_VALUE(tb, x) \
	(tb & (1 << TUNING_RING_IDX(x)))
#define	GENERATE_TUNING_RING_IDX_VALUE(x) \
	(1 << TUNING_RING_IDX(x))
#define	MAX_CFG_BIT_VAL (383)

/*
 * The code porting from sdhci-msm.c part-1 start
 * We need to porting the code from sdhci-msm.c for below reasons:
 * 1.GGC is a SD bus signal re-timing IC, has no vendor ID, need to work with sd host IC together
 * 2.Qualcomm engineer doesn’t allow us to add code into their source code file directly
 */
#define CORE_MCI_VERSION		0x50
#define CORE_VERSION_MAJOR_SHIFT	28
#define CORE_VERSION_MAJOR_MASK		(0xf << CORE_VERSION_MAJOR_SHIFT)
#define CORE_VERSION_MINOR_MASK		0xff

#define CORE_MCI_GENERICS		0x70
#define SWITCHABLE_SIGNALING_VOLTAGE	BIT(29)

#define HC_MODE_EN		0x1
#define CORE_POWER		0x0
#define CORE_SW_RST		BIT(7)
#define FF_CLK_SW_RST_DIS	BIT(13)

#define CORE_PWRCTL_BUS_OFF	BIT(0)
#define CORE_PWRCTL_BUS_ON	BIT(1)
#define CORE_PWRCTL_IO_LOW	BIT(2)
#define CORE_PWRCTL_IO_HIGH	BIT(3)
#define CORE_PWRCTL_BUS_SUCCESS BIT(0)
#define CORE_PWRCTL_BUS_FAIL    BIT(1)
#define CORE_PWRCTL_IO_SUCCESS	BIT(2)
#define CORE_PWRCTL_IO_FAIL     BIT(3)
#define REQ_BUS_OFF		BIT(0)
#define REQ_BUS_ON		BIT(1)
#define REQ_IO_LOW		BIT(2)
#define REQ_IO_HIGH		BIT(3)
#define INT_MASK		0xf
#define MAX_PHASES		16
#define CORE_DLL_LOCK		BIT(7)
#define CORE_DDR_DLL_LOCK	BIT(11)
#define CORE_DLL_EN		BIT(16)
#define CORE_CDR_EN		BIT(17)
#define CORE_CK_OUT_EN		BIT(18)
#define CORE_CDR_EXT_EN		BIT(19)
#define CORE_DLL_PDN		BIT(29)
#define CORE_DLL_RST		BIT(30)
#define CORE_CMD_DAT_TRACK_SEL	BIT(0)

#define CORE_DDR_CAL_EN		BIT(0)
#define CORE_FLL_CYCLE_CNT	BIT(18)
#define CORE_DLL_CLOCK_DISABLE	BIT(21)

#define DLL_USR_CTL_POR_VAL	0x10800
#define ENABLE_DLL_LOCK_STATUS	BIT(26)
#define FINE_TUNE_MODE_EN	BIT(27)
#define BIAS_OK_SIGNAL		BIT(29)

#define DLL_CONFIG_3_LOW_FREQ_VAL	0x08
#define DLL_CONFIG_3_HIGH_FREQ_VAL	0x10

#define CORE_VENDOR_SPEC_POR_VAL 0xa9c
#define CORE_CLK_PWRSAVE	BIT(1)
#define CORE_HC_MCLK_SEL_DFLT	(2 << 8)
#define CORE_HC_MCLK_SEL_HS400	(3 << 8)
#define CORE_HC_MCLK_SEL_MASK	(3 << 8)
#define CORE_IO_PAD_PWR_SWITCH_EN	BIT(15)
#define CORE_IO_PAD_PWR_SWITCH	BIT(16)
#define CORE_HC_SELECT_IN_EN	BIT(18)
#define CORE_HC_SELECT_IN_HS400	(6 << 19)
#define CORE_HC_SELECT_IN_MASK	(7 << 19)

#define CORE_3_0V_SUPPORT	BIT(25)
#define CORE_1_8V_SUPPORT	BIT(26)
#define CORE_VOLT_SUPPORT	(CORE_3_0V_SUPPORT | CORE_1_8V_SUPPORT)

#define CORE_CSR_CDC_CTLR_CFG0		0x130
#define CORE_SW_TRIG_FULL_CALIB		BIT(16)
#define CORE_HW_AUTOCAL_ENA		BIT(17)

#define CORE_CSR_CDC_CTLR_CFG1		0x134
#define CORE_CSR_CDC_CAL_TIMER_CFG0	0x138
#define CORE_TIMER_ENA			BIT(16)

#define CORE_CSR_CDC_CAL_TIMER_CFG1	0x13C
#define CORE_CSR_CDC_REFCOUNT_CFG	0x140
#define CORE_CSR_CDC_COARSE_CAL_CFG	0x144
#define CORE_CDC_OFFSET_CFG		0x14C
#define CORE_CSR_CDC_DELAY_CFG		0x150
#define CORE_CDC_SLAVE_DDA_CFG		0x160
#define CORE_CSR_CDC_STATUS0		0x164
#define CORE_CALIBRATION_DONE		BIT(0)

#define CORE_CDC_ERROR_CODE_MASK	0x7000000

#define CORE_CSR_CDC_GEN_CFG		0x178
#define CORE_CDC_SWITCH_BYPASS_OFF	BIT(0)
#define CORE_CDC_SWITCH_RC_EN		BIT(1)

#define CORE_CDC_T4_DLY_SEL		BIT(0)
#define CORE_CMDIN_RCLK_EN		BIT(1)
#define CORE_START_CDC_TRAFFIC		BIT(6)

#define CORE_PWRSAVE_DLL	BIT(3)

#define DDR_CONFIG_POR_VAL	0x80040873


#define INVALID_TUNING_PHASE	-1
#define SDHCI_MSM_MIN_CLOCK	400000
#define CORE_FREQ_100MHZ	(100 * 1000 * 1000)

#define CDR_SELEXT_SHIFT	20
#define CDR_SELEXT_MASK		(0xf << CDR_SELEXT_SHIFT)
#define CMUX_SHIFT_PHASE_SHIFT	24
#define CMUX_SHIFT_PHASE_MASK	(7 << CMUX_SHIFT_PHASE_SHIFT)

#define MSM_MMC_AUTOSUSPEND_DELAY_MS	50

/* Timeout value to avoid infinite waiting for pwr_irq */
#define MSM_PWR_IRQ_TIMEOUT_MS 5000

/* Max load for eMMC Vdd-io supply */
#define MMC_VQMMC_MAX_LOAD_UA	325000

#define msm_bayhub_host_readl(msm_bayhub_host, host, offset) \
	msm_bayhub_host->var_ops->msm_bayhub_readl_relaxed(host, offset)

#define msm_bayhub_host_writel(msm_bayhub_host, val, host, offset) \
	msm_bayhub_host->var_ops->msm_bayhub_writel_relaxed(val, host, offset)

/* CQHCI vendor specific registers */
#define CQHCI_VENDOR_CFG1	0xA00
#define CQHCI_VENDOR_DIS_RST_ON_CQ_EN	(0x3 << 13)
/* The code porting from sdhci-msm.c part-1 end */

/*
 * Bayhub patch part-1 start
 * This part define the GGC used structure
 * The GGC chip can work with standard SD host controller
 * GGC is the chip IC name and it's a SD bus signal re-timing IC
 * Aim at high-speed SD mode(ex.SDR104) and long wire of SD signals
 */
#define TUNING_PHASE_SIZE	11
static const u32 ggc_cfg_data[16] = {
	0x07000000, 0x07364022, 0x01015412, 0x01062400,
	0x10400076, 0x00025432, 0x01046076, 0x62011000,
	0x30503106, 0x64141711, 0x10057513, 0x00336200,
	0x00020006, 0x40000400, 0x12200310, 0x3A314177
};

struct ggc_bus_mode_cfg {
	u32 tx_selb_tb[TUNING_PHASE_SIZE];
	u32 all_selb_tb[TUNING_PHASE_SIZE];
	u32 tx_selb_failed_history;
	int bus_mode;
	int default_sela;
	int default_selb;
	u32 default_delaycode;
	u32 dll_voltage_unlock_cnt[4];
	u32 max_delaycode;
	u32 min_delaycode;
	u32 delaycode_narrowdown_index;
	u32 fail_phase;
};

enum tuning_state {
	NO_TUNING = 0,
	OUTPUT_PASS_TYPE = 1,
	SET_PHASE_FAIL_TYPE = 2,
	TUNING_FAIL_TYPE = 3,
	READ_STATUS_FAIL_TYPE = 4,
	TUNING_CMD7_TIMEOUT = 5,
	RETUNING_CASE = 6,
};


struct ggc_reg_op {
	u32 ofs;
	u32 mask;
	u32 value;
};

static const char *const op_dbg_str[] = {
	"no tuning",
	"pass",
	"set_phase_fail",
	"tuning fail",
	"read status fail",
	"tuning CMD7 timeout",
	"retuning case"
};

struct ggc_adaptor {
	struct ggc_bus_mode_cfg sdr50;
	struct ggc_bus_mode_cfg sdr104;
	struct ggc_bus_mode_cfg *cur_bus_mode;

	struct ggc_reg_op pha_stas_rx_low32;
	struct ggc_reg_op pha_stas_rx_high32;
	struct ggc_reg_op pha_stas_tx_low32;
	struct ggc_reg_op pha_stas_tx_high32;
	struct ggc_reg_op dll_sela_after_mask;
	struct ggc_reg_op dll_selb_after_mask;

	struct ggc_reg_op dll_delay_100m_backup;
	struct ggc_reg_op dll_delay_200m_backup;

	struct ggc_reg_op dll_sela_100m_cfg;
	struct ggc_reg_op dll_sela_200m_cfg;
	struct ggc_reg_op dll_selb_100m_cfg;
	struct ggc_reg_op dll_selb_200m_cfg;
	struct ggc_reg_op dll_selb_100m_cfg_en;
	struct ggc_reg_op dll_selb_200m_cfg_en;
	struct ggc_reg_op internl_tuning_en_100m;
	struct ggc_reg_op internl_tuning_en_200m;
	struct ggc_reg_op cmd19_cnt_cfg;

	struct ggc_reg_op inject_failure_for_tuning_enable_cfg;
	struct ggc_reg_op inject_failure_for_200m_tuning_cfg;
	struct ggc_reg_op inject_failure_for_100m_tuning_cfg;

	int def_sela_100m;
	int def_sela_200m;
	int def_selb_100m;
	int def_selb_200m;

	u32 gg_reg_cur[16];
	u8 cur_read_buf[512];

	bool dll_unlock_reinit_flg;
	u8 driver_strength_reinit_flg;
	bool tuning_cmd7_timeout_reinit_flg;
	u32 tuning_cmd7_timeout_reinit_cnt;
	u32 ggc_cur_sela;
	bool selx_tuning_done_flag;
	u32 ggc_cmd_tx_selb_failed_range;
	int ggc_sw_selb_tuning_first_selb;
	enum tuning_state ggc_sela_tuning_result[11];
	int dll_voltage_scan_map[4];
	int cur_dll_voltage_idx;
	int sdr50_notuning_sela_inject_flag;
	int sdr50_notuning_crc_error_flag;
	u32 sdr50_notuning_sela_rx_inject;
	u32 bh201_sdr50_sela_sw_inject;
	u32 bh201_sdr50_selb_hw_inject;
	u32 bh201_sdr104_selb_hw_inject;
	u32 bh201_drive_strength;
	bool tuning_in_progress;
	int pwr_gpio; /* External power enable pin for Redriver IC */
	int det_gpio;
};
/* Bayhub patch part-1 end */

/* The code porting from sdhci-msm.c part-2 start */
struct sdhci_msm_bayhub_offset {
	u32 core_hc_mode;
	u32 core_mci_data_cnt;
	u32 core_mci_status;
	u32 core_mci_fifo_cnt;
	u32 core_mci_version;
	u32 core_generics;
	u32 core_testbus_config;
	u32 core_testbus_sel2_bit;
	u32 core_testbus_ena;
	u32 core_testbus_sel2;
	u32 core_pwrctl_status;
	u32 core_pwrctl_mask;
	u32 core_pwrctl_clear;
	u32 core_pwrctl_ctl;
	u32 core_sdcc_debug_reg;
	u32 core_dll_config;
	u32 core_dll_status;
	u32 core_vendor_spec;
	u32 core_vendor_spec_adma_err_addr0;
	u32 core_vendor_spec_adma_err_addr1;
	u32 core_vendor_spec_func2;
	u32 core_vendor_spec_capabilities0;
	u32 core_ddr_200_cfg;
	u32 core_vendor_spec3;
	u32 core_dll_config_2;
	u32 core_dll_config_3;
	u32 core_ddr_config_old; /* Applicable to sdcc minor ver < 0x49 */
	u32 core_ddr_config;
	u32 core_dll_usr_ctl; /* Present on SDCC5.1 onwards */
};

static const struct sdhci_msm_bayhub_offset sdhci_msm_bayhub_v5_offset = {
	.core_mci_data_cnt = 0x35c,
	.core_mci_status = 0x324,
	.core_mci_fifo_cnt = 0x308,
	.core_mci_version = 0x318,
	.core_generics = 0x320,
	.core_testbus_config = 0x32c,
	.core_testbus_sel2_bit = 3,
	.core_testbus_ena = (1 << 31),
	.core_testbus_sel2 = (1 << 3),
	.core_pwrctl_status = 0x240,
	.core_pwrctl_mask = 0x244,
	.core_pwrctl_clear = 0x248,
	.core_pwrctl_ctl = 0x24c,
	.core_sdcc_debug_reg = 0x358,
	.core_dll_config = 0x200,
	.core_dll_status = 0x208,
	.core_vendor_spec = 0x20c,
	.core_vendor_spec_adma_err_addr0 = 0x214,
	.core_vendor_spec_adma_err_addr1 = 0x218,
	.core_vendor_spec_func2 = 0x210,
	.core_vendor_spec_capabilities0 = 0x21c,
	.core_ddr_200_cfg = 0x224,
	.core_vendor_spec3 = 0x250,
	.core_dll_config_2 = 0x254,
	.core_dll_config_3 = 0x258,
	.core_ddr_config = 0x25c,
	.core_dll_usr_ctl = 0x388,
};

struct sdhci_msm_bayhub_variant_ops {
	u32 (*msm_bayhub_readl_relaxed)(struct sdhci_host *host, u32 offset);
	void (*msm_bayhub_writel_relaxed)(u32 val, struct sdhci_host *host,
			u32 offset);
};

/*
 * From V5, register spaces have changed. Wrap this info in a structure
 * and choose the data_structure based on version info mentioned in DT.
 */
struct sdhci_msm_bayhub_variant_info {
	bool mci_removed;
	bool restore_dll_config;
	bool uses_tassadar_dll;
	const struct sdhci_msm_bayhub_variant_ops *var_ops;
	const struct sdhci_msm_bayhub_offset *offset;
};

struct sdhci_msm_bayhub_host {
	struct platform_device *pdev;
	void __iomem *core_mem;	/* MSM SDCC mapped address */
	int pwr_irq;		/* power irq */
	struct clk *bus_clk;	/* SDHC bus voter clock */
	struct clk *xo_clk;	/* TCXO clk needed for FLL feature of cm_dll*/
	struct clk_bulk_data bulk_clks[4]; /* core, iface, cal, sleep clocks */
	unsigned long clk_rate;
	struct mmc_host *mmc;
	struct opp_table *opp_table;
	bool has_opp_table;
	bool use_14lpp_dll_reset;
	bool tuning_done;
	bool calibration_done;
	u8 saved_tuning_phase;
	bool use_cdclp533;
	u32 curr_pwr_state;
	u32 curr_io_level;
	wait_queue_head_t pwr_irq_wait;
	bool pwr_irq_flag;
	u32 caps_0;
	bool mci_removed;
	bool restore_dll_config;
	const struct sdhci_msm_bayhub_variant_ops *var_ops;
	const struct sdhci_msm_bayhub_offset *offset;
	bool use_cdr;
	u32 transfer_mode;
	bool updated_ddr_cfg;
	bool uses_tassadar_dll;
	u32 dll_config;
	u32 ddr_config;
	bool vqmmc_enabled;
	struct ggc_adaptor  ggc; /* GGC chip structure is added into Qualcomm host */
};

static const struct sdhci_msm_bayhub_offset *sdhci_priv_msm_bayhub_offset(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *msm_bayhub_host = sdhci_pltfm_priv(pltfm_host);

	return msm_bayhub_host->offset;
}

/*
 * APIs to read/write to vendor specific registers which were there in the
 * core_mem region before MCI was removed.
 */
static u32 sdhci_msm_bayhub_v5_variant_readl_relaxed(struct sdhci_host *host,
		u32 offset)
{
	return readl_relaxed(host->ioaddr + offset);
}

static void sdhci_msm_bayhub_v5_variant_writel_relaxed(u32 val,
		struct sdhci_host *host, u32 offset)
{
	writel_relaxed(val, host->ioaddr + offset);
}

static unsigned int msm_bayhub_get_clock_mult_for_bus_mode(struct sdhci_host *host)
{
	struct mmc_ios ios = host->mmc->ios;
	/*
	 * The SDHC requires internal clock frequency to be double the
	 * actual clock that will be set for DDR mode. The controller
	 * uses the faster clock(100/400MHz) for some of its parts and
	 * send the actual required clock (50/200MHz) to the card.
	 */
	if (ios.timing == MMC_TIMING_UHS_DDR50 ||
	    ios.timing == MMC_TIMING_MMC_DDR52 ||
	    ios.timing == MMC_TIMING_MMC_HS400 ||
	    host->flags & SDHCI_HS400_TUNING)
		return 2;
	return 1;
}

static void msm_bayhub_set_clock_rate_for_bus_mode(struct sdhci_host *host,
					    unsigned int clock)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *msm_bayhub_host = sdhci_pltfm_priv(pltfm_host);
	struct mmc_ios curr_ios = host->mmc->ios;
	struct clk *core_clk = msm_bayhub_host->bulk_clks[0].clk;
	unsigned long achieved_rate;
	unsigned int desired_rate;
	unsigned int mult;
	int rc;

	mult = msm_bayhub_get_clock_mult_for_bus_mode(host);
	desired_rate = clock * mult;
	rc = dev_pm_opp_set_rate(mmc_dev(host->mmc), desired_rate);
	if (rc) {
		pr_err("%s: Failed to set clock at rate %u at timing %d\n",
		       mmc_hostname(host->mmc), desired_rate, curr_ios.timing);
		return;
	}

	/*
	 * Qualcomm clock drivers by default round clock _up_ if they can't
	 * make the requested rate.  This is not good for SD.  Yell if we
	 * encounter it.
	 */
	achieved_rate = clk_get_rate(core_clk);
	if (achieved_rate > desired_rate)
		pr_warn("%s: Card appears overclocked; req %u Hz, actual %lu Hz\n",
			mmc_hostname(host->mmc), desired_rate, achieved_rate);
	host->mmc->actual_clock = achieved_rate / mult;

	/* Stash the rate we requested to use in sdhci_msm_bayhub_runtime_resume() */
	msm_bayhub_host->clk_rate = desired_rate;

	pr_debug("%s: Setting clock at rate %lu at timing %d\n",
		 mmc_hostname(host->mmc), achieved_rate, curr_ios.timing);
}

/* Platform specific tuning */
static inline int msm_bayhub_dll_poll_ck_out_en(struct sdhci_host *host, u8 poll)
{
	u32 wait_cnt = 50;
	u8 ck_out_en;
	struct mmc_host *mmc = host->mmc;
	const struct sdhci_msm_bayhub_offset *msm_bayhub_offset =
					sdhci_priv_msm_bayhub_offset(host);

	/* Poll for CK_OUT_EN bit.  max. poll time = 50us */
	ck_out_en = !!(readl_relaxed(host->ioaddr +
			msm_bayhub_offset->core_dll_config) & CORE_CK_OUT_EN);

	while (ck_out_en != poll) {
		if (--wait_cnt == 0) {
			dev_err(mmc_dev(mmc), "%s: CK_OUT_EN bit is not %d\n",
			       mmc_hostname(mmc), poll);
			return -ETIMEDOUT;
		}
		udelay(1);

		ck_out_en = !!(readl_relaxed(host->ioaddr +
			msm_bayhub_offset->core_dll_config) & CORE_CK_OUT_EN);
	}

	return 0;
}

static int msm_bayhub_config_cm_dll_phase(struct sdhci_host *host, u8 phase)
{
	int rc;
	static const u8 grey_coded_phase_table[] = {
		0x0, 0x1, 0x3, 0x2, 0x6, 0x7, 0x5, 0x4,
		0xc, 0xd, 0xf, 0xe, 0xa, 0xb, 0x9, 0x8
	};
	unsigned long flags;
	u32 config;
	struct mmc_host *mmc = host->mmc;
	const struct sdhci_msm_bayhub_offset *msm_bayhub_offset =
					sdhci_priv_msm_bayhub_offset(host);

	if (phase > 0xf)
		return -EINVAL;

	spin_lock_irqsave(&host->lock, flags);

	config = readl_relaxed(host->ioaddr + msm_bayhub_offset->core_dll_config);
	config &= ~(CORE_CDR_EN | CORE_CK_OUT_EN);
	config |= (CORE_CDR_EXT_EN | CORE_DLL_EN);
	writel_relaxed(config, host->ioaddr + msm_bayhub_offset->core_dll_config);

	/* Wait until CK_OUT_EN bit of DLL_CONFIG register becomes '0' */
	rc = msm_bayhub_dll_poll_ck_out_en(host, 0);
	if (rc)
		goto err_out;

	/*
	 * Write the selected DLL clock output phase (0 ... 15)
	 * to CDR_SELEXT bit field of DLL_CONFIG register.
	 */
	config = readl_relaxed(host->ioaddr + msm_bayhub_offset->core_dll_config);
	config &= ~CDR_SELEXT_MASK;
	config |= grey_coded_phase_table[phase] << CDR_SELEXT_SHIFT;
	writel_relaxed(config, host->ioaddr + msm_bayhub_offset->core_dll_config);

	config = readl_relaxed(host->ioaddr + msm_bayhub_offset->core_dll_config);
	config |= CORE_CK_OUT_EN;
	writel_relaxed(config, host->ioaddr + msm_bayhub_offset->core_dll_config);

	/* Wait until CK_OUT_EN bit of DLL_CONFIG register becomes '1' */
	rc = msm_bayhub_dll_poll_ck_out_en(host, 1);
	if (rc)
		goto err_out;

	config = readl_relaxed(host->ioaddr + msm_bayhub_offset->core_dll_config);
	config |= CORE_CDR_EN;
	config &= ~CORE_CDR_EXT_EN;
	writel_relaxed(config, host->ioaddr + msm_bayhub_offset->core_dll_config);
	goto out;

err_out:
	dev_err(mmc_dev(mmc), "%s: Failed to set DLL phase: %d\n",
	       mmc_hostname(mmc), phase);
out:
	spin_unlock_irqrestore(&host->lock, flags);
	return rc;
}

/*
 * Find out the greatest range of consecuitive selected
 * DLL clock output phases that can be used as sampling
 * setting for SD3.0 UHS-I card read operation (in SDR104
 * timing mode) or for eMMC4.5 card read operation (in
 * HS400/HS200 timing mode).
 * Select the 3/4 of the range and configure the DLL with the
 * selected DLL clock output phase.
 */

static int msm_bayhub_find_most_appropriate_phase(struct sdhci_host *host,
					   u8 *phase_table, u8 total_phases)
{
	int ret;
	u8 ranges[MAX_PHASES][MAX_PHASES] = { {0}, {0} };
	u8 phases_per_row[MAX_PHASES] = { 0 };
	int row_index = 0, col_index = 0, selected_row_index = 0, curr_max = 0;
	int i, cnt, phase_0_raw_index = 0, phase_15_raw_index = 0;
	bool phase_0_found = false, phase_15_found = false;
	struct mmc_host *mmc = host->mmc;

	if (!total_phases || (total_phases > MAX_PHASES)) {
		dev_err(mmc_dev(mmc), "%s: Invalid argument: total_phases=%d\n",
		       mmc_hostname(mmc), total_phases);
		return -EINVAL;
	}

	for (cnt = 0; cnt < total_phases; cnt++) {
		ranges[row_index][col_index] = phase_table[cnt];
		phases_per_row[row_index] += 1;
		col_index++;

		if ((cnt + 1) == total_phases) {
			continue;
		/* check if next phase in phase_table is consecutive or not */
		} else if ((phase_table[cnt] + 1) != phase_table[cnt + 1]) {
			row_index++;
			col_index = 0;
		}
	}

	if (row_index >= MAX_PHASES)
		return -EINVAL;

	/* Check if phase-0 is present in first valid window? */
	if (!ranges[0][0]) {
		phase_0_found = true;
		phase_0_raw_index = 0;
		/* Check if cycle exist between 2 valid windows */
		for (cnt = 1; cnt <= row_index; cnt++) {
			if (phases_per_row[cnt]) {
				for (i = 0; i < phases_per_row[cnt]; i++) {
					if (ranges[cnt][i] == 15) {
						phase_15_found = true;
						phase_15_raw_index = cnt;
						break;
					}
				}
			}
		}
	}

	/* If 2 valid windows form cycle then merge them as single window */
	if (phase_0_found && phase_15_found) {
		/* number of phases in raw where phase 0 is present */
		u8 phases_0 = phases_per_row[phase_0_raw_index];
		/* number of phases in raw where phase 15 is present */
		u8 phases_15 = phases_per_row[phase_15_raw_index];

		if (phases_0 + phases_15 >= MAX_PHASES)
			/*
			 * If there are more than 1 phase windows then total
			 * number of phases in both the windows should not be
			 * more than or equal to MAX_PHASES.
			 */
			return -EINVAL;

		/* Merge 2 cyclic windows */
		i = phases_15;
		for (cnt = 0; cnt < phases_0; cnt++) {
			ranges[phase_15_raw_index][i] =
			    ranges[phase_0_raw_index][cnt];
			if (++i >= MAX_PHASES)
				break;
		}

		phases_per_row[phase_0_raw_index] = 0;
		phases_per_row[phase_15_raw_index] = phases_15 + phases_0;
	}

	for (cnt = 0; cnt <= row_index; cnt++) {
		if (phases_per_row[cnt] > curr_max) {
			curr_max = phases_per_row[cnt];
			selected_row_index = cnt;
		}
	}

	i = (curr_max * 3) / 4;
	if (i)
		i--;

	ret = ranges[selected_row_index][i];

	if (ret >= MAX_PHASES) {
		ret = -EINVAL;
		dev_err(mmc_dev(mmc), "%s: Invalid phase selected=%d\n",
		       mmc_hostname(mmc), ret);
	}

	return ret;
}

static inline void msm_bayhub_cm_dll_set_freq(struct sdhci_host *host)
{
	u32 mclk_freq = 0, config;
	const struct sdhci_msm_bayhub_offset *msm_bayhub_offset =
					sdhci_priv_msm_bayhub_offset(host);

	/* Program the MCLK value to MCLK_FREQ bit field */
	if (host->clock <= 112000000)
		mclk_freq = 0;
	else if (host->clock <= 125000000)
		mclk_freq = 1;
	else if (host->clock <= 137000000)
		mclk_freq = 2;
	else if (host->clock <= 150000000)
		mclk_freq = 3;
	else if (host->clock <= 162000000)
		mclk_freq = 4;
	else if (host->clock <= 175000000)
		mclk_freq = 5;
	else if (host->clock <= 187000000)
		mclk_freq = 6;
	else if (host->clock <= 200000000)
		mclk_freq = 7;

	config = readl_relaxed(host->ioaddr + msm_bayhub_offset->core_dll_config);
	config &= ~CMUX_SHIFT_PHASE_MASK;
	config |= mclk_freq << CMUX_SHIFT_PHASE_SHIFT;
	writel_relaxed(config, host->ioaddr + msm_bayhub_offset->core_dll_config);
}

/* Initialize the DLL (Programmable Delay Line) */
static int msm_bayhub_init_cm_dll(struct sdhci_host *host)
{
	struct mmc_host *mmc = host->mmc;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *msm_bayhub_host = sdhci_pltfm_priv(pltfm_host);
	int wait_cnt = 50;
	unsigned long flags, xo_clk = 0;
	u32 config;
	const struct sdhci_msm_bayhub_offset *msm_bayhub_offset =
					msm_bayhub_host->offset;

	if (msm_bayhub_host->use_14lpp_dll_reset && !IS_ERR_OR_NULL(msm_bayhub_host->xo_clk))
		xo_clk = clk_get_rate(msm_bayhub_host->xo_clk);

	spin_lock_irqsave(&host->lock, flags);

	/*
	 * Make sure that clock is always enabled when DLL
	 * tuning is in progress. Keeping PWRSAVE ON may
	 * turn off the clock.
	 */
	config = readl_relaxed(host->ioaddr + msm_bayhub_offset->core_vendor_spec);
	config &= ~CORE_CLK_PWRSAVE;
	writel_relaxed(config, host->ioaddr + msm_bayhub_offset->core_vendor_spec);

	if (msm_bayhub_host->dll_config)
		writel_relaxed(msm_bayhub_host->dll_config,
				host->ioaddr + msm_bayhub_offset->core_dll_config);

	if (msm_bayhub_host->use_14lpp_dll_reset) {
		config = readl_relaxed(host->ioaddr +
				msm_bayhub_offset->core_dll_config);
		config &= ~CORE_CK_OUT_EN;
		writel_relaxed(config, host->ioaddr +
				msm_bayhub_offset->core_dll_config);

		config = readl_relaxed(host->ioaddr +
				msm_bayhub_offset->core_dll_config_2);
		config |= CORE_DLL_CLOCK_DISABLE;
		writel_relaxed(config, host->ioaddr +
				msm_bayhub_offset->core_dll_config_2);
	}

	config = readl_relaxed(host->ioaddr +
			msm_bayhub_offset->core_dll_config);
	config |= CORE_DLL_RST;
	writel_relaxed(config, host->ioaddr +
			msm_bayhub_offset->core_dll_config);

	config = readl_relaxed(host->ioaddr +
			msm_bayhub_offset->core_dll_config);
	config |= CORE_DLL_PDN;
	writel_relaxed(config, host->ioaddr +
			msm_bayhub_offset->core_dll_config);

	if (!msm_bayhub_host->dll_config)
		msm_bayhub_cm_dll_set_freq(host);

	if (msm_bayhub_host->use_14lpp_dll_reset &&
	    !IS_ERR_OR_NULL(msm_bayhub_host->xo_clk)) {
		u32 mclk_freq = 0;

		config = readl_relaxed(host->ioaddr +
				msm_bayhub_offset->core_dll_config_2);
		config &= CORE_FLL_CYCLE_CNT;
		if (config)
			mclk_freq = DIV_ROUND_CLOSEST_ULL((host->clock * 8),
					xo_clk);
		else
			mclk_freq = DIV_ROUND_CLOSEST_ULL((host->clock * 4),
					xo_clk);

		config = readl_relaxed(host->ioaddr +
				msm_bayhub_offset->core_dll_config_2);
		config &= ~(0xFF << 10);
		config |= mclk_freq << 10;

		writel_relaxed(config, host->ioaddr +
				msm_bayhub_offset->core_dll_config_2);
		/* wait for 5us before enabling DLL clock */
		udelay(5);
	}

	config = readl_relaxed(host->ioaddr +
			msm_bayhub_offset->core_dll_config);
	config &= ~CORE_DLL_RST;
	writel_relaxed(config, host->ioaddr +
			msm_bayhub_offset->core_dll_config);

	config = readl_relaxed(host->ioaddr +
			msm_bayhub_offset->core_dll_config);
	config &= ~CORE_DLL_PDN;
	writel_relaxed(config, host->ioaddr +
			msm_bayhub_offset->core_dll_config);

	if (msm_bayhub_host->use_14lpp_dll_reset) {
		if (!msm_bayhub_host->dll_config)
			msm_bayhub_cm_dll_set_freq(host);
		config = readl_relaxed(host->ioaddr +
				msm_bayhub_offset->core_dll_config_2);
		config &= ~CORE_DLL_CLOCK_DISABLE;
		writel_relaxed(config, host->ioaddr +
				msm_bayhub_offset->core_dll_config_2);
	}

	/*
	 * Configure DLL user control register to enable DLL status.
	 * This setting is applicable to SDCC v5.1 onwards only.
	 */
	if (msm_bayhub_host->uses_tassadar_dll) {
		config = DLL_USR_CTL_POR_VAL | FINE_TUNE_MODE_EN |
			ENABLE_DLL_LOCK_STATUS | BIAS_OK_SIGNAL;
		writel_relaxed(config, host->ioaddr +
				msm_bayhub_offset->core_dll_usr_ctl);

		config = readl_relaxed(host->ioaddr +
				msm_bayhub_offset->core_dll_config_3);
		config &= ~0xFF;
		if (msm_bayhub_host->clk_rate < 150000000)
			config |= DLL_CONFIG_3_LOW_FREQ_VAL;
		else
			config |= DLL_CONFIG_3_HIGH_FREQ_VAL;
		writel_relaxed(config, host->ioaddr +
			msm_bayhub_offset->core_dll_config_3);
	}

	config = readl_relaxed(host->ioaddr +
			msm_bayhub_offset->core_dll_config);
	config |= CORE_DLL_EN;
	writel_relaxed(config, host->ioaddr +
			msm_bayhub_offset->core_dll_config);

	config = readl_relaxed(host->ioaddr +
			msm_bayhub_offset->core_dll_config);
	config |= CORE_CK_OUT_EN;
	writel_relaxed(config, host->ioaddr +
			msm_bayhub_offset->core_dll_config);

	/* Wait until DLL_LOCK bit of DLL_STATUS register becomes '1' */
	while (!(readl_relaxed(host->ioaddr + msm_bayhub_offset->core_dll_status) &
		 CORE_DLL_LOCK)) {
		/* max. wait for 50us sec for LOCK bit to be set */
		if (--wait_cnt == 0) {
			dev_err(mmc_dev(mmc), "%s: DLL failed to LOCK\n",
			       mmc_hostname(mmc));
			spin_unlock_irqrestore(&host->lock, flags);
			return -ETIMEDOUT;
		}
		udelay(1);
	}

	spin_unlock_irqrestore(&host->lock, flags);
	return 0;
}

static void msm_bayhub_hc_select_default(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *msm_bayhub_host = sdhci_pltfm_priv(pltfm_host);
	u32 config;
	const struct sdhci_msm_bayhub_offset *msm_bayhub_offset =
					msm_bayhub_host->offset;

	if (!msm_bayhub_host->use_cdclp533) {
		config = readl_relaxed(host->ioaddr +
				msm_bayhub_offset->core_vendor_spec3);
		config &= ~CORE_PWRSAVE_DLL;
		writel_relaxed(config, host->ioaddr +
				msm_bayhub_offset->core_vendor_spec3);
	}

	config = readl_relaxed(host->ioaddr + msm_bayhub_offset->core_vendor_spec);
	config &= ~CORE_HC_MCLK_SEL_MASK;
	config |= CORE_HC_MCLK_SEL_DFLT;
	writel_relaxed(config, host->ioaddr + msm_bayhub_offset->core_vendor_spec);

	/*
	 * Disable HC_SELECT_IN to be able to use the UHS mode select
	 * configuration from Host Control2 register for all other
	 * modes.
	 * Write 0 to HC_SELECT_IN and HC_SELECT_IN_EN field
	 * in VENDOR_SPEC_FUNC
	 */
	config = readl_relaxed(host->ioaddr + msm_bayhub_offset->core_vendor_spec);
	config &= ~CORE_HC_SELECT_IN_EN;
	config &= ~CORE_HC_SELECT_IN_MASK;
	writel_relaxed(config, host->ioaddr + msm_bayhub_offset->core_vendor_spec);

	/*
	 * Make sure above writes impacting free running MCLK are completed
	 * before changing the clk_rate at GCC.
	 */
	wmb();
}

static void msm_bayhub_hc_select_hs400(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *msm_bayhub_host = sdhci_pltfm_priv(pltfm_host);
	struct mmc_ios ios = host->mmc->ios;
	u32 config, dll_lock;
	int rc;
	const struct sdhci_msm_bayhub_offset *msm_bayhub_offset =
					msm_bayhub_host->offset;

	/* Select the divided clock (free running MCLK/2) */
	config = readl_relaxed(host->ioaddr + msm_bayhub_offset->core_vendor_spec);
	config &= ~CORE_HC_MCLK_SEL_MASK;
	config |= CORE_HC_MCLK_SEL_HS400;

	writel_relaxed(config, host->ioaddr + msm_bayhub_offset->core_vendor_spec);
	/*
	 * Select HS400 mode using the HC_SELECT_IN from VENDOR SPEC
	 * register
	 */
	if ((msm_bayhub_host->tuning_done || ios.enhanced_strobe) &&
	    !msm_bayhub_host->calibration_done) {
		config = readl_relaxed(host->ioaddr +
				msm_bayhub_offset->core_vendor_spec);
		config |= CORE_HC_SELECT_IN_HS400;
		config |= CORE_HC_SELECT_IN_EN;
		writel_relaxed(config, host->ioaddr +
				msm_bayhub_offset->core_vendor_spec);
	}
	if (!msm_bayhub_host->clk_rate && !msm_bayhub_host->use_cdclp533) {
		/*
		 * Poll on DLL_LOCK or DDR_DLL_LOCK bits in
		 * core_dll_status to be set. This should get set
		 * within 15 us at 200 MHz.
		 */
		rc = readl_relaxed_poll_timeout(host->ioaddr +
						msm_bayhub_offset->core_dll_status,
						dll_lock,
						(dll_lock &
						(CORE_DLL_LOCK |
						CORE_DDR_DLL_LOCK)), 10,
						1000);
		if (rc == -ETIMEDOUT)
			pr_err("%s: Unable to get DLL_LOCK/DDR_DLL_LOCK, dll_status: 0x%08x\n",
			       mmc_hostname(host->mmc), dll_lock);
	}
	/*
	 * Make sure above writes impacting free running MCLK are completed
	 * before changing the clk_rate at GCC.
	 */
	wmb();
}

/*
 * sdhci_msm_bayhub_hc_select_mode :- In general all timing modes are
 * controlled via UHS mode select in Host Control2 register.
 * eMMC specific HS200/HS400 doesn't have their respective modes
 * defined here, hence we use these values.
 *
 * HS200 - SDR104 (Since they both are equivalent in functionality)
 * HS400 - This involves multiple configurations
 *		Initially SDR104 - when tuning is required as HS200
 *		Then when switching to DDR @ 400MHz (HS400) we use
 *		the vendor specific HC_SELECT_IN to control the mode.
 *
 * In addition to controlling the modes we also need to select the
 * correct input clock for DLL depending on the mode.
 *
 * HS400 - divided clock (free running MCLK/2)
 * All other modes - default (free running MCLK)
 */
static void sdhci_msm_bayhub_hc_select_mode(struct sdhci_host *host)
{
	struct mmc_ios ios = host->mmc->ios;

	if (ios.timing == MMC_TIMING_MMC_HS400 ||
	    host->flags & SDHCI_HS400_TUNING)
		msm_bayhub_hc_select_hs400(host);
	else
		msm_bayhub_hc_select_default(host);
}

static int sdhci_msm_bayhub_cdclp533_calibration(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *msm_bayhub_host = sdhci_pltfm_priv(pltfm_host);
	u32 config, calib_done;
	int ret;
	const struct sdhci_msm_bayhub_offset *msm_bayhub_offset =
					msm_bayhub_host->offset;

	pr_debug("%s: %s: Enter\n", mmc_hostname(host->mmc), __func__);

	/*
	 * Retuning in HS400 (DDR mode) will fail, just reset the
	 * tuning block and restore the saved tuning phase.
	 */
	ret = msm_bayhub_init_cm_dll(host);
	if (ret)
		goto out;

	/* Set the selected phase in delay line hw block */
	ret = msm_bayhub_config_cm_dll_phase(host, msm_bayhub_host->saved_tuning_phase);
	if (ret)
		goto out;

	config = readl_relaxed(host->ioaddr + msm_bayhub_offset->core_dll_config);
	config |= CORE_CMD_DAT_TRACK_SEL;
	writel_relaxed(config, host->ioaddr + msm_bayhub_offset->core_dll_config);

	config = readl_relaxed(host->ioaddr + msm_bayhub_offset->core_ddr_200_cfg);
	config &= ~CORE_CDC_T4_DLY_SEL;
	writel_relaxed(config, host->ioaddr + msm_bayhub_offset->core_ddr_200_cfg);

	config = readl_relaxed(host->ioaddr + CORE_CSR_CDC_GEN_CFG);
	config &= ~CORE_CDC_SWITCH_BYPASS_OFF;
	writel_relaxed(config, host->ioaddr + CORE_CSR_CDC_GEN_CFG);

	config = readl_relaxed(host->ioaddr + CORE_CSR_CDC_GEN_CFG);
	config |= CORE_CDC_SWITCH_RC_EN;
	writel_relaxed(config, host->ioaddr + CORE_CSR_CDC_GEN_CFG);

	config = readl_relaxed(host->ioaddr + msm_bayhub_offset->core_ddr_200_cfg);
	config &= ~CORE_START_CDC_TRAFFIC;
	writel_relaxed(config, host->ioaddr + msm_bayhub_offset->core_ddr_200_cfg);

	/* Perform CDC Register Initialization Sequence */

	writel_relaxed(0x11800EC, host->ioaddr + CORE_CSR_CDC_CTLR_CFG0);
	writel_relaxed(0x3011111, host->ioaddr + CORE_CSR_CDC_CTLR_CFG1);
	writel_relaxed(0x1201000, host->ioaddr + CORE_CSR_CDC_CAL_TIMER_CFG0);
	writel_relaxed(0x4, host->ioaddr + CORE_CSR_CDC_CAL_TIMER_CFG1);
	writel_relaxed(0xCB732020, host->ioaddr + CORE_CSR_CDC_REFCOUNT_CFG);
	writel_relaxed(0xB19, host->ioaddr + CORE_CSR_CDC_COARSE_CAL_CFG);
	writel_relaxed(0x4E2, host->ioaddr + CORE_CSR_CDC_DELAY_CFG);
	writel_relaxed(0x0, host->ioaddr + CORE_CDC_OFFSET_CFG);
	writel_relaxed(0x16334, host->ioaddr + CORE_CDC_SLAVE_DDA_CFG);

	/* CDC HW Calibration */

	config = readl_relaxed(host->ioaddr + CORE_CSR_CDC_CTLR_CFG0);
	config |= CORE_SW_TRIG_FULL_CALIB;
	writel_relaxed(config, host->ioaddr + CORE_CSR_CDC_CTLR_CFG0);

	config = readl_relaxed(host->ioaddr + CORE_CSR_CDC_CTLR_CFG0);
	config &= ~CORE_SW_TRIG_FULL_CALIB;
	writel_relaxed(config, host->ioaddr + CORE_CSR_CDC_CTLR_CFG0);

	config = readl_relaxed(host->ioaddr + CORE_CSR_CDC_CTLR_CFG0);
	config |= CORE_HW_AUTOCAL_ENA;
	writel_relaxed(config, host->ioaddr + CORE_CSR_CDC_CTLR_CFG0);

	config = readl_relaxed(host->ioaddr + CORE_CSR_CDC_CAL_TIMER_CFG0);
	config |= CORE_TIMER_ENA;
	writel_relaxed(config, host->ioaddr + CORE_CSR_CDC_CAL_TIMER_CFG0);

	ret = readl_relaxed_poll_timeout(host->ioaddr + CORE_CSR_CDC_STATUS0,
					 calib_done,
					 (calib_done & CORE_CALIBRATION_DONE),
					 1, 50);

	if (ret == -ETIMEDOUT) {
		pr_err("%s: %s: CDC calibration was not completed\n",
		       mmc_hostname(host->mmc), __func__);
		goto out;
	}

	ret = readl_relaxed(host->ioaddr + CORE_CSR_CDC_STATUS0)
			& CORE_CDC_ERROR_CODE_MASK;
	if (ret) {
		pr_err("%s: %s: CDC error code %d\n",
		       mmc_hostname(host->mmc), __func__, ret);
		ret = -EINVAL;
		goto out;
	}

	config = readl_relaxed(host->ioaddr + msm_bayhub_offset->core_ddr_200_cfg);
	config |= CORE_START_CDC_TRAFFIC;
	writel_relaxed(config, host->ioaddr + msm_bayhub_offset->core_ddr_200_cfg);
out:
	pr_debug("%s: %s: Exit, ret %d\n", mmc_hostname(host->mmc),
		 __func__, ret);
	return ret;
}

static int sdhci_msm_bayhub_cm_dll_sdc4_calibration(struct sdhci_host *host)
{
	struct mmc_host *mmc = host->mmc;
	u32 dll_status, config, ddr_cfg_offset;
	int ret;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *msm_bayhub_host = sdhci_pltfm_priv(pltfm_host);
	const struct sdhci_msm_bayhub_offset *msm_bayhub_offset =
					sdhci_priv_msm_bayhub_offset(host);

	pr_debug("%s: %s: Enter\n", mmc_hostname(host->mmc), __func__);

	/*
	 * Currently the core_ddr_config register defaults to desired
	 * configuration on reset. Currently reprogramming the power on
	 * reset (POR) value in case it might have been modified by
	 * bootloaders. In the future, if this changes, then the desired
	 * values will need to be programmed appropriately.
	 */
	if (msm_bayhub_host->updated_ddr_cfg)
		ddr_cfg_offset = msm_bayhub_offset->core_ddr_config;
	else
		ddr_cfg_offset = msm_bayhub_offset->core_ddr_config_old;
	writel_relaxed(msm_bayhub_host->ddr_config, host->ioaddr + ddr_cfg_offset);

	if (mmc->ios.enhanced_strobe) {
		config = readl_relaxed(host->ioaddr +
				msm_bayhub_offset->core_ddr_200_cfg);
		config |= CORE_CMDIN_RCLK_EN;
		writel_relaxed(config, host->ioaddr +
				msm_bayhub_offset->core_ddr_200_cfg);
	}

	config = readl_relaxed(host->ioaddr + msm_bayhub_offset->core_dll_config_2);
	config |= CORE_DDR_CAL_EN;
	writel_relaxed(config, host->ioaddr + msm_bayhub_offset->core_dll_config_2);

	ret = readl_relaxed_poll_timeout(host->ioaddr +
					msm_bayhub_offset->core_dll_status,
					dll_status,
					(dll_status & CORE_DDR_DLL_LOCK),
					10, 1000);

	if (ret == -ETIMEDOUT) {
		pr_err("%s: %s: CM_DLL_SDC4 calibration was not completed\n",
		       mmc_hostname(host->mmc), __func__);
		goto out;
	}

	/*
	 * Set CORE_PWRSAVE_DLL bit in CORE_VENDOR_SPEC3.
	 * When MCLK is gated OFF, it is not gated for less than 0.5us
	 * and MCLK must be switched on for at-least 1us before DATA
	 * starts coming. Controllers with 14lpp and later tech DLL cannot
	 * guarantee above requirement. So PWRSAVE_DLL should not be
	 * turned on for host controllers using this DLL.
	 */
	if (!msm_bayhub_host->use_14lpp_dll_reset) {
		config = readl_relaxed(host->ioaddr +
				msm_bayhub_offset->core_vendor_spec3);
		config |= CORE_PWRSAVE_DLL;
		writel_relaxed(config, host->ioaddr +
				msm_bayhub_offset->core_vendor_spec3);
	}

	/*
	 * Drain writebuffer to ensure above DLL calibration
	 * and PWRSAVE DLL is enabled.
	 */
	wmb();
out:
	pr_debug("%s: %s: Exit, ret %d\n", mmc_hostname(host->mmc),
		 __func__, ret);
	return ret;
}

static int sdhci_msm_bayhub_hs400_dll_calibration(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *msm_bayhub_host = sdhci_pltfm_priv(pltfm_host);
	struct mmc_host *mmc = host->mmc;
	int ret;
	u32 config;
	const struct sdhci_msm_bayhub_offset *msm_bayhub_offset =
					msm_bayhub_host->offset;

	pr_debug("%s: %s: Enter\n", mmc_hostname(host->mmc), __func__);

	/*
	 * Retuning in HS400 (DDR mode) will fail, just reset the
	 * tuning block and restore the saved tuning phase.
	 */
	ret = msm_bayhub_init_cm_dll(host);
	if (ret)
		goto out;

	if (!mmc->ios.enhanced_strobe) {
		/* Set the selected phase in delay line hw block */
		ret = msm_bayhub_config_cm_dll_phase(host,
					      msm_bayhub_host->saved_tuning_phase);
		if (ret)
			goto out;
		config = readl_relaxed(host->ioaddr +
				msm_bayhub_offset->core_dll_config);
		config |= CORE_CMD_DAT_TRACK_SEL;
		writel_relaxed(config, host->ioaddr +
				msm_bayhub_offset->core_dll_config);
	}

	if (msm_bayhub_host->use_cdclp533)
		ret = sdhci_msm_bayhub_cdclp533_calibration(host);
	else
		ret = sdhci_msm_bayhub_cm_dll_sdc4_calibration(host);
out:
	pr_debug("%s: %s: Exit, ret %d\n", mmc_hostname(host->mmc),
		 __func__, ret);
	return ret;
}

static bool sdhci_msm_bayhub_is_tuning_needed(struct sdhci_host *host)
{
	struct mmc_ios *ios = &host->mmc->ios;

	/*
	 * Tuning is required for SDR104, HS200 and HS400 cards and
	 * if clock frequency is greater than 100MHz in these modes.
	 */
	if (host->clock <= CORE_FREQ_100MHZ ||
	    !(ios->timing == MMC_TIMING_MMC_HS400 ||
	    ios->timing == MMC_TIMING_MMC_HS200 ||
	    ios->timing == MMC_TIMING_UHS_SDR104) ||
	    ios->enhanced_strobe)
		return false;

	return true;
}

static int sdhci_msm_bayhub_restore_sdr_dll_config(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *msm_bayhub_host = sdhci_pltfm_priv(pltfm_host);
	int ret;

	/*
	 * SDR DLL comes into picture only for timing modes which needs
	 * tuning.
	 */
	if (!sdhci_msm_bayhub_is_tuning_needed(host))
		return 0;

	/* Reset the tuning block */
	ret = msm_bayhub_init_cm_dll(host);
	if (ret)
		return ret;

	/* Restore the tuning block */
	ret = msm_bayhub_config_cm_dll_phase(host, msm_bayhub_host->saved_tuning_phase);

	return ret;
}

static void sdhci_msm_bayhub_set_cdr(struct sdhci_host *host, bool enable)
{
	const struct sdhci_msm_bayhub_offset *msm_bayhub_offset =
							sdhci_priv_msm_bayhub_offset(host);
	u32 config, oldconfig = readl_relaxed(host->ioaddr +
					      msm_bayhub_offset->core_dll_config);

	config = oldconfig;
	if (enable) {
		config |= CORE_CDR_EN;
		config &= ~CORE_CDR_EXT_EN;
	} else {
		config &= ~CORE_CDR_EN;
		config |= CORE_CDR_EXT_EN;
	}

	if (config != oldconfig) {
		writel_relaxed(config, host->ioaddr +
			       msm_bayhub_offset->core_dll_config);
	}
}

static int sdhci_msm_bayhub_execute_tuning(struct mmc_host *mmc, u32 opcode)
{
	struct sdhci_host *host = mmc_priv(mmc);
	int tuning_seq_cnt = 10;
	u8 phase, tuned_phases[16], tuned_phase_cnt = 0;
	int rc;
	struct mmc_ios ios = host->mmc->ios;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *msm_bayhub_host = sdhci_pltfm_priv(pltfm_host);

	if (!sdhci_msm_bayhub_is_tuning_needed(host)) {
		msm_bayhub_host->use_cdr = false;
		sdhci_msm_bayhub_set_cdr(host, false);
		return 0;
	}

	/* Clock-Data-Recovery used to dynamically adjust RX sampling point */
	msm_bayhub_host->use_cdr = true;

	/*
	 * Clear tuning_done flag before tuning to ensure proper
	 * HS400 settings.
	 */
	msm_bayhub_host->tuning_done = 0;

	/*
	 * For HS400 tuning in HS200 timing requires:
	 * - select MCLK/2 in VENDOR_SPEC
	 * - program MCLK to 400MHz (or nearest supported) in GCC
	 */
	if (host->flags & SDHCI_HS400_TUNING) {
		sdhci_msm_bayhub_hc_select_mode(host);
		msm_bayhub_set_clock_rate_for_bus_mode(host, ios.clock);
		host->flags &= ~SDHCI_HS400_TUNING;
	}

retry:
	/* First of all reset the tuning block */
	rc = msm_bayhub_init_cm_dll(host);
	if (rc)
		return rc;

	phase = 0;
	do {
		/* Set the phase in delay line hw block */
		rc = msm_bayhub_config_cm_dll_phase(host, phase);
		if (rc)
			return rc;

		rc = mmc_send_tuning(mmc, opcode, NULL);
		if (!rc) {
			/* Tuning is successful at this tuning point */
			tuned_phases[tuned_phase_cnt++] = phase;
			dev_dbg(mmc_dev(mmc), "%s: Found good phase = %d\n",
				 mmc_hostname(mmc), phase);
		}
	} while (++phase < ARRAY_SIZE(tuned_phases));

	if (tuned_phase_cnt) {
		if (tuned_phase_cnt == ARRAY_SIZE(tuned_phases)) {
			/*
			 * All phases valid is _almost_ as bad as no phases
			 * valid.  Probably all phases are not really reliable
			 * but we didn't detect where the unreliable place is.
			 * That means we'll essentially be guessing and hoping
			 * we get a good phase.  Better to try a few times.
			 */
			dev_dbg(mmc_dev(mmc), "%s: All phases valid; try again\n",
				mmc_hostname(mmc));
			if (--tuning_seq_cnt) {
				tuned_phase_cnt = 0;
				goto retry;
			}
		}

		rc = msm_bayhub_find_most_appropriate_phase(host, tuned_phases,
						     tuned_phase_cnt);
		if (rc >= 0)
			phase = rc;
		else
			return rc;

		/*
		 * Finally set the selected phase in delay
		 * line hw block.
		 */
		rc = msm_bayhub_config_cm_dll_phase(host, phase);
		if (rc)
			return rc;
		msm_bayhub_host->saved_tuning_phase = phase;
		dev_dbg(mmc_dev(mmc), "%s: Setting the tuning phase to %d\n",
			 mmc_hostname(mmc), phase);
	} else {
		if (--tuning_seq_cnt)
			goto retry;
		/* Tuning failed */
		dev_dbg(mmc_dev(mmc), "%s: No tuning point found\n",
		       mmc_hostname(mmc));
		rc = -EIO;
	}

	if (!rc)
		msm_bayhub_host->tuning_done = true;
	return rc;
}
/* The code porting from sdhci-msm.c part-2 end */

/*
 * Bayhub patch part-2 start
 * The APIs used to initialization GGC chip and structure
 */
static void cfg_bit_2_byte(int max_bit, int tar, int *byt, int *bit)
{
	u8 cfg_bit_map[6] = {6, 5, 4, 2, 1, 0};

	*byt = (max_bit - tar) / 6;
	*bit = cfg_bit_map[(max_bit - tar) % 6];
}

static u32 cfg_read_bits_ofs_mask(u8 *cfg, struct ggc_reg_op *bits)
{
	u32 rv = 0;
	u32 mask = bits->mask;
	int byte = 0, bit = 0;
	int i = 0;

	while (mask) {
		cfg_bit_2_byte(MAX_CFG_BIT_VAL, bits->ofs + i, &byte, &bit);
		if (cfg[byte] & (1 << bit))
			rv |= 1 << i;

		i++;
		mask >>= 1;
	};
	return rv;
}

static void get_default_setting(struct sdhci_host *host, u8 *data)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *vendor_host = sdhci_pltfm_priv(pltfm_host);

	vendor_host->ggc.def_sela_100m =
		cfg_read_bits_ofs_mask(data, &vendor_host->ggc.dll_sela_100m_cfg);
	vendor_host->ggc.def_sela_200m =
		cfg_read_bits_ofs_mask(data, &vendor_host->ggc.dll_sela_200m_cfg);
	vendor_host->ggc.def_selb_100m =
		cfg_read_bits_ofs_mask(data, &vendor_host->ggc.dll_sela_100m_cfg);
	vendor_host->ggc.def_selb_200m =
		cfg_read_bits_ofs_mask(data, &vendor_host->ggc.dll_sela_200m_cfg);
}

static void cfg_write_bits_ofs_mask(u8 *cfg,
		struct ggc_reg_op *bits, u32 write_value)
{
	u32 wv = write_value & bits->mask;
	u32 mask = bits->mask;
	int byte = 0, bit = 0;
	int i = 0;

	while (mask) {
		cfg_bit_2_byte(MAX_CFG_BIT_VAL, bits->ofs + i, &byte, &bit);
		if (wv & 1)
			cfg[byte] |= (u8) (1 << bit);
		else
			cfg[byte] &= (u8) (~(1 << bit));

		i++;
		wv >>= 1;
		mask >>= 1;
	};
}

/* Use to get byte offset and bit offset */
static void ram_bit_2_bt(int tar, int *byt, int *bit)
{
	*byt = tar / 8;
	*bit = tar % 8;
}

static void set_gg_reg_cur_val(struct ggc_adaptor  *ggc,
	u8 *data, u8 len)
{
	memcpy(&ggc->gg_reg_cur[0], data, len);
}

static void get_gg_reg_cur_val(struct ggc_adaptor  *ggc,
	u8 *data, u8 len)
{
	memcpy(data, &ggc->gg_reg_cur[0], len);
}

static u32 read_ram_bits_ofs_mask(u8 *cfg, struct ggc_reg_op *bits)
{
	u32 rv = 0;
	u32 mask = bits->mask;
	int byte = 0, bit = 0;
	int i = 0;

	while (mask) {
		/* No replace interface at bitmap
		 * The read out address maybe not align at byte
		 */
		ram_bit_2_bt(bits->ofs + i, &byte, &bit);
		if (cfg[byte] & (1 << bit))
			rv |= 1 << i;

		i++;
		mask >>= 1;
	};
	return rv;
}

static void ggc_dll_voltage_init(struct sdhci_host *host)
{
	int i = 0;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *vendor_host =
						sdhci_pltfm_priv(pltfm_host);

	for (i = 0; i < 4; i++) {
		vendor_host->ggc.dll_voltage_scan_map[i] = 0;
		vendor_host->ggc.sdr50.dll_voltage_unlock_cnt[i] = 0;
		vendor_host->ggc.sdr104.dll_voltage_unlock_cnt[i] = 0;
	}
}

static void ggc_chip_init(struct sdhci_host *host)
{
	u8 data[512];
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *vendor_host =
						sdhci_pltfm_priv(pltfm_host);

	memcpy(data, ggc_cfg_data, sizeof(ggc_cfg_data));
	get_default_setting(host, data);
	set_gg_reg_cur_val(&vendor_host->ggc, data, 64);
}

static void driver_send_command7(struct sdhci_host *host)
{
	struct mmc_host *mmc = host->mmc;
	struct mmc_command cmd = {0};

	cmd.opcode = MMC_SELECT_CARD;
	cmd.arg = 0;
	cmd.flags = MMC_RSP_NONE | MMC_CMD_AC;
	mmc_wait_for_cmd(mmc, &cmd, 3);
}

static void driver_send_command24(struct sdhci_host *host,
		u32 *cfg_data, int data_len)
{
	struct mmc_host *mmc = host->mmc;
	struct mmc_request mrq = {0};
	struct mmc_command cmd = { 0 };
	struct mmc_data data = { 0 };
	struct scatterlist sg;
	u8 *data1 = kzalloc(PAGE_SIZE, GFP_KERNEL);

	if (!data1)
		return;

	memcpy(data1, (u8 *)&(cfg_data[0]), data_len);
	sg_init_one(&sg, data1, 512);

	cmd.opcode = MMC_WRITE_BLOCK;
	cmd.arg = 0;
	cmd.flags = MMC_RSP_R1 | MMC_CMD_ADTC;
	data.blksz = 512;
	data.blocks = 1;
	data.flags = MMC_DATA_WRITE;
	data.timeout_ns = 1000 * 1000 * 1000;
	data.sg = &sg;
	data.sg_len = 1;
	mrq.cmd = &cmd;
	mrq.data = &data;
	mrq.stop = NULL;

	mmc_wait_for_req(mmc, &mrq);
	kfree(data1);
}

static void bht_update_cfg(struct mmc_host *mmc_host,
	struct mmc_card *card, u32 *cfg_data, int data_len)
{
	struct sdhci_host *host;

	host = mmc_priv(mmc_host);
	mmc_set_bus_width(mmc_host, MMC_BUS_WIDTH_4);
	if (host->ops->reset)
		host->ops->reset(host, SDHCI_RESET_CMD|SDHCI_RESET_DATA);

	driver_send_command7(host);
	driver_send_command24(host, cfg_data, data_len);
	driver_send_command7(host);

	mmc_set_bus_width(mmc_host, MMC_BUS_WIDTH_1);
}


static void tx_selb_failed_tb_reset(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *vendor_host =
						sdhci_pltfm_priv(pltfm_host);

	memset(&vendor_host->ggc.sdr104.tx_selb_tb, 0xff,
			sizeof(vendor_host->ggc.sdr104.tx_selb_tb));
	memset(&vendor_host->ggc.sdr50.tx_selb_tb, 0xff,
			sizeof(vendor_host->ggc.sdr50.tx_selb_tb));
}

static void all_selb_failed_tb_reset(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *vendor_host =
						sdhci_pltfm_priv(pltfm_host);

	memset(vendor_host->ggc.sdr104.all_selb_tb, 0xff,
			sizeof(vendor_host->ggc.sdr104.all_selb_tb));
	memset(vendor_host->ggc.sdr50.all_selb_tb, 0xff,
			sizeof(vendor_host->ggc.sdr50.all_selb_tb));
}

static void tx_selb_failed_history_reset(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *vendor_host =
						sdhci_pltfm_priv(pltfm_host);

	vendor_host->ggc.sdr50.tx_selb_failed_history = BIT_PASS_MASK;
	vendor_host->ggc.sdr104.tx_selb_failed_history = BIT_PASS_MASK;
}

static void ggc_reset_selx_failed_tb(struct sdhci_host *host)
{
	tx_selb_failed_tb_reset(host);
	all_selb_failed_tb_reset(host);
	tx_selb_failed_history_reset(host);
}

static void _ggc_reset_sela_tuning_result(
			struct sdhci_msm_bayhub_host *host)
{
	int i = 0;

	for (i = 0; i < TUNING_PHASE_SIZE; i++)
		host->ggc.ggc_sela_tuning_result[i] = NO_TUNING;
}

static void _ggc_reset_tuning_result_for_dll(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *vendor_host =
						sdhci_pltfm_priv(pltfm_host);

	ggc_reset_selx_failed_tb(host);
	vendor_host->ggc.ggc_cmd_tx_selb_failed_range = BIT_PASS_MASK;
	vendor_host->ggc.selx_tuning_done_flag = 0;
	_ggc_reset_sela_tuning_result(vendor_host);
}

static void ggc_tuning_result_reset(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *vendor_host =
						sdhci_pltfm_priv(pltfm_host);

	_ggc_reset_tuning_result_for_dll(host);

	vendor_host->ggc.sdr50.bus_mode = SD_FNC_AM_SDR50;
	vendor_host->ggc.sdr104.bus_mode = SD_FNC_AM_SDR104;
	vendor_host->ggc.driver_strength_reinit_flg = 0;
	vendor_host->ggc.cur_bus_mode = NULL;
	vendor_host->ggc.dll_unlock_reinit_flg = 0;
	vendor_host->ggc.tuning_cmd7_timeout_reinit_flg = 0;
	vendor_host->ggc.tuning_cmd7_timeout_reinit_cnt = 0;
	vendor_host->ggc.sdr50_notuning_sela_inject_flag = 1;
	vendor_host->ggc.sdr50_notuning_crc_error_flag = 0;
	vendor_host->ggc.sdr50_notuning_sela_rx_inject =
						vendor_host->ggc.bh201_sdr50_sela_sw_inject;
}

static void bht_load_hw_inject(struct mmc_host *mmc_host,
	struct mmc_card *card, u32 *cfg_data, int data_len,
	u32 sel200, u32 sel100)
{
	struct sdhci_host *host = mmc_priv(mmc_host);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *vendor_host =
						sdhci_pltfm_priv(pltfm_host);
	u32 gg_hw_inj[16];

	memcpy(gg_hw_inj, ggc_cfg_data, sizeof(ggc_cfg_data));
	gg_hw_inj[1] = 0x7364032;
	gg_hw_inj[11] = vendor_host->ggc.bh201_sdr104_selb_hw_inject;
	gg_hw_inj[12] = vendor_host->ggc.bh201_sdr50_selb_hw_inject;
	gg_hw_inj[15] = vendor_host->ggc.bh201_drive_strength;

	bht_update_cfg(mmc_host, card, gg_hw_inj, data_len);
}

static void bht_load(struct mmc_host *mmc_host, struct mmc_card *card)
{
	struct sdhci_host *host = mmc_priv(mmc_host);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *vendor_host =
						sdhci_pltfm_priv(pltfm_host);
	u8 driver_strength_reinit_flg =
				vendor_host->ggc.driver_strength_reinit_flg;
	int cur_dll_voltage_idx = vendor_host->ggc.cur_dll_voltage_idx;
	u32 i = 0;
	u32 reg;
	u32 gg_sw_def[16];
	u8 data[512];
	static const int s_dll_voltage_cfg[4][2] = {
		{0x30503106, 0x64141711},
		{0x31503106, 0x64141711},
		{0x30503106, 0x64141751},
		{0x31503106, 0x64141751},
	};

	memcpy(gg_sw_def, ggc_cfg_data, sizeof(ggc_cfg_data));

	pr_debug("%s: Load BHT patch\n", mmc_hostname(mmc_host));

	mmc_send_relative_addr(mmc_host, &card->rca);
	mmc_host->card = card;
	if (vendor_host->ggc.dll_unlock_reinit_flg) {
		pr_debug("dll unlock reinit: idx=%d\n",
				cur_dll_voltage_idx);
		ggc_tuning_result_reset(host);
		gg_sw_def[8] = s_dll_voltage_cfg[cur_dll_voltage_idx][0];
		gg_sw_def[9] = s_dll_voltage_cfg[cur_dll_voltage_idx][1];
	}
	if (vendor_host->ggc.driver_strength_reinit_flg) {
		pr_debug("%s: driver strength should be init to %d\n",
			mmc_hostname(mmc_host), driver_strength_reinit_flg);
		ggc_tuning_result_reset(host);
		if (vendor_host->ggc.driver_strength_reinit_flg <= 7) {
			gg_sw_def[15] &= 0x0f0fffff;
			gg_sw_def[15] |= (driver_strength_reinit_flg << 28)
					| (driver_strength_reinit_flg << 20);
		}
	}
	driver_send_command7(host);
	if (vendor_host->ggc.tuning_cmd7_timeout_reinit_flg == 0
				&& vendor_host->ggc.selx_tuning_done_flag == 0) {
		bht_load_hw_inject(mmc_host, card, gg_sw_def,
			sizeof(gg_sw_def), 0x3ff, 0x77f);
		bht_update_cfg(mmc_host, card, gg_sw_def, sizeof(gg_sw_def));
		set_gg_reg_cur_val(&vendor_host->ggc, (u8 *)gg_sw_def, sizeof(gg_sw_def));
	} else {
		if (vendor_host->ggc.selx_tuning_done_flag)
			pr_debug("%s: skip load default configuration for tuning done\n",
				mmc_hostname(mmc_host));
		if (vendor_host->ggc.tuning_cmd7_timeout_reinit_flg) {
			pr_debug("%s: write previous inject results to bh201 for cmd7 timeout flag is set\n",
				mmc_hostname(mmc_host));
			get_gg_reg_cur_val(&vendor_host->ggc, data, sizeof(gg_sw_def));

			pr_debug("%s: dump config data before write to bh201\n", __func__);
			for (i = 0; i < 128; i++) {
				memcpy(&reg, data+i*sizeof(u32), sizeof(u32));
				pr_debug("ggc_reg32[%03d]=0x%08x\n", i, reg);
			}
			bht_update_cfg(mmc_host, card, (u32 *)data, sizeof(data));
		}
	}
}

static void bh201_signal_voltage_on_off(struct sdhci_host *host, u32 on_off)
{
	int card_present_status = 0;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *vendor_host = sdhci_pltfm_priv(pltfm_host);

	if (gpio_is_valid(vendor_host->ggc.det_gpio)) {
		card_present_status = gpio_get_value(vendor_host->ggc.det_gpio);
		pr_debug("%s: detect_gpio pin %d status is %d\n",
		mmc_hostname(host->mmc), vendor_host->ggc.det_gpio, card_present_status);
	} else {
		pr_err("%s: no det_gpio provided\n", mmc_hostname(host->mmc));
	}

	if (on_off) {
		pr_debug("%s: apply bht power on patch\n", mmc_hostname(host->mmc));

		ggc_dll_voltage_init(host);

		ggc_chip_init(host);

		if (gpio_is_valid(vendor_host->ggc.pwr_gpio)) {
			gpio_direction_output(vendor_host->ggc.pwr_gpio, 1);
			msleep(100);
			pr_debug("%s: pwr_gpio pin %d status is %d\n",
				mmc_hostname(host->mmc), vendor_host->ggc.pwr_gpio,
				gpio_get_value(vendor_host->ggc.pwr_gpio));
		} else {
			pr_err("%s: no pwr_gpio provided\n", mmc_hostname(host->mmc));
		}
	} else {
		pr_debug("%s: apply bht power off patch\n", mmc_hostname(host->mmc));

		ggc_dll_voltage_init(host);

		if (card_present_status <= 0) {
			pr_debug("%s: clear tuning result for power off and card removed\n",
				mmc_hostname(host->mmc));
			ggc_tuning_result_reset(host);
		}
		ggc_chip_init(host);

		if (gpio_is_valid(vendor_host->ggc.pwr_gpio)) {
			gpio_direction_output(vendor_host->ggc.pwr_gpio, 0);
			pr_debug("%s: pwr_gpio pin %d status is %d\n",
			mmc_hostname(host->mmc), vendor_host->ggc.pwr_gpio,
			gpio_get_value(vendor_host->ggc.pwr_gpio));
		} else {
			pr_err("%s: no pwr_gpio provided\n", mmc_hostname(host->mmc));
		}
	}
}

static void sdhci_bh201_parse(struct mmc_host *mmc_host)
{
	struct sdhci_host *host = mmc_priv(mmc_host);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *vendor_host = sdhci_pltfm_priv(pltfm_host);
	struct device_node *np = vendor_host->pdev->dev.of_node;
	struct ggc_reg_op index_array[] = {
		{ 14, 0xffffffff, 0 }, { 46, 0xffffffff, 0 },
		{ 205, 0xffffffff, 0 }, { 237, 0xffffffff, 0 },
		{ 141, 0xf, 0 }, { 145, 0xf, 0 },
		{ 83, 0xfff, 0 }, { 95, 0xfff, 0 },
		{ 126, 0xf, 0 }, { 130, 0xf, 0 },
		{ 140, 0xf, 0 }, { 144, 0xf, 0 },
		{ 183, 0x1, 0 }, { 184, 0x1, 0 },
		{ 171, 0x1, 0 }, { 172, 0x1, 0 },
		{ 173, 0x3f, 0 }, { 357, 0x1, 0 },
		{ 93, 0x7ff, 0 }, { 81, 0x7ff, 0 },
	};

	memcpy(&vendor_host->ggc.pha_stas_rx_low32, &index_array[0],
		sizeof(struct ggc_reg_op));
	memcpy(&vendor_host->ggc.pha_stas_rx_high32, &index_array[1],
		sizeof(struct ggc_reg_op));
	memcpy(&vendor_host->ggc.pha_stas_tx_low32, &index_array[2],
		sizeof(struct ggc_reg_op));
	memcpy(&vendor_host->ggc.pha_stas_tx_high32, &index_array[3],
		sizeof(struct ggc_reg_op));
	memcpy(&vendor_host->ggc.dll_sela_after_mask, &index_array[4],
		sizeof(struct ggc_reg_op));
	memcpy(&vendor_host->ggc.dll_selb_after_mask, &index_array[5],
		sizeof(struct ggc_reg_op));

	memcpy(&vendor_host->ggc.dll_delay_100m_backup, &index_array[6],
		sizeof(struct ggc_reg_op));
	memcpy(&vendor_host->ggc.dll_delay_200m_backup, &index_array[7],
		sizeof(struct ggc_reg_op));

	memcpy(&vendor_host->ggc.dll_sela_100m_cfg, &index_array[8],
		sizeof(struct ggc_reg_op));
	memcpy(&vendor_host->ggc.dll_sela_200m_cfg, &index_array[9],
		sizeof(struct ggc_reg_op));
	memcpy(&vendor_host->ggc.dll_selb_100m_cfg, &index_array[10],
		sizeof(struct ggc_reg_op));
	memcpy(&vendor_host->ggc.dll_selb_200m_cfg, &index_array[11],
		sizeof(struct ggc_reg_op));
	memcpy(&vendor_host->ggc.dll_selb_100m_cfg_en, &index_array[12],
		sizeof(struct ggc_reg_op));
	memcpy(&vendor_host->ggc.dll_selb_200m_cfg_en, &index_array[13],
		sizeof(struct ggc_reg_op));
	memcpy(&vendor_host->ggc.internl_tuning_en_100m, &index_array[14],
		sizeof(struct ggc_reg_op));
	memcpy(&vendor_host->ggc.internl_tuning_en_200m, &index_array[15],
		sizeof(struct ggc_reg_op));
	memcpy(&vendor_host->ggc.cmd19_cnt_cfg, &index_array[16],
		sizeof(struct ggc_reg_op));

	memcpy(&vendor_host->ggc.inject_failure_for_tuning_enable_cfg,
	&index_array[17], sizeof(struct ggc_reg_op));
	memcpy(&vendor_host->ggc.inject_failure_for_200m_tuning_cfg,
	&index_array[18], sizeof(struct ggc_reg_op));
	memcpy(&vendor_host->ggc.inject_failure_for_100m_tuning_cfg,
	&index_array[19], sizeof(struct ggc_reg_op));

	vendor_host->ggc.bh201_drive_strength = 0x3A314177;
	vendor_host->ggc.bh201_sdr50_sela_sw_inject = 0x47F;
	vendor_host->ggc.bh201_sdr50_selb_hw_inject = 0x00725777;
	vendor_host->ggc.bh201_sdr104_selb_hw_inject = 0x57336200;
	host->flags |= SDHCI_SDR50_NEEDS_TUNING;
	host->mmc_host_ops.init_card = bht_load;

	if (of_find_property(np, "bh201_drive_strength", NULL))
		of_property_read_u32_index(np, "bh201_drive_strength", 0,
			&vendor_host->ggc.bh201_drive_strength);
	if (of_find_property(np, "bh201_sdr50_sela_sw_inject", NULL))
		of_property_read_u32_index(np, "bh201_sdr50_sela_sw_inject", 0,
			&vendor_host->ggc.bh201_sdr50_sela_sw_inject);
	if (of_find_property(np, "bh201_sdr50_selb_hw_inject", NULL))
		of_property_read_u32_index(np, "bh201_sdr50_selb_hw_inject", 0,
			&vendor_host->ggc.bh201_sdr50_selb_hw_inject);
	if (of_find_property(np, "bh201_sdr104_selb_hw_inject", NULL))
		of_property_read_u32_index(np, "bh201_sdr104_selb_hw_inject", 0,
			&vendor_host->ggc.bh201_sdr104_selb_hw_inject);

	vendor_host->ggc.pwr_gpio = of_get_named_gpio(np, "pwr-gpios", 0);
	if (!gpio_is_valid(vendor_host->ggc.pwr_gpio))
		dev_err(&vendor_host->pdev->dev, "no pwr-gpio provided !\n");
	else
		dev_info(&vendor_host->pdev->dev, "pwr-gpio provided\n");

	vendor_host->ggc.det_gpio = of_get_named_gpio(np, "det-gpios", 0);
	if (!gpio_is_valid(vendor_host->ggc.det_gpio))
		dev_err(&vendor_host->pdev->dev, "no det-gpio provided !\n");
	else
		dev_info(&vendor_host->pdev->dev, "det-gpio provided\n");

	if (gpio_is_valid(vendor_host->ggc.pwr_gpio)) {
		devm_gpio_request_one(&vendor_host->pdev->dev, vendor_host->ggc.pwr_gpio,
			GPIOF_OUT_INIT_LOW, "sprd-1-pwr");
		pr_debug("%s: detect_gpio pin %d\n",
			mmc_hostname(host->mmc), vendor_host->ggc.pwr_gpio);
	} else {
		pr_err("%s: no detect_gpio provided\n",
			mmc_hostname(host->mmc));
	}

	if (gpio_is_valid(vendor_host->ggc.det_gpio)) {
		devm_gpio_request_one(&vendor_host->pdev->dev, vendor_host->ggc.det_gpio,
		GPIOF_DIR_IN, "sprd-1-det");
		pr_debug("%s: detect_gpio pin %d\n",
			mmc_hostname(host->mmc), vendor_host->ggc.det_gpio);
	} else {
		pr_err("%s: no detect_gpio provided\n",
			mmc_hostname(host->mmc));
	}
}
/* Bayhub patch part-2 end */

/*
 * The code porting from mmc/core start
 * We need to porting the code from sdhci-msm.c for below reasons:
 * 1.ACMD42 should be added between Bus width switch and transfer mode switch
 * 2.Bus width switch should at front of transfer mode switch during mode switch stage
 * 3.If above two points can be patched to sd.c directly, we can decrease much redundant code
 * 4.Above changes match with SD host spec, but don’t match with current sd initialazation flow
 */
static const unsigned int freqs[] = { 400000, 300000, 200000, 100000 };
#define SD_POWEROFF_NOTIFY_TIMEOUT_MS 2000
#define SD_WRITE_EXTR_SINGLE_TIMEOUT_MS 1000

struct sd_busy_data {
	struct mmc_card *card;
	u8 *reg_buf;
};

static bool mmc_sd_card_using_v18(struct mmc_card *card)
{
	/*
	 * According to the SD spec., the Bus Speed Mode (function group 1) bits
	 * 2 to 4 are zero if the card is initialized at 3.3V signal level. Thus
	 * they can be used to determine if the card has already switched to
	 * 1.8V signaling.
	 */
	return card->sw_caps.sd3_bus_mode &
	       (SD_MODE_UHS_SDR50 | SD_MODE_UHS_SDR104 | SD_MODE_UHS_DDR50);
}

/*
 * Fetches and decodes switch information
 */
static int mmc_read_switch(struct mmc_card *card)
{
	int err;
	u8 *status;

	if (card->scr.sda_vsn < SCR_SPEC_VER_1)
		return 0;

	if (!(card->csd.cmdclass & CCC_SWITCH)) {
		pr_warn("%s: card lacks mandatory switch function, performance might suffer\n",
			mmc_hostname(card->host));
		return 0;
	}

	status = kmalloc(64, GFP_KERNEL);
	if (!status)
		return -ENOMEM;

	/*
	 * Find out the card's support bits with a mode 0 operation.
	 * The argument does not matter, as the support bits do not
	 * change with the arguments.
	 */
	err = mmc_sd_switch(card, 0, 0, 0, status);
	if (err) {
		/*
		 * If the host or the card can't do the switch,
		 * fail more gracefully.
		 */
		if (err != -EINVAL && err != -EFAULT)
			goto out;

		pr_warn("%s: problem reading Bus Speed modes\n",
			mmc_hostname(card->host));
		err = 0;

		goto out;
	}

	if (status[13] & SD_MODE_HIGH_SPEED)
		card->sw_caps.hs_max_dtr = HIGH_SPEED_MAX_DTR;

	if (card->scr.sda_spec3) {
		card->sw_caps.sd3_bus_mode = status[13];
		/* Driver Strengths supported by the card */
		card->sw_caps.sd3_drv_type = status[9];
		card->sw_caps.sd3_curr_limit = status[7] | status[6] << 8;
	}

out:
	kfree(status);

	return err;
}

static int sd_select_driver_type(struct mmc_card *card, u8 *status)
{
	int card_drv_type, drive_strength, drv_type;
	int err;

	card->drive_strength = 0;

	card_drv_type = card->sw_caps.sd3_drv_type | SD_DRIVER_TYPE_B;

	drive_strength = mmc_select_drive_strength(card,
						   card->sw_caps.uhs_max_dtr,
						   card_drv_type, &drv_type);

	if (drive_strength) {
		err = mmc_sd_switch(card, 1, 2, drive_strength, status);
		if (err)
			return err;
		if ((status[15] & 0xF) != drive_strength) {
			pr_warn("%s: Problem setting drive strength!\n",
				mmc_hostname(card->host));
			return 0;
		}
		card->drive_strength = drive_strength;
	}

	if (drv_type)
		mmc_set_driver_type(card->host, drv_type);

	return 0;
}

static void sd_update_bus_speed_mode(struct mmc_card *card)
{
	/*
	 * If the host doesn't support any of the UHS-I modes, fallback on
	 * default speed.
	 */
	if (!mmc_host_uhs(card->host)) {
		card->sd_bus_speed = 0;
		return;
	}

	if ((card->host->caps & MMC_CAP_UHS_SDR104) &&
		(card->sw_caps.sd3_bus_mode & SD_MODE_UHS_SDR104)) {
		card->sd_bus_speed = UHS_SDR104_BUS_SPEED;
	} else if ((card->host->caps & MMC_CAP_UHS_DDR50) &&
		(card->sw_caps.sd3_bus_mode & SD_MODE_UHS_DDR50)) {
		card->sd_bus_speed = UHS_DDR50_BUS_SPEED;
	} else if ((card->host->caps & (MMC_CAP_UHS_SDR104 |
		MMC_CAP_UHS_SDR50)) && (card->sw_caps.sd3_bus_mode &
		SD_MODE_UHS_SDR50)) {
		card->sd_bus_speed = UHS_SDR50_BUS_SPEED;
	} else if ((card->host->caps & (MMC_CAP_UHS_SDR104 |
		MMC_CAP_UHS_SDR50 | MMC_CAP_UHS_SDR25)) &&
		(card->sw_caps.sd3_bus_mode & SD_MODE_UHS_SDR25)) {
		card->sd_bus_speed = UHS_SDR25_BUS_SPEED;
	} else if ((card->host->caps & (MMC_CAP_UHS_SDR104 |
		MMC_CAP_UHS_SDR50 | MMC_CAP_UHS_SDR25 |
		MMC_CAP_UHS_SDR12)) && (card->sw_caps.sd3_bus_mode &
		SD_MODE_UHS_SDR12)) {
		card->sd_bus_speed = UHS_SDR12_BUS_SPEED;
	}
}

static int sd_set_bus_speed_mode(struct mmc_card *card, u8 *status)
{
	int err;
	unsigned int timing = 0;

	switch (card->sd_bus_speed) {
	case UHS_SDR104_BUS_SPEED:
		timing = MMC_TIMING_UHS_SDR104;
		card->sw_caps.uhs_max_dtr = UHS_SDR104_MAX_DTR;
		break;
	case UHS_DDR50_BUS_SPEED:
		timing = MMC_TIMING_UHS_DDR50;
		card->sw_caps.uhs_max_dtr = UHS_DDR50_MAX_DTR;
		break;
	case UHS_SDR50_BUS_SPEED:
		timing = MMC_TIMING_UHS_SDR50;
		card->sw_caps.uhs_max_dtr = UHS_SDR50_MAX_DTR;
		break;
	case UHS_SDR25_BUS_SPEED:
		timing = MMC_TIMING_UHS_SDR25;
		card->sw_caps.uhs_max_dtr = UHS_SDR25_MAX_DTR;
		break;
	case UHS_SDR12_BUS_SPEED:
		timing = MMC_TIMING_UHS_SDR12;
		card->sw_caps.uhs_max_dtr = UHS_SDR12_MAX_DTR;
		break;
	default:
		return 0;
	}

	err = mmc_sd_switch(card, 1, 0, card->sd_bus_speed, status);
	if (err)
		return err;

	if ((status[16] & 0xF) != card->sd_bus_speed)
		pr_warn("%s: Problem setting bus speed mode!\n",
			mmc_hostname(card->host));
	else {
		mmc_set_timing(card->host, timing);
		mmc_set_clock(card->host, card->sw_caps.uhs_max_dtr);
	}

	return 0;
}

/* Get host's max current setting at its current voltage */
static u32 sd_get_host_max_current(struct mmc_host *host)
{
	u32 voltage, max_current;

	voltage = 1 << host->ios.vdd;
	switch (voltage) {
	case MMC_VDD_165_195:
		max_current = host->max_current_180;
		break;
	case MMC_VDD_29_30:
	case MMC_VDD_30_31:
		max_current = host->max_current_300;
		break;
	case MMC_VDD_32_33:
	case MMC_VDD_33_34:
		max_current = host->max_current_330;
		break;
	default:
		max_current = 0;
	}

	return max_current;
}

static int sd_set_current_limit(struct mmc_card *card, u8 *status)
{
	int current_limit = SD_SET_CURRENT_NO_CHANGE;
	int err;
	u32 max_current;

	/*
	 * Current limit switch is only defined for SDR50, SDR104, and DDR50
	 * bus speed modes. For other bus speed modes, we do not change the
	 * current limit.
	 */
	if ((card->sd_bus_speed != UHS_SDR50_BUS_SPEED) &&
	    (card->sd_bus_speed != UHS_SDR104_BUS_SPEED) &&
	    (card->sd_bus_speed != UHS_DDR50_BUS_SPEED))
		return 0;

	/*
	 * Host has different current capabilities when operating at
	 * different voltages, so find out its max current first.
	 */
	max_current = sd_get_host_max_current(card->host);

	/*
	 * We only check host's capability here, if we set a limit that is
	 * higher than the card's maximum current, the card will be using its
	 * maximum current, e.g. if the card's maximum current is 300ma, and
	 * when we set current limit to 200ma, the card will draw 200ma, and
	 * when we set current limit to 400/600/800ma, the card will draw its
	 * maximum 300ma from the host.
	 *
	 * The above is incorrect: if we try to set a current limit that is
	 * not supported by the card, the card can rightfully error out the
	 * attempt, and remain at the default current limit.  This results
	 * in a 300mA card being limited to 200mA even though the host
	 * supports 800mA. Failures seen with SanDisk 8GB UHS cards with
	 * an iMX6 host. --rmk
	 */
	if (max_current >= 800 &&
	    card->sw_caps.sd3_curr_limit & SD_MAX_CURRENT_800)
		current_limit = SD_SET_CURRENT_LIMIT_800;
	else if (max_current >= 600 &&
		 card->sw_caps.sd3_curr_limit & SD_MAX_CURRENT_600)
		current_limit = SD_SET_CURRENT_LIMIT_600;
	else if (max_current >= 400 &&
		 card->sw_caps.sd3_curr_limit & SD_MAX_CURRENT_400)
		current_limit = SD_SET_CURRENT_LIMIT_400;
	else if (max_current >= 200 &&
		 card->sw_caps.sd3_curr_limit & SD_MAX_CURRENT_200)
		current_limit = SD_SET_CURRENT_LIMIT_200;

	if (current_limit != SD_SET_CURRENT_NO_CHANGE) {
		err = mmc_sd_switch(card, 1, 3, current_limit, status);
		if (err)
			return err;

		if (((status[15] >> 4) & 0x0F) != current_limit)
			pr_warn("%s: Problem setting current limit!\n",
				mmc_hostname(card->host));

	}

	return 0;
}

static int mmc_wait_for_app_cmd(struct mmc_host *host, struct mmc_card *card,
				struct mmc_command *cmd)
{
	struct mmc_request mrq = {};
	int i, err = -EIO;

	/*
	 * We have to resend MMC_APP_CMD for each attempt so
	 * we cannot use the retries field in mmc_command.
	 */
	for (i = 0; i <= MMC_CMD_RETRIES; i++) {
		err = mmc_app_cmd(host, card);
		if (err) {
			/* no point in retrying; no APP commands allowed */
			if (mmc_host_is_spi(host)) {
				if (cmd->resp[0] & R1_SPI_ILLEGAL_COMMAND)
					break;
			}
			continue;
		}

		memset(&mrq, 0, sizeof(struct mmc_request));

		memset(cmd->resp, 0, sizeof(cmd->resp));
		cmd->retries = 0;

		mrq.cmd = cmd;
		cmd->data = NULL;

		mmc_wait_for_req(host, &mrq);

		err = cmd->error;
		if (!cmd->error)
			break;

		/* no point in retrying illegal APP commands */
		if (mmc_host_is_spi(host)) {
			if (cmd->resp[0] & R1_SPI_ILLEGAL_COMMAND)
				break;
		}
	}

	return err;
}

int mmc_app_set_clr_card_detect(struct mmc_card *card)
{
	struct mmc_command cmd = {};

	cmd.opcode = 42;
	cmd.flags = MMC_RSP_R1 | MMC_CMD_AC;

	return mmc_wait_for_app_cmd(card->host, card, &cmd);
}
/*
 * UHS-I specific initialization procedure
 */
static int mmc_sd_init_uhs_card(struct mmc_card *card)
{
	int err;
	u8 *status;

	if (!(card->csd.cmdclass & CCC_SWITCH))
		return 0;

	status = kmalloc(64, GFP_KERNEL);
	if (!status)
		return -ENOMEM;

	/* Set 4-bit bus width */
	err = mmc_app_set_bus_width(card, MMC_BUS_WIDTH_4);
	if (err)
		goto out;

	mmc_set_bus_width(card->host, MMC_BUS_WIDTH_4);

	/*
	 * Select the bus speed mode depending on host
	 * and card capability.
	 */
	sd_update_bus_speed_mode(card);

	/* GGC chip need this command to switch mode */
	mmc_app_set_clr_card_detect(card);

	/* Set the driver strength for the card */
	err = sd_select_driver_type(card, status);
	if (err)
		goto out;

	/* Set current limit for the card */
	err = sd_set_current_limit(card, status);
	if (err)
		goto out;

	/* Set bus speed mode of the card */
	err = sd_set_bus_speed_mode(card, status);
	if (err)
		goto out;

	/*
	 * SPI mode doesn't define CMD19 and tuning is only valid for SDR50 and
	 * SDR104 mode SD-cards. Note that tuning is mandatory for SDR104.
	 */
	if (!mmc_host_is_spi(card->host) &&
		(card->host->ios.timing == MMC_TIMING_UHS_SDR50 ||
		 card->host->ios.timing == MMC_TIMING_UHS_DDR50 ||
		 card->host->ios.timing == MMC_TIMING_UHS_SDR104)) {
		err = mmc_execute_tuning(card);

		/*
		 * As SD Specifications Part1 Physical Layer Specification
		 * Version 3.01 says, CMD19 tuning is available for unlocked
		 * cards in transfer state of 1.8V signaling mode. The small
		 * difference between v3.00 and 3.01 spec means that CMD19
		 * tuning is also available for DDR50 mode.
		 */
		if (err && card->host->ios.timing == MMC_TIMING_UHS_DDR50) {
			pr_warn("%s: ddr50 tuning failed\n",
				mmc_hostname(card->host));
			err = 0;
		}
	}

out:
	kfree(status);

	return err;
}

static int sd_write_ext_reg(struct mmc_card *card, u8 fno, u8 page, u16 offset,
			    u8 reg_data)
{
	struct mmc_host *host = card->host;
	struct mmc_request mrq = {};
	struct mmc_command cmd = {};
	struct mmc_data data = {};
	struct scatterlist sg;
	u8 *reg_buf;

	reg_buf = kzalloc(512, GFP_KERNEL);
	if (!reg_buf)
		return -ENOMEM;

	mrq.cmd = &cmd;
	mrq.data = &data;

	/*
	 * Arguments of CMD49:
	 * [31:31] MIO (0 = memory).
	 * [30:27] FNO (function number).
	 * [26:26] MW - mask write mode (0 = disable).
	 * [25:18] page number.
	 * [17:9] offset address.
	 * [8:0] length (0 = 1 byte).
	 */
	cmd.arg = fno << 27 | page << 18 | offset << 9;

	/* The first byte in the buffer is the data to be written. */
	reg_buf[0] = reg_data;

	data.flags = MMC_DATA_WRITE;
	data.blksz = 512;
	data.blocks = 1;
	data.sg = &sg;
	data.sg_len = 1;
	sg_init_one(&sg, reg_buf, 512);

	cmd.opcode = SD_WRITE_EXTR_SINGLE;
	cmd.flags = MMC_RSP_R1 | MMC_CMD_ADTC;

	mmc_set_data_timeout(&data, card);
	mmc_wait_for_req(host, &mrq);

	kfree(reg_buf);

	/*
	 * Note that, the SD card is allowed to signal busy on DAT0 up to 1s
	 * after the CMD49. Although, let's leave this to be managed by the
	 * caller.
	 */

	if (cmd.error)
		return cmd.error;
	if (data.error)
		return data.error;

	return 0;
}

static int sd_read_ext_reg(struct mmc_card *card, u8 fno, u8 page,
			   u16 offset, u16 len, u8 *reg_buf)
{
	u32 cmd_args;

	/*
	 * Command arguments of CMD48:
	 * [31:31] MIO (0 = memory).
	 * [30:27] FNO (function number).
	 * [26:26] reserved (0).
	 * [25:18] page number.
	 * [17:9] offset address.
	 * [8:0] length (0 = 1 byte, 1ff = 512 bytes).
	 */
	cmd_args = fno << 27 | page << 18 | offset << 9 | (len - 1);

	return mmc_send_adtc_data(card, card->host, SD_READ_EXTR_SINGLE,
				  cmd_args, reg_buf, 512);
}

static int sd_parse_ext_reg_power(struct mmc_card *card, u8 fno, u8 page,
				  u16 offset)
{
	int err;
	u8 *reg_buf;

	reg_buf = kzalloc(512, GFP_KERNEL);
	if (!reg_buf)
		return -ENOMEM;

	/* Read the extension register for power management function. */
	err = sd_read_ext_reg(card, fno, page, offset, 512, reg_buf);
	if (err) {
		pr_warn("%s: error %d reading PM func of ext reg\n",
			mmc_hostname(card->host), err);
		goto out;
	}

	/* PM revision consists of 4 bits. */
	card->ext_power.rev = reg_buf[0] & 0xf;

	/* Power Off Notification support at bit 4. */
	if (reg_buf[1] & BIT(4))
		card->ext_power.feature_support |= SD_EXT_POWER_OFF_NOTIFY;

	/* Power Sustenance support at bit 5. */
	if (reg_buf[1] & BIT(5))
		card->ext_power.feature_support |= SD_EXT_POWER_SUSTENANCE;

	/* Power Down Mode support at bit 6. */
	if (reg_buf[1] & BIT(6))
		card->ext_power.feature_support |= SD_EXT_POWER_DOWN_MODE;

	card->ext_power.fno = fno;
	card->ext_power.page = page;
	card->ext_power.offset = offset;

out:
	kfree(reg_buf);
	return err;
}

static int sd_parse_ext_reg_perf(struct mmc_card *card, u8 fno, u8 page,
				 u16 offset)
{
	int err;
	u8 *reg_buf;

	reg_buf = kzalloc(512, GFP_KERNEL);
	if (!reg_buf)
		return -ENOMEM;

	err = sd_read_ext_reg(card, fno, page, offset, 512, reg_buf);
	if (err) {
		pr_warn("%s: error %d reading PERF func of ext reg\n",
			mmc_hostname(card->host), err);
		goto out;
	}

	/* PERF revision. */
	card->ext_perf.rev = reg_buf[0];

	/* FX_EVENT support at bit 0. */
	if (reg_buf[1] & BIT(0))
		card->ext_perf.feature_support |= SD_EXT_PERF_FX_EVENT;

	/* Card initiated self-maintenance support at bit 0. */
	if (reg_buf[2] & BIT(0))
		card->ext_perf.feature_support |= SD_EXT_PERF_CARD_MAINT;

	/* Host initiated self-maintenance support at bit 1. */
	if (reg_buf[2] & BIT(1))
		card->ext_perf.feature_support |= SD_EXT_PERF_HOST_MAINT;

	/* Cache support at bit 0. */
	if (reg_buf[4] & BIT(0))
		card->ext_perf.feature_support |= SD_EXT_PERF_CACHE;

	/* Command queue support indicated via queue depth bits (0 to 4). */
	if (reg_buf[6] & 0x1f)
		card->ext_perf.feature_support |= SD_EXT_PERF_CMD_QUEUE;

	card->ext_perf.fno = fno;
	card->ext_perf.page = page;
	card->ext_perf.offset = offset;

out:
	kfree(reg_buf);
	return err;
}

static int sd_parse_ext_reg(struct mmc_card *card, u8 *gen_info_buf,
			    u16 *next_ext_addr)
{
	u8 num_regs, fno, page;
	u16 sfc, offset, ext = *next_ext_addr;
	u32 reg_addr;

	/*
	 * Parse only one register set per extension, as that is sufficient to
	 * support the standard functions. This means another 48 bytes in the
	 * buffer must be available.
	 */
	if (ext + 48 > 512)
		return -EFAULT;

	/* Standard Function Code */
	memcpy(&sfc, &gen_info_buf[ext], 2);

	/* Address to the next extension. */
	memcpy(next_ext_addr, &gen_info_buf[ext + 40], 2);

	/* Number of registers for this extension. */
	num_regs = gen_info_buf[ext + 42];

	/* We support only one register per extension. */
	if (num_regs != 1)
		return 0;

	/* Extension register address. */
	memcpy(&reg_addr, &gen_info_buf[ext + 44], 4);

	/* 9 bits (0 to 8) contains the offset address. */
	offset = reg_addr & 0x1ff;

	/* 8 bits (9 to 16) contains the page number. */
	page = reg_addr >> 9 & 0xff;

	/* 4 bits (18 to 21) contains the function number. */
	fno = reg_addr >> 18 & 0xf;

	/* Standard Function Code for power management. */
	if (sfc == 0x1)
		return sd_parse_ext_reg_power(card, fno, page, offset);

	/* Standard Function Code for performance enhancement. */
	if (sfc == 0x2)
		return sd_parse_ext_reg_perf(card, fno, page, offset);

	return 0;
}

static int sd_read_ext_regs(struct mmc_card *card)
{
	int err, i;
	u8 num_ext, *gen_info_buf;
	u16 rev, len, next_ext_addr;

	if (mmc_host_is_spi(card->host))
		return 0;

	if (!(card->scr.cmds & SD_SCR_CMD48_SUPPORT))
		return 0;

	gen_info_buf = kzalloc(512, GFP_KERNEL);
	if (!gen_info_buf)
		return -ENOMEM;

	/*
	 * Read 512 bytes of general info, which is found at function number 0,
	 * at page 0 and with no offset.
	 */
	err = sd_read_ext_reg(card, 0, 0, 0, 512, gen_info_buf);
	if (err) {
		pr_warn("%s: error %d reading general info of SD ext reg\n",
			mmc_hostname(card->host), err);
		goto out;
	}

	/* General info structure revision. */
	memcpy(&rev, &gen_info_buf[0], 2);

	/* Length of general info in bytes. */
	memcpy(&len, &gen_info_buf[2], 2);

	/* Number of extensions to be find. */
	num_ext = gen_info_buf[4];

	/* We support revision 0, but limit it to 512 bytes for simplicity. */
	if (rev != 0 || len > 512) {
		pr_warn("%s: non-supported SD ext reg layout\n",
			mmc_hostname(card->host));
		goto out;
	}

	/*
	 * Parse the extension registers. The first extension should start
	 * immediately after the general info header (16 bytes).
	 */
	next_ext_addr = 16;
	for (i = 0; i < num_ext; i++) {
		err = sd_parse_ext_reg(card, gen_info_buf, &next_ext_addr);
		if (err) {
			pr_warn("%s: error %d parsing SD ext reg\n",
				mmc_hostname(card->host), err);
			goto out;
		}
	}

out:
	kfree(gen_info_buf);
	return err;
}

static bool sd_cache_enabled(struct mmc_host *host)
{
	return host->card->ext_perf.feature_enabled & SD_EXT_PERF_CACHE;
}

static int sd_flush_cache(struct mmc_host *host)
{
	struct mmc_card *card = host->card;
	u8 *reg_buf, fno, page;
	u16 offset;
	int err;

	if (!sd_cache_enabled(host))
		return 0;

	reg_buf = kzalloc(512, GFP_KERNEL);
	if (!reg_buf)
		return -ENOMEM;

	/*
	 * Set Flush Cache at bit 0 in the performance enhancement register at
	 * 261 bytes offset.
	 */
	fno = card->ext_perf.fno;
	page = card->ext_perf.page;
	offset = card->ext_perf.offset + 261;

	err = sd_write_ext_reg(card, fno, page, offset, BIT(0));
	if (err) {
		pr_warn("%s: error %d writing Cache Flush bit\n",
			mmc_hostname(host), err);
		goto out;
	}

	err = mmc_poll_for_busy(card, SD_WRITE_EXTR_SINGLE_TIMEOUT_MS, false,
				MMC_BUSY_EXTR_SINGLE);
	if (err)
		goto out;

	/*
	 * Read the Flush Cache bit. The card shall reset it, to confirm that
	 * it's has completed the flushing of the cache.
	 */
	err = sd_read_ext_reg(card, fno, page, offset, 1, reg_buf);
	if (err) {
		pr_warn("%s: error %d reading Cache Flush bit\n",
			mmc_hostname(host), err);
		goto out;
	}

	if (reg_buf[0] & BIT(0))
		err = -ETIMEDOUT;
out:
	kfree(reg_buf);
	return err;
}

static int sd_enable_cache(struct mmc_card *card)
{
	u8 *reg_buf;
	int err;

	card->ext_perf.feature_enabled &= ~SD_EXT_PERF_CACHE;

	reg_buf = kzalloc(512, GFP_KERNEL);
	if (!reg_buf)
		return -ENOMEM;

	/*
	 * Set Cache Enable at bit 0 in the performance enhancement register at
	 * 260 bytes offset.
	 */
	err = sd_write_ext_reg(card, card->ext_perf.fno, card->ext_perf.page,
			       card->ext_perf.offset + 260, BIT(0));
	if (err) {
		pr_warn("%s: error %d writing Cache Enable bit\n",
			mmc_hostname(card->host), err);
		goto out;
	}

	err = mmc_poll_for_busy(card, SD_WRITE_EXTR_SINGLE_TIMEOUT_MS, false,
				MMC_BUSY_EXTR_SINGLE);
	if (!err)
		card->ext_perf.feature_enabled |= SD_EXT_PERF_CACHE;

out:
	kfree(reg_buf);
	return err;
}

/*
 * Handle the detection and initialisation of a card.
 *
 * In the case of a resume, "oldcard" will contain the card
 * we're trying to reinitialise.
 */
static int mmc_sd_init_card_bayhub(struct mmc_host *host, u32 ocr,
	struct mmc_card *oldcard)
{
	struct mmc_card *card;
	int err;
	u32 cid[4];
	u32 rocr = 0;
	bool v18_fixup_failed = false;

	WARN_ON(!host->claimed);
retry:
	err = mmc_sd_get_cid(host, ocr, cid, &rocr);
	if (err)
		return err;

	if (oldcard) {
		if (memcmp(cid, oldcard->raw_cid, sizeof(cid)) != 0) {
			pr_debug("%s: Perhaps the card was replaced\n",
				mmc_hostname(host));
			return -ENOENT;
		}

		card = oldcard;
	} else {
		/*
		 * Allocate card structure.
		 */
		card = mmc_alloc_card(host, &sd_type);
		if (IS_ERR(card))
			return PTR_ERR(card);

		card->ocr = ocr;
		card->type = MMC_TYPE_SD;
		memcpy(card->raw_cid, cid, sizeof(card->raw_cid));
	}

	/*
	 * Call the optional HC's init_card function to handle quirks.
	 */
	if (host->ops->init_card)
		host->ops->init_card(host, card);

	/*
	 * For native busses:  get card RCA and quit open drain mode.
	 */
	if (!mmc_host_is_spi(host)) {
		err = mmc_send_relative_addr(host, &card->rca);
		if (err)
			goto free_card;
	}

	if (!oldcard) {
		err = mmc_sd_get_csd(card);
		if (err)
			goto free_card;

		mmc_decode_cid(card);
	}

	/*
	 * handling only for cards supporting DSR and hosts requesting
	 * DSR configuration
	 */
	if (card->csd.dsr_imp && host->dsr_req)
		mmc_set_dsr(host);

	/*
	 * Select card, as all following commands rely on that.
	 */
	if (!mmc_host_is_spi(host)) {
		err = mmc_select_card(card);
		if (err)
			goto free_card;
	}

	err = mmc_sd_setup_card(host, card, oldcard != NULL);
	if (err)
		goto free_card;

	/*
	 * If the card has not been power cycled, it may still be using 1.8V
	 * signaling. Detect that situation and try to initialize a UHS-I (1.8V)
	 * transfer mode.
	 */
	if (!v18_fixup_failed && !mmc_host_is_spi(host) && mmc_host_uhs(host) &&
	    mmc_sd_card_using_v18(card) &&
	    host->ios.signal_voltage != MMC_SIGNAL_VOLTAGE_180) {
		/*
		 * Re-read switch information in case it has changed since
		 * oldcard was initialized.
		 */
		if (oldcard) {
			err = mmc_read_switch(card);
			if (err)
				goto free_card;
		}
		if (mmc_sd_card_using_v18(card)) {
			if (mmc_host_set_uhs_voltage(host) ||
			    mmc_sd_init_uhs_card(card)) {
				v18_fixup_failed = true;
				mmc_power_cycle(host, ocr);
				if (!oldcard)
					mmc_remove_card(card);
				goto retry;
			}
			goto done;
		}
	}

	/* Initialization sequence for UHS-I cards */
	if (rocr & SD_ROCR_S18A && mmc_host_uhs(host)) {
		err = mmc_sd_init_uhs_card(card);
		if (err)
			goto free_card;
	} else {
		/*
		 * Switch to wider bus (if supported).
		 * Bayhub patch move mmc_app_set_bus_width in front of mmc_sd_switch_hs
		 */
		if ((host->caps & MMC_CAP_4_BIT_DATA) &&
			(card->scr.bus_widths & SD_SCR_BUS_WIDTH_4)) {
			err = mmc_app_set_bus_width(card, MMC_BUS_WIDTH_4);
			if (err)
				goto free_card;

			mmc_set_bus_width(host, MMC_BUS_WIDTH_4);
		}

		/* GGC chip need this command to switch mode */
		mmc_app_set_clr_card_detect(card);

		/*
		 * Attempt to change to high-speed (if supported)
		 */
		err = mmc_sd_switch_hs(card);
		if (err > 0)
			mmc_set_timing(card->host, MMC_TIMING_SD_HS);
		else if (err)
			goto free_card;

		/*
		 * Set bus speed.
		 */
		mmc_set_clock(host, mmc_sd_get_max_clock(card));
	}

	if (!oldcard) {
		/* Read/parse the extension registers. */
		err = sd_read_ext_regs(card);
		if (err)
			goto free_card;
	}

	/* Enable internal SD cache if supported. */
	if (card->ext_perf.feature_support & SD_EXT_PERF_CACHE) {
		err = sd_enable_cache(card);
		if (err)
			goto free_card;
	}

	if (host->cqe_ops && !host->cqe_enabled) {
		err = host->cqe_ops->cqe_enable(host, card);
		if (!err) {
			host->cqe_enabled = true;
			host->hsq_enabled = true;
			pr_info("%s: Host Software Queue enabled\n",
				mmc_hostname(host));
		}
	}

	if (host->caps2 & MMC_CAP2_AVOID_3_3V &&
	    host->ios.signal_voltage == MMC_SIGNAL_VOLTAGE_330) {
		pr_err("%s: Host failed to negotiate down from 3.3V\n",
			mmc_hostname(host));
		err = -EINVAL;
		goto free_card;
	}
done:
	host->card = card;
	return 0;

free_card:
	if (!oldcard)
		mmc_remove_card(card);

	return err;
}

/*
 * Host is being removed. Free up the current card.
 */
static void mmc_sd_remove(struct mmc_host *host)
{
	mmc_remove_card(host->card);
	host->card = NULL;
}

/*
 * Card detection - card is alive.
 */
static int mmc_sd_alive(struct mmc_host *host)
{
	return mmc_send_status(host->card, NULL);
}

/*
 * Card detection callback from host.
 */
static void mmc_sd_detect(struct mmc_host *host)
{
	int err;

	mmc_get_card(host->card, NULL);

	/*
	 * Just check if our card has been removed.
	 */
	err = _mmc_detect_card_removed(host);

	mmc_put_card(host->card, NULL);

	if (err) {
		mmc_sd_remove(host);

		mmc_claim_host(host);
		mmc_detach_bus(host);
		mmc_power_off(host);
		mmc_release_host(host);
	}
}

static int sd_can_poweroff_notify(struct mmc_card *card)
{
	return card->ext_power.feature_support & SD_EXT_POWER_OFF_NOTIFY;
}

static int sd_busy_poweroff_notify_cb(void *cb_data, bool *busy)
{
	struct sd_busy_data *data = cb_data;
	struct mmc_card *card = data->card;
	int err;

	/*
	 * Read the status register for the power management function. It's at
	 * one byte offset and is one byte long. The Power Off Notification
	 * Ready is bit 0.
	 */
	err = sd_read_ext_reg(card, card->ext_power.fno, card->ext_power.page,
			      card->ext_power.offset + 1, 1, data->reg_buf);
	if (err) {
		pr_warn("%s: error %d reading status reg of PM func\n",
			mmc_hostname(card->host), err);
		return err;
	}

	*busy = !(data->reg_buf[0] & BIT(0));
	return 0;
}

static int sd_poweroff_notify(struct mmc_card *card)
{
	struct sd_busy_data cb_data;
	u8 *reg_buf;
	int err;

	reg_buf = kzalloc(512, GFP_KERNEL);
	if (!reg_buf)
		return -ENOMEM;

	/*
	 * Set the Power Off Notification bit in the power management settings
	 * register at 2 bytes offset.
	 */
	err = sd_write_ext_reg(card, card->ext_power.fno, card->ext_power.page,
			       card->ext_power.offset + 2, BIT(0));
	if (err) {
		pr_warn("%s: error %d writing Power Off Notify bit\n",
			mmc_hostname(card->host), err);
		goto out;
	}

	cb_data.card = card;
	cb_data.reg_buf = reg_buf;
	err = __mmc_poll_for_busy(card, SD_POWEROFF_NOTIFY_TIMEOUT_MS,
				  &sd_busy_poweroff_notify_cb, &cb_data);

out:
	kfree(reg_buf);
	return err;
}

static int _mmc_sd_suspend(struct mmc_host *host)
{
	struct mmc_card *card = host->card;
	int err = 0;

	mmc_claim_host(host);

	if (mmc_card_suspended(card))
		goto out;

	if (sd_can_poweroff_notify(card))
		err = sd_poweroff_notify(card);
	else if (!mmc_host_is_spi(host))
		err = mmc_deselect_cards(host);

	if (!err) {
		mmc_power_off(host);
		mmc_card_set_suspended(card);
	}

out:
	mmc_release_host(host);
	return err;
}

/*
 * Callback for suspend
 */
static int mmc_sd_suspend(struct mmc_host *host)
{
	int err;

	err = _mmc_sd_suspend(host);
	if (!err) {
		pm_runtime_disable(&host->card->dev);
		pm_runtime_set_suspended(&host->card->dev);
	}

	return err;
}

/*
 * This function tries to determine if the same card is still present
 * and, if so, restore all state to it.
 */
static int _mmc_sd_resume(struct mmc_host *host)
{
	int err = 0;

	mmc_claim_host(host);

	if (!mmc_card_suspended(host->card))
		goto out;

	mmc_power_up(host, host->card->ocr);
	err = mmc_sd_init_card_bayhub(host, host->card->ocr, host->card);
	mmc_card_clr_suspended(host->card);

out:
	mmc_release_host(host);
	return err;
}

/*
 * Callback for resume
 */
static int mmc_sd_resume(struct mmc_host *host)
{
	pm_runtime_enable(&host->card->dev);
	return 0;
}

/*
 * Callback for runtime_suspend.
 */
static int mmc_sd_runtime_suspend(struct mmc_host *host)
{
	int err;

	if (!(host->caps & MMC_CAP_AGGRESSIVE_PM))
		return 0;

	err = _mmc_sd_suspend(host);
	if (err)
		pr_err("%s: error %d doing aggressive suspend\n",
			mmc_hostname(host), err);

	return err;
}

/*
 * Callback for runtime_resume.
 */
static int mmc_sd_runtime_resume(struct mmc_host *host)
{
	int err;

	err = _mmc_sd_resume(host);
	if (err && err != -ENOMEDIUM)
		pr_err("%s: error %d doing runtime resume\n",
			mmc_hostname(host), err);

	return 0;
}

static int mmc_sd_hw_reset(struct mmc_host *host)
{
	mmc_power_cycle(host, host->card->ocr);
	return mmc_sd_init_card_bayhub(host, host->card->ocr, host->card);
}

static const struct mmc_bus_ops mmc_sd_ops = {
	.remove = mmc_sd_remove,
	.detect = mmc_sd_detect,
	.runtime_suspend = mmc_sd_runtime_suspend,
	.runtime_resume = mmc_sd_runtime_resume,
	.suspend = mmc_sd_suspend,
	.resume = mmc_sd_resume,
	.alive = mmc_sd_alive,
	.shutdown = mmc_sd_suspend,
	.hw_reset = mmc_sd_hw_reset,
	.cache_enabled = sd_cache_enabled,
	.flush_cache = sd_flush_cache,
};

/*
 * Starting point for SD card init.
 */
static int mmc_attach_sd_bayhub(struct mmc_host *host)
{
	int err;
	u32 ocr, rocr;

	WARN_ON(!host->claimed);

	err = mmc_send_app_op_cond(host, 0, &ocr);
	if (err)
		return err;

	mmc_attach_bus(host, &mmc_sd_ops);
	if (host->ocr_avail_sd)
		host->ocr_avail = host->ocr_avail_sd;

	/*
	 * We need to get OCR a different way for SPI.
	 */
	if (mmc_host_is_spi(host)) {
		mmc_go_idle(host);

		err = mmc_spi_read_ocr(host, 0, &ocr);
		if (err)
			goto err;
	}

	/*
	 * Some SD cards claims an out of spec VDD voltage range. Let's treat
	 * these bits as being in-valid and especially also bit7.
	 */
	ocr &= ~0x7FFF;

	rocr = mmc_select_voltage(host, ocr);

	/*
	 * Can we support the voltage(s) of the card(s)?
	 */
	if (!rocr) {
		err = -EINVAL;
		goto err;
	}

	/*
	 * Detect and init the card.
	 */
	err = mmc_sd_init_card_bayhub(host, rocr, NULL);
	if (err)
		goto err;

	mmc_release_host(host);
	err = mmc_add_card(host->card);
	if (err)
		goto remove_card;

	mmc_claim_host(host);
	return 0;

remove_card:
	mmc_remove_card(host->card);
	host->card = NULL;
	mmc_claim_host(host);
err:
	mmc_detach_bus(host);

	pr_err("%s: error %d whilst initialising SD card\n",
		mmc_hostname(host), err);

	return err;
}

static int mmc_schedule_delayed_work(struct delayed_work *work,
				     unsigned long delay)
{
	/*
	 * We use the system_freezable_wq, because of two reasons.
	 * First, it allows several works (not the same work item) to be
	 * executed simultaneously. Second, the queue becomes frozen when
	 * userspace becomes frozen during system PM.
	 */
	return queue_delayed_work(system_freezable_wq, work, delay);
}

static void mmc_hw_reset_for_init(struct mmc_host *host)
{
	mmc_pwrseq_reset(host);

	if (!(host->caps & MMC_CAP_HW_RESET) || !host->ops->hw_reset)
		return;
	host->ops->hw_reset(host);
}

static int mmc_rescan_try_freq(struct mmc_host *host, unsigned int freq)
{
	host->f_init = freq;

	pr_debug("%s: %s: trying to init card at %u Hz\n",
		mmc_hostname(host), __func__, host->f_init);

	mmc_power_up(host, host->ocr_avail);

	/*
	 * Some eMMCs (with VCCQ always on) may not be reset after power up, so
	 * do a hardware reset if possible.
	 */
	mmc_hw_reset_for_init(host);

	/*
	 * sdio_reset sends CMD52 to reset card.  Since we do not know
	 * if the card is being re-initialized, just send it.  CMD52
	 * should be ignored by SD/eMMC cards.
	 * Skip it if we already know that we do not support SDIO commands
	 */
	if (!(host->caps2 & MMC_CAP2_NO_SDIO))
		sdio_reset(host);

	mmc_go_idle(host);

	if (!(host->caps2 & MMC_CAP2_NO_SD)) {
		if (mmc_send_if_cond_pcie(host, host->ocr_avail))
			goto out;
		if (mmc_card_sd_express(host))
			return 0;
	}

	/* Order's important: probe SDIO, then SD, then MMC */
	if (!(host->caps2 & MMC_CAP2_NO_SDIO))
		if (!mmc_attach_sdio(host))
			return 0;

	if (!(host->caps2 & MMC_CAP2_NO_SD))
		if (!mmc_attach_sd_bayhub(host))
			return 0;

	if (!(host->caps2 & MMC_CAP2_NO_MMC))
		if (!mmc_attach_mmc(host))
			return 0;

out:
	mmc_power_off(host);
	return -EIO;
}

void mmc_rescan_bayhub(struct work_struct *work)
{
	struct mmc_host *host =
		container_of(work, struct mmc_host, detect.work);
	int i;

	if (host->rescan_disable)
		return;

	/* If there is a non-removable card registered, only scan once */
	if (!mmc_card_is_removable(host) && host->rescan_entered)
		return;
	host->rescan_entered = 1;

	if (host->trigger_card_event && host->ops->card_event) {
		mmc_claim_host(host);
		host->ops->card_event(host);
		mmc_release_host(host);
		host->trigger_card_event = false;
	}

	/* Verify a registered card to be functional, else remove it. */
	if (host->bus_ops)
		host->bus_ops->detect(host);

	host->detect_change = 0;

	/* if there still is a card present, stop here */
	if (host->bus_ops != NULL)
		goto out;

	mmc_claim_host(host);
	if (mmc_card_is_removable(host) && host->ops->get_cd &&
			host->ops->get_cd(host) == 0) {
		mmc_power_off(host);
		mmc_release_host(host);
		goto out;
	}

	/* If an SD express card is present, then leave it as is. */
	if (mmc_card_sd_express(host)) {
		mmc_release_host(host);
		goto out;
	}

	for (i = 0; i < ARRAY_SIZE(freqs); i++) {
		unsigned int freq = freqs[i];

		if (freq > host->f_max) {
			if (i + 1 < ARRAY_SIZE(freqs))
				continue;
			freq = host->f_max;
		}
		if (!mmc_rescan_try_freq(host, max(freq, host->f_min)))
			break;
		if (freqs[i] <= host->f_min)
			break;
	}
	mmc_release_host(host);

 out:
	if (host->caps & MMC_CAP_NEEDS_POLL)
		mmc_schedule_delayed_work(&host->detect, HZ);
}

/* The code porting from mmc/core end */

/*
 * Bayhub patch part-3 start
 * The APIs used to select good timing and configure GGC chip
 */
static int card_deselect_card(struct sdhci_host *host)
{
	int ret = -1;
	int err;
	struct mmc_host *mmc = host->mmc;
	struct mmc_command cmd = { 0 };

	cmd.opcode = MMC_SELECT_CARD;
	cmd.arg = 0;
	cmd.flags = MMC_RSP_NONE | MMC_CMD_AC;

	err = mmc_wait_for_cmd(mmc, &cmd, 3);
	if (err)
		pr_err("BHT ERR: CMD7 FAIL: err = %d\n", err);
	else
		ret = 0;

	return ret;
}

static bool enter_exit_emulator_mode(struct sdhci_host *host, bool b_enter)
{
	bool ret = false;
	u8 times = b_enter ? 2 : 1;
	u8 i = 0;

	for (i = 0; i < times; i++) {
		ret = card_deselect_card(host);
		if (ret)
			break;
	}
	return ret;
}

static bool _gg_emulator_read_only(struct sdhci_host *host,
				   u8 *in_data, u32 datalen)
{
	struct mmc_host *mmc = host->mmc;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *vendor_host = sdhci_pltfm_priv(pltfm_host);
	int rc = 0;
	u8 *data1 = kzalloc(PAGE_SIZE, GFP_KERNEL);
	struct mmc_request mrq = { 0 };
	struct mmc_command cmd = { 0 };
	struct mmc_data data = { 0 };
	struct scatterlist sg;

	if (!data1) {
		pr_err("BHT MSG:gg read no memory\n");
		rc = -ENOMEM;
		goto out;
	}

	sg_init_one(&sg, data1, 512);

	cmd.opcode = MMC_READ_SINGLE_BLOCK;
	cmd.arg = 0;
	cmd.flags = MMC_RSP_R1 | MMC_CMD_ADTC;
	data.blksz = 512;
	data.blocks = 1;
	data.flags = MMC_DATA_READ;
	data.timeout_ns = 1000 * 1000 * 1000;
	data.sg = &sg;
	data.sg_len = 1;
	mrq.cmd = &cmd;
	mrq.data = &data;
	mrq.stop = NULL;

	mmc_wait_for_req(mmc, &mrq);
	memcpy(in_data, data1, datalen);

	kfree(data1);

	if ((cmd.error == -EILSEQ) || (data.error == -EILSEQ))
		vendor_host->ggc.sdr50_notuning_crc_error_flag = 1;

	if (cmd.error || data.error)
		rc = -1;
out:
	return rc;
}

static void host_cmddat_line_reset(struct sdhci_host *host)
{
	if (host->ops->reset)
		host->ops->reset(host, SDHCI_RESET_CMD | SDHCI_RESET_DATA);
}

static int gg_select_card_spec(struct sdhci_host *host)
{
	int err;
	struct mmc_command cmd = { 0 };
	struct mmc_card *card = host->mmc->card;

	cmd.opcode = MMC_SELECT_CARD;

	if (card) {
		cmd.arg = card->rca << 16;
		cmd.flags = MMC_RSP_R1 | MMC_CMD_AC;
	} else {
		cmd.arg = 0;
		cmd.flags = MMC_RSP_NONE | MMC_CMD_AC;
	}

	err = mmc_wait_for_cmd(host->mmc, &cmd, 0);
	if (err == -EILSEQ) {
		host_cmddat_line_reset(host);

		memset(&cmd, 0, sizeof(struct mmc_command));
		cmd.opcode = 5;
		cmd.arg = 0;
		cmd.flags =
			MMC_RSP_SPI_R4 | MMC_RSP_R4 | MMC_CMD_BCR;

		mmc_wait_for_cmd(host->mmc, &cmd, 0);

		pr_err("BHT ERR:%s: CMD7 CRC\n", __func__);
		host_cmddat_line_reset(host);
		return 0;
	}
	if (err == -ETIMEDOUT) {
		pr_err("BHT ERR:%s: CMD7 timeout\n", __func__);
		host_cmddat_line_reset(host);
		return err;
	}
	return 0;
}

static bool gg_emulator_read_ext(struct sdhci_host *host, bool *card_status,
					bool *read_status, u8 *data, u32 datalen)
{
	bool ret = false;
	bool card_ret = true;
	bool rd_ret = false;

	if (enter_exit_emulator_mode(host, true) == 0)
		ret = true;
	else
		ret = false;
	if (!ret)
		goto exit;

	if (_gg_emulator_read_only(host, data, datalen) == 0)
		rd_ret = true;
	else
		rd_ret = false;

	if (enter_exit_emulator_mode(host, false) == 0)
		ret = true;
	else
		ret = false;

	if (!ret)
		goto exit;

	if (gg_select_card_spec(host) == 0)
		card_ret = true;
	else
		card_ret = false;

	if (!rd_ret)
		pr_err("BHT ERR:GGC read status error\n");

exit:
	if (!card_ret) {
		pr_err("BHT ERR:GGC Emulator exit Fail!!\n");
		ret = false;
	}

	if (card_status)
		*card_status = ret;

	if (read_status)
		*read_status = rd_ret;

	if (rd_ret && !ret)
		pr_err("BHT ERR:data read ok, but exit NG\n");
	else if (!rd_ret && ret)
		pr_err("BHT ERR:data read NG, but exit ok\n");

	return ret;
}

static void _status_bit_2_bt(int tar, int *byt, int *bit)
{
	*byt = tar / 8;
	*bit = tar % 8;
}

static u32 _read_status_data_read_register(u8 *cfg, struct ggc_reg_op *bts)
{
	u32 rv = 0;
	u32 msk = bts->mask;
	int byt = 0, bit = 0;
	int i = 0;

	do {
		_status_bit_2_bt(bts->ofs + i, &byt, &bit);
		if (cfg[byt] & (1 << bit))
			rv |= 1 << i;

		i++;
		msk >>= 1;
		if (msk == 0)
			break;
	} while (1);
	return rv;
}

static bool ggc_read_registers_ext(struct sdhci_host *host,
		bool *card_status, bool *read_status,
		struct ggc_reg_op *gg_reg_arr, u8 num)
{
	u8 get_idx = 0;
	bool ret = false;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *vendor_host = sdhci_pltfm_priv(pltfm_host);
	struct ggc_adaptor *ggc = &vendor_host->ggc;

	if (read_status)
		*read_status = false;
	if (card_status)
		*card_status = false;

	memset(ggc->cur_read_buf, 0, 512);
	ret = gg_emulator_read_ext(host, card_status, read_status, ggc->cur_read_buf, 512);
	if (read_status == false)
		goto exit;

	for (get_idx = 0; get_idx < num; get_idx++)
		(gg_reg_arr + get_idx)->value =
			_read_status_data_read_register(ggc->cur_read_buf, (gg_reg_arr + get_idx));

exit:
	return ret;
}

static bool gg_emulator_read(struct sdhci_host *host, u8 *data, u32 datalen)
{
	bool ret = false;
	bool rd_ret = false;

	ret = enter_exit_emulator_mode(host, true);
	if (ret)
		goto exit;

	rd_ret = _gg_emulator_read_only(host, data, datalen);

	ret = enter_exit_emulator_mode(host, false);
	if (ret)
		goto exit;

	ret = gg_select_card_spec(host);

exit:
	if (rd_ret)
		pr_err("BHT ERR:GGC read status error\n");

	if (ret)
		pr_err("BHT ERR:GGC Emulator exit Fail!!\n");

	if (rd_ret == 0 && ret) {
		pr_err("BHT ERR:data read ok, but exit NG\n");
		ret = 0;
	}

	if (rd_ret && ret == 0) {
		pr_err("BHT ERR:data read NG, but exit ok\n");
		ret = -1;
	}

	return ret ? false : true;
}

static bool _ggc_emulator_write_only(struct sdhci_host *host,
				     u8 *in_data, u32 datalen)
{
	struct mmc_host *mmc = host->mmc;
	int rc = 0;
	u8 *data1 = kzalloc(PAGE_SIZE, GFP_KERNEL);
	struct mmc_request mrq = { 0 };
	struct mmc_command cmd = { 0 };
	struct mmc_data data = { 0 };
	struct scatterlist sg;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *vendor_host = sdhci_pltfm_priv(pltfm_host);

	if (!data1) {
		pr_err("BHT MSG:gg write no memory\n");
		rc = -ENOMEM;
		goto out;
	}

	memcpy(data1, in_data, datalen);
	sg_init_one(&sg, data1, 512);

	cmd.opcode = MMC_WRITE_BLOCK;
	cmd.arg = 0;
	cmd.flags = MMC_RSP_R1 | MMC_CMD_ADTC;
	data.blksz = 512;
	data.blocks = 1;
	data.flags = MMC_DATA_WRITE;
	data.timeout_ns = 1000 * 1000 * 1000;
	data.sg = &sg;
	data.sg_len = 1;
	mrq.cmd = &cmd;
	mrq.data = &data;
	mrq.stop = NULL;

	mmc_wait_for_req(mmc, &mrq);

	if (cmd.error == -EILSEQ)
		vendor_host->ggc.sdr50_notuning_crc_error_flag = 1;

	kfree(data1);
out:
	return rc;
}

static bool gg_emulator_write(struct sdhci_host *host, u8 *data, u32 datalen)
{
	bool ret = false;
	bool wr_ret = false;
	u32 i = 0;
	u32 reg;

	ret = enter_exit_emulator_mode(host, true);
	if (ret)
		goto exit;

	pr_debug("BHT MSG: dump config data\n");
	for (i = 0; i < (datalen/sizeof(u32)); i++)	{
		memcpy(&reg, data+i*sizeof(u32), sizeof(u32));
		pr_debug("BHT MSG:\tggc_reg32[%03d]=0x%08x\n", i, reg);
	}

	_ggc_emulator_write_only(host, data, datalen);
	wr_ret = true;

	ret = enter_exit_emulator_mode(host, false);
	if (ret)
		goto exit;

	ret = gg_select_card_spec(host);

exit:
	if (wr_ret == false)
		ret = false;

	if (ret == false)
		pr_err("BHT ERR:%s: GGC Emulator Write Fail!!\n", __func__);

	return ret;
}

static bool get_gg_reg_cur(struct sdhci_host *host, u8 *data,
		    struct ggc_reg_op *gg_reg_arr, u8 num)
{
	u8 get_idx = 0;
	bool ret = false;

	/* read ggc register */
	memset(data, 0, 512);
	ret = gg_emulator_read(host, data, 512);

	if (ret == false)
		goto exit;

	/* read the offset bits value */
	for (get_idx = 0; get_idx < num; get_idx++) {
		(gg_reg_arr + get_idx)->value =
		    read_ram_bits_ofs_mask(data, (gg_reg_arr + get_idx));
	}
exit:
	return ret;
}

static void chg_gg_reg_cur_val(struct ggc_adaptor  *ggc, u8 *data,
	struct ggc_reg_op *gg_reg_arr, u8 num, bool b_sav_chg)
{
	u8 chg_idx = 0;

	for (chg_idx = 0; chg_idx < num; chg_idx++) {
		/* modify the ggc register bit value */
		cfg_write_bits_ofs_mask(data, (gg_reg_arr + chg_idx),
					(gg_reg_arr + chg_idx)->value);
	}

	if (b_sav_chg)
		set_gg_reg_cur_val(ggc, data, 64);
}

static void log_bin(u32 n)
{
	int i = 0;
	u8 tb[33] = { 0 };

	for (i = 0; i < 32; i++) {
		if (n & (1 << i))
			tb[i] = '1';
		else
			tb[i] = '0';
	}
	pr_debug("BHT MSG:bin:%s\n", tb);
}

static void phase_str(u8 *tb, u32 n)
{
	int i = 0;

	for (i = 0; i < TUNING_PHASE_SIZE; i++) {
		if (n & (1 << i))
			tb[i] = '1';
		else
			tb[i] = '0';
	}
	tb[TUNING_PHASE_SIZE] = 0;
}

static int get_bit_number(u32 n)
{
	int i = 0;
	int cnt = 0;

	for (i = 0; i < TUNING_PHASE_SIZE; i++) {
		if (n & (1 << i))
			cnt++;
	}
	return cnt;
}

static bool gg_emulator_write_ext(struct sdhci_host *host, bool *card_status, u8 *data, u32 datalen)
{
	bool ret = false;
	bool wr_ret = false;

	ret = enter_exit_emulator_mode(host, true);
	if (ret)
		goto exit;

	_ggc_emulator_write_only(host, data, datalen);
	wr_ret = true;

	ret = enter_exit_emulator_mode(host, false);
	if (ret)
		goto exit;

	ret = (gg_select_card_spec(host) == 0) ? true : false;
	if (ret == false) {
		if (card_status)
			*card_status = false;
	}

exit:
	if (wr_ret == false)
		ret = false;

	if (ret == false)
		pr_err("BHT ERR:%s: GGC Emulator Write Fail!!\n", __func__);

	return ret;
}

static bool ggc_set_output_tuning_phase_ext(struct sdhci_host *host,
		bool *card_status, int sela, int selb)
{
	bool ret = true;
	u8 data[512] = { 0 };
	struct ggc_reg_op gg_reg_arr[8];
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *vendor_host = sdhci_pltfm_priv(pltfm_host);

	get_gg_reg_cur_val(&vendor_host->ggc, data, 64);
	memcpy(&gg_reg_arr[0], &vendor_host->ggc.dll_sela_100m_cfg,
			sizeof(struct ggc_reg_op));
	memcpy(&gg_reg_arr[1], &vendor_host->ggc.dll_sela_200m_cfg,
			sizeof(struct ggc_reg_op));
	memcpy(&gg_reg_arr[2], &vendor_host->ggc.dll_selb_100m_cfg,
			sizeof(struct ggc_reg_op));
	memcpy(&gg_reg_arr[3], &vendor_host->ggc.dll_selb_200m_cfg,
			sizeof(struct ggc_reg_op));
	memcpy(&gg_reg_arr[4], &vendor_host->ggc.dll_selb_100m_cfg_en,
			sizeof(struct ggc_reg_op));
	memcpy(&gg_reg_arr[5], &vendor_host->ggc.dll_selb_200m_cfg_en,
			sizeof(struct ggc_reg_op));
	memcpy(&gg_reg_arr[6], &vendor_host->ggc.internl_tuning_en_100m,
		  sizeof(struct ggc_reg_op));
	memcpy(&gg_reg_arr[7], &vendor_host->ggc.internl_tuning_en_200m,
		  sizeof(struct ggc_reg_op));
	gg_reg_arr[0].value = sela;
	gg_reg_arr[1].value = sela;
	gg_reg_arr[2].value = selb;
	gg_reg_arr[3].value = selb;
	gg_reg_arr[4].value = 1;
	gg_reg_arr[5].value = 1;
	gg_reg_arr[6].value = 1;
	gg_reg_arr[7].value = 1;
	if (card_status)
		*card_status = true;
	chg_gg_reg_cur_val(&vendor_host->ggc, data, gg_reg_arr, 8, true);
	ret = gg_emulator_write_ext(host, card_status, data, 512);
	return ret;
}

static bool gg_fix_output_tuning_phase(struct sdhci_host *host, int sela, int selb)
{
	u8 data[512] = { 0 };
	struct ggc_reg_op gg_reg_arr[10];
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *vendor_host = sdhci_pltfm_priv(pltfm_host);

	pr_debug("BHT MSG: %s - sela dll: %x, selb dll: %x\n", __func__, sela,
		selb);

	get_gg_reg_cur_val(&vendor_host->ggc, data, 64);

	memcpy(&gg_reg_arr[0], &vendor_host->ggc.dll_sela_100m_cfg,
			sizeof(struct ggc_reg_op));
	memcpy(&gg_reg_arr[1], &vendor_host->ggc.dll_sela_200m_cfg,
			sizeof(struct ggc_reg_op));
	memcpy(&gg_reg_arr[2], &vendor_host->ggc.dll_selb_100m_cfg,
			sizeof(struct ggc_reg_op));
	memcpy(&gg_reg_arr[3], &vendor_host->ggc.dll_selb_200m_cfg,
			sizeof(struct ggc_reg_op));
	memcpy(&gg_reg_arr[4], &vendor_host->ggc.dll_selb_100m_cfg_en,
			sizeof(struct ggc_reg_op));
	memcpy(&gg_reg_arr[5], &vendor_host->ggc.dll_selb_200m_cfg_en,
			sizeof(struct ggc_reg_op));
	memcpy(&gg_reg_arr[6], &vendor_host->ggc.internl_tuning_en_100m,
		  sizeof(struct ggc_reg_op));
	memcpy(&gg_reg_arr[7], &vendor_host->ggc.internl_tuning_en_200m,
		  sizeof(struct ggc_reg_op));
	gg_reg_arr[0].value = sela;
	gg_reg_arr[1].value = sela;
	gg_reg_arr[2].value = selb;
	gg_reg_arr[3].value = selb;
	gg_reg_arr[4].value = 1;
	gg_reg_arr[5].value = 1;
	gg_reg_arr[6].value = 0;
	gg_reg_arr[7].value = 0;

	chg_gg_reg_cur_val(&vendor_host->ggc, data, gg_reg_arr, 8, true);

	return gg_emulator_write(host, data, 512);
}

static void gen_array_data(u32 low32, u32 high32, u32 *ptw)
{
	u8 tu_res_per[6][TUNING_PHASE_SIZE];
	u8 i = 0, j = 0;
	u8 i_mode = 0;
	u32 tw = 0;

	memset(tu_res_per, 1, sizeof(tu_res_per));
	for (i = 0; i < 64; i++) {
		u32 tmp_data = (i < 32) ? low32 : high32;

		tu_res_per[i / TUNING_PHASE_SIZE][i % TUNING_PHASE_SIZE] =
		    (tmp_data & (1 << (i % 32))) >> (i % 32);
	}

	for (i_mode = 0; i_mode < TUNING_PHASE_SIZE; i_mode++) {
		for (j = 0; j < 6; j++) {
			if (tu_res_per[j][i_mode] != 0)
				tw |= (1 << i_mode);
			else {
				tw &= ~(1 << i_mode);
				break;
			}
		}
	}
	if (ptw)
		*ptw = tw;
}

static bool sw_calc_tuning_result(struct sdhci_host *host, u32 *tx_selb,
			   u32 *all_selb, u64 *raw_tx_selb)
{
	bool ret = false;
	u8 data[512] = { 0 };
	u32 selb_status_tx_low32 = 0, selb_status_tx_high32 = 0;
	u32 selb_status_ggc_low32 = 0, selb_status_ggc_high32 = 0;
	struct ggc_reg_op gg_reg_arr[6];
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *vendor_host = sdhci_pltfm_priv(pltfm_host);

	memcpy(&gg_reg_arr[0], &vendor_host->ggc.pha_stas_tx_low32, sizeof(struct ggc_reg_op));
	memcpy(&gg_reg_arr[1], &vendor_host->ggc.pha_stas_tx_high32, sizeof(struct ggc_reg_op));
	memcpy(&gg_reg_arr[2], &vendor_host->ggc.pha_stas_rx_low32, sizeof(struct ggc_reg_op));
	memcpy(&gg_reg_arr[3], &vendor_host->ggc.pha_stas_rx_high32, sizeof(struct ggc_reg_op));
	memcpy(&gg_reg_arr[4], &vendor_host->ggc.dll_sela_after_mask, sizeof(struct ggc_reg_op));
	memcpy(&gg_reg_arr[5], &vendor_host->ggc.dll_selb_after_mask, sizeof(struct ggc_reg_op));

	ret = get_gg_reg_cur(host, data, gg_reg_arr, 6);

	if (ret == true) {
		selb_status_tx_low32 = gg_reg_arr[0].value;
		pr_debug("BHT MSG:[205-236]:\n");
		log_bin(selb_status_tx_low32);
		selb_status_tx_high32 = gg_reg_arr[1].value;
		pr_debug("BHT MSG:[237-268]:\n");
		log_bin(selb_status_tx_high32);
		selb_status_ggc_low32 = gg_reg_arr[2].value;
		pr_debug("BHT MSG:[14-45]:\n");
		log_bin(selb_status_ggc_low32);
		selb_status_ggc_high32 = gg_reg_arr[3].value;
		pr_debug("BHT MSG:[46-77]:\n");
		log_bin(selb_status_ggc_high32);
		pr_debug("BHT MSG:dll sela after mask=%xh\n", gg_reg_arr[4].value);
		pr_debug("BHT MSG:dll selb after mask=%xh\n", gg_reg_arr[5].value);

		if (raw_tx_selb) {
			*raw_tx_selb = gg_reg_arr[1].value;
			(*raw_tx_selb) <<= 32;
			*raw_tx_selb += gg_reg_arr[0].value;
			pr_debug("BHT MSG:raw_tx_selb:%llxh\n", *raw_tx_selb);
		}

		if (tx_selb) {
			gen_array_data(gg_reg_arr[0].value, gg_reg_arr[1].value,
				       tx_selb);
			pr_debug("BHT MSG:tx_selb:%xh\n", *tx_selb);
		}
		if (all_selb) {
			gen_array_data(gg_reg_arr[2].value, gg_reg_arr[3].value,
				       all_selb);
			pr_debug("BHT MSG:all_selb:%xh\n", *all_selb);
		}
	}

	return ret;
}

static bool gg_tuning_result(struct sdhci_host *host, u32 *tx_selb, u32 *all_selb,
			u64 *raw_tx_selb)
{
	host_cmddat_line_reset(host);
	return sw_calc_tuning_result(host, tx_selb, all_selb, raw_tx_selb);
}

static u64 GENERATE_64_IDX_VALUE(int sft)
{
	u64 val = 1;

	return val << sft;
}

static bool is_bus_mode_sdr104(struct sdhci_host *host)
{
	bool ret = false;

	if (host->timing == MMC_TIMING_UHS_SDR104)
		ret = true;

	return ret;
}

static bool _check_bus_mode(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *vendor_host = sdhci_pltfm_priv(pltfm_host);
	struct ggc_adaptor *ggc = &vendor_host->ggc;

	if (is_bus_mode_sdr104(host))
		ggc->cur_bus_mode = &vendor_host->ggc.sdr104;
	else
		ggc->cur_bus_mode = &vendor_host->ggc.sdr50;

	return true;
}

static void tx_selb_failed_history_update(struct sdhci_host *host, u32 val)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *vendor_host = sdhci_pltfm_priv(pltfm_host);

	_check_bus_mode(host);

	vendor_host->ggc.cur_bus_mode->tx_selb_failed_history &= val;
}

static u32 tx_selb_failed_history_get(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *vendor_host = sdhci_pltfm_priv(pltfm_host);

	_check_bus_mode(host);

	return vendor_host->ggc.cur_bus_mode->tx_selb_failed_history;
}

static void tx_selb_failed_tb_update(struct sdhci_host *host, int sela, u32 val)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *vendor_host = sdhci_pltfm_priv(pltfm_host);

	_check_bus_mode(host);
	vendor_host->ggc.cur_bus_mode->tx_selb_tb[sela] &= val;
}

static u32 tx_selb_failed_tb_get(struct sdhci_host *host, int sela)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *vendor_host = sdhci_pltfm_priv(pltfm_host);
	u32 value = 0;

	_check_bus_mode(host);

	if (is_bus_mode_sdr104(host))
		value = vendor_host->ggc.sdr104.tx_selb_tb[sela];
	else
		value = vendor_host->ggc.sdr50.tx_selb_tb[sela];

	return value;
}

static void all_selb_failed_tb_update(struct sdhci_host *host, int sela, u32 val)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *vendor_host = sdhci_pltfm_priv(pltfm_host);

	_check_bus_mode(host);
	vendor_host->ggc.cur_bus_mode->all_selb_tb[sela] &= val;
}

static u32 all_selb_failed_tb_get(struct sdhci_host *host, int sela)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *vendor_host = sdhci_pltfm_priv(pltfm_host);
	u32 val;

	_check_bus_mode(host);

	val = vendor_host->ggc.cur_bus_mode->all_selb_tb[sela];

	return val;
}

static void chk_phase_window(u8 *tuning_win, u8 *mid_val, u8 *max_pass_win)
{
	u8 tuning_pass[TUNING_PHASE_SIZE + 32];
	u8 tuning_pass_start[TUNING_PHASE_SIZE + 32];
	u8 tuning_pass_num_max = 0;
	u8 first_0 = 0;
	u8 i = 0, j = 0;
	u8 i_mode = 0, selb_mode = 0;

	memset(tuning_pass, 1, sizeof(tuning_pass));
	memset(tuning_pass_start, 1, sizeof(tuning_pass_start));

	for (i = 0; i < TUNING_PHASE_SIZE; i++) {
		if (tuning_win[i] == 0) {
			first_0 = i;
			break;
		}
	}

	for (i = 0; i < TUNING_PHASE_SIZE; i++) {
		i_mode = (first_0 + i) % TUNING_PHASE_SIZE;
		if (tuning_win[i_mode] == 1)
			tuning_pass[j]++;
		else if (tuning_pass[j])
			j++;
		if (tuning_pass[j] == 1)
			tuning_pass_start[j] = i_mode;
	}

	for (i = 0; i < TUNING_PHASE_SIZE; i++) {
		if (tuning_pass_num_max < tuning_pass[i]) {
			tuning_pass_num_max = tuning_pass[i];
			i_mode = i;
		}
	}

	if (tuning_pass_num_max == 0)
		pr_err
		    ("Get max pass window fail, there is no any pass phase!!\n");
	else {
		*max_pass_win = tuning_pass_num_max - 1;
		tuning_pass_num_max /= 2;
		selb_mode = tuning_pass_start[i_mode] + tuning_pass_num_max;
		if ((*max_pass_win % 2 == 0))
			selb_mode += 1;
		selb_mode %= TUNING_PHASE_SIZE;
	}

	*mid_val = selb_mode;
}

static void dump_array(u8 *tb)
{
	int i = 0;
	u8 str[12] = { 0 };

	for (i = 0; i < TUNING_PHASE_SIZE; i++)
		str[i] = tb[i] + '0';
}

static void bits_generate_array(u8 *tb, u32 v)
{
	int i = 0;

	for (i = 0; i < TUNING_PHASE_SIZE; i++) {
		if ((v & (1 << i)))
			tb[i] = 1;
		else
			tb[i] = 0;
	}
	dump_array(tb);
}

static void chk_arr_max_win(u8 *tuning_win, u8 first_i, u8 *mid_val,
				u8 *first_val, u8 *max_pass_win, u8 first_valid, u8 right_valid)
{
	u8 tuning_pass[TUNING_PHASE_SIZE];
	u8 tuning_pass_start[TUNING_PHASE_SIZE];
	u8 tuning_pass_num_max = 0;
	u8 first_0 = 0;
	u8 i = 0, j = 0;
	u8 i_mode = 0, selb_mode = 0;

	memset(tuning_pass, 1, sizeof(tuning_pass));
	memset(tuning_pass_start, 1, sizeof(tuning_pass_start));

	if (first_valid)
		first_0 = first_i;
	else {
		for (i = 0; i < TUNING_PHASE_SIZE; i++) {
			if (tuning_win[i] == 0) {
				first_0 = i;
				break;
			}
		}
	}

	for (i = 0; i < TUNING_PHASE_SIZE; i++) {
		i_mode = (first_0 + i) % TUNING_PHASE_SIZE;
		if (tuning_win[i_mode] == 1)
			tuning_pass[j]++;
		else if (tuning_pass[j])
			j++;
		if (tuning_pass[j] == 1)
			tuning_pass_start[j] = i_mode;
	}

	for (i = 0; i < TUNING_PHASE_SIZE; i++) {
		if (tuning_pass_num_max < tuning_pass[i]) {
			tuning_pass_num_max = tuning_pass[i];
			i_mode = i;
		}
	}

	if (tuning_pass_num_max == 0)
		pr_err
		    ("Get max pass window fail, there is no any pass phase!!\n");
	else {
		*max_pass_win = tuning_pass_num_max - 1;
		tuning_pass_num_max /= 2;
		if (first_val)
			*first_val = tuning_pass_start[i_mode];
		selb_mode = tuning_pass_start[i_mode] + tuning_pass_num_max;
		if ((*max_pass_win % 2 == 0) && (right_valid)
		    )
			selb_mode += 1;
		selb_mode %= TUNING_PHASE_SIZE;
	}

	*mid_val = selb_mode;
}

void no_fail_p(u8 *tuning_win, u8 *mid_val, u8 *max_pass_win, u8 *first_val)
{
	u8 first_0 = 0;
	u8 first_valid = 0;
	u8 right_valid = 1;

	chk_arr_max_win(tuning_win, first_0, mid_val, first_val,
	max_pass_win, first_valid, right_valid);
}

static int ggc_get_selx_weight(u32 val)
{
	int i = 0;
	int cnt = 0;

	for (i = 0; i < TUNING_PHASE_SIZE; i++) {
		if (GET_IDX_VALUE(val, i))
			cnt++;
	}
	return cnt;
}

void tx_selb_calculate_valid_phase_range(u32 val, int *start,
						int *pass_cnt)
{
	int i = 0, flg = 0;

	*pass_cnt = ggc_get_selx_weight(val);
	for (i = 0; i < (TUNING_PHASE_SIZE * 2); i++) {
		if ((GET_TUNING_RING_IDX_VALUE(val, i)) == 0 && (flg == 0))
			flg = 1;
		if ((flg == 1) && GET_TUNING_RING_IDX_VALUE(val, i)) {
			*start = TUNING_RING_IDX(i);
			break;
		}
	}
}

bool ggc_update_default_selb_phase_tuning_cnt(struct sdhci_host *host, int selb,
					      int tuning_cnt)
{
	struct ggc_reg_op gg_reg_arr[3];
	u8 data[512];
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *vendor_host = sdhci_pltfm_priv(pltfm_host);

	get_gg_reg_cur_val(&vendor_host->ggc, data, 64);

	pr_debug("BHT MSG: selb:%xh,tuning_cnt:%xh\n", selb,
		 tuning_cnt);
	memcpy(&gg_reg_arr[0], &vendor_host->ggc.dll_selb_100m_cfg,
		sizeof(struct ggc_reg_op));
	memcpy(&gg_reg_arr[1], &vendor_host->ggc.dll_selb_200m_cfg,
		sizeof(struct ggc_reg_op));
	memcpy(&gg_reg_arr[2], &vendor_host->ggc.cmd19_cnt_cfg,
		sizeof(struct ggc_reg_op));

	gg_reg_arr[0].value = selb;
	gg_reg_arr[1].value = selb;
	gg_reg_arr[2].value = tuning_cnt;
	chg_gg_reg_cur_val(&vendor_host->ggc, data, gg_reg_arr, 3, true);

	return true;
}

static void _ggc_update_cur_setting_for_sw_selb_tuning(struct sdhci_host *host,
						       u32 val)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *vendor_host = sdhci_pltfm_priv(pltfm_host);
	int start = 0, pass_cnt = 0;

	tx_selb_calculate_valid_phase_range(val, &start, &pass_cnt);
	pr_debug("BHT MSG:%s %x %x %x\n", __func__, val, start, pass_cnt);
	ggc_update_default_selb_phase_tuning_cnt(host, start, pass_cnt);	//update
	vendor_host->ggc.ggc_sw_selb_tuning_first_selb = start;
}

int sdhci_bht_sdr50_execute_tuning(struct sdhci_host *host, u32 opcode)
{

	u8 phase, *data_buf;
	int size = 64;
	int rc = 0;
	struct mmc_host *mmc = host->mmc;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *vendor_host = sdhci_pltfm_priv(pltfm_host);

	pr_debug("%s: Enter %s\n", mmc_hostname(mmc), __func__);

	data_buf = kmalloc(size, GFP_KERNEL);

	phase = 0;
	do {
		struct mmc_command cmd = { 0 };
		struct mmc_data data = { 0 };
		struct mmc_request mrq = {
			.cmd = &cmd,
			.data = &data
		};
		struct scatterlist sg;

		cmd.opcode = opcode;
		cmd.flags = MMC_RSP_R1 | MMC_CMD_ADTC;

		data.blksz = size;
		data.blocks = 1;
		data.flags = MMC_DATA_READ;
		data.timeout_ns = 30 * 1000 * 1000;	/* 30ms */

		data.sg = &sg;
		data.sg_len = 1;
		sg_init_one(&sg, data_buf, size);
		memset(data_buf, 0, size);
		host_cmddat_line_reset(host);
		mmc_wait_for_req(mmc, &mrq);

		if (cmd.error) {
			if (cmd.error == -EILSEQ)
				vendor_host->ggc.sdr50_notuning_crc_error_flag = 1;
			if (cmd.error == -ETIMEDOUT && phase == 0) {
				pr_err("BHT ERR:cmd19 timeout\n");
				rc = -ETIMEDOUT;
				goto kfree;
			}
		}

		if (data.error) {
			if (data.error == -EILSEQ)
				vendor_host->ggc.sdr50_notuning_crc_error_flag = 1;
		}
	} while (++phase < 16);

kfree:
	kfree(data_buf);

	return rc;
}

int get_config_sela_setting(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *vendor_host = sdhci_pltfm_priv(pltfm_host);

	if (is_bus_mode_sdr104(host))
		return vendor_host->ggc.def_sela_200m;
	else
		return vendor_host->ggc.def_sela_100m;
}

int get_config_selb_setting(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *vendor_host = sdhci_pltfm_priv(pltfm_host);

	if (is_bus_mode_sdr104(host))
		return vendor_host->ggc.def_selb_200m;
	else
		return vendor_host->ggc.def_selb_100m;
}

u32 get_all_sela_status(struct sdhci_host *host, u32 target_selb)
{
	u32 all_sela = 0;
	u32 all_selb = 0;
	int i = 0;

	for (i = 0; i < TUNING_PHASE_SIZE; i++) {
		all_selb = all_selb_failed_tb_get(host, i);
		if (all_selb & (1 << target_selb))
			all_sela |= 1 << i;
	}
	return all_sela;
}

int get_pass_window_weight(u32 val)
{
	int i = 0;
	int cnt = 0;

	for (i = 0; i < TUNING_PHASE_SIZE; i++) {
		if (GET_IDX_VALUE(val, i))
			cnt++;
	}
	return cnt;
}

int get_sela_nearby_pass_window(u32 sela, u32 base)
{

	int i = 0;
	int idx = base;
	int cnt = 0;

	if (GET_IDX_VALUE(sela, idx) == 0)
		return 0;

	for (i = 0; i < TUNING_PHASE_SIZE; i++) {
		if (GET_IDX_VALUE(sela, idx)) {
			idx++;
			idx %= TUNING_PHASE_SIZE;
		} else {
			break;
		}
	}

	if (idx == 0)
		idx = 0xa;
	else
		idx--;

	for (i = 0; i < TUNING_PHASE_SIZE; i++) {
		if (GET_IDX_VALUE(sela, idx)) {
			cnt++;
			if (idx == 0)
				idx = 0xa;
			else
				idx--;
		} else {
			break;
		}
	}
	return cnt;
}

int get_left_one_sel(int base)
{
	if (base == 0)
		return 0xa;
	else
		return base - 1;
}

int get_right_one_sel(int base)
{
	if (base == 0xa)
		return 0x0;
	else
		return base + 1;
}

int get_dif(int x, int y)
{
	int dif = 0;

	if (y > x)
		dif = y - x;
	else
		dif = x - y;

	return dif;
}

static int update_selb(struct sdhci_host *host, int target_selb)
{
	return target_selb;
}

static int ggc_get_10case_0_index(u32 val)
{
	int i = 0;

	for (i = 0; i < TUNING_PHASE_SIZE; i++) {
		if (GET_IDX_VALUE(val, i) == 0
		    && GET_IDX_VALUE(val,
				     TUNING_RING_IDX(i + TUNING_PHASE_SIZE -
						      1))) {
			return i;
		}
	}

	return -1;
}

static u32 ggc_get_01case_0_index(u32 val)
{
	int i = 0;

	for (i = 0; i < TUNING_PHASE_SIZE; i++) {
		if (GET_IDX_VALUE(val, i) == 0
		    && GET_IDX_VALUE(val, TUNING_RING_IDX(i + 1))) {
			return i;
		}
	}

	return -1;
}

static int ggc_get_next_1_index(u32 val, int pos)
{
	int i = 0;

	pos = pos % TUNING_PHASE_SIZE;
	for (i = 0; i < TUNING_PHASE_SIZE; i++)	{
		if (GET_IDX_VALUE(val, (pos+i)%TUNING_PHASE_SIZE))
			break;
	}
	if (GET_IDX_VALUE(val, (pos+i)%TUNING_PHASE_SIZE))
		return (pos+i)%TUNING_PHASE_SIZE;
	else
		return -1;
}

static u32 ggc_get_01case_1_index(u32 val)
{
	int i = 0;

	for (i = 0; i < TUNING_PHASE_SIZE; i++) {
		if (GET_IDX_VALUE(val, i) == 0
			&& GET_IDX_VALUE(val, TUNING_RING_IDX(i + 1))) {
			return TUNING_RING_IDX(i + 1);
		}
	}

	return -1;
}

static int ggc_get_first_0_index(u32 val)
{
	int i = 0;

	for (i = 0; i < TUNING_PHASE_SIZE; i++) {
		if (GET_IDX_VALUE(val, i) == 0)
			return i;
	}
	pr_debug("BHT MSG:oops-not find 0 index\n");
	return 0;
}

static int _tx_selb_inject_policy(int tx_selb, int org_selb)
{
	int group_pos[TUNING_PHASE_SIZE+1][3];
	int group_cnt = 0;
	int max_len_group = 0;
	int max_len = 0;
	int i, j, cnt;
	int zero_start, zero_end, sel;

	if ((org_selb & BIT_PASS_MASK) != BIT_PASS_MASK) {
		sel = tx_selb;
		zero_start = ggc_get_10case_0_index(sel);
		sel &=
		    ~GENERATE_TUNING_RING_IDX_VALUE(get_left_one_sel
						     (zero_start));
		zero_end = ggc_get_01case_0_index(sel);
		sel &=
		    ~GENERATE_TUNING_RING_IDX_VALUE(get_right_one_sel
						     (zero_end));
		if (sel != (sel & tx_selb)) {
			pr_err
			    ("tx selb reinject exception case :not adjacent phase\n");
			pr_err("BHT ERR:selb_failed range:%xh  ,new tx_selb:%x\n",
			       org_selb, tx_selb);
		}
		org_selb &= tx_selb;
	} else {
		cnt = ggc_get_selx_weight(~tx_selb);
		pr_debug("BHT MSG:%d\n", cnt);
		switch (cnt) {
		case 1:
			i = ggc_get_first_0_index(tx_selb);
			tx_selb &=
			    ~GENERATE_TUNING_RING_IDX_VALUE(get_right_one_sel
							     (i));
			tx_selb &=
			    ~GENERATE_TUNING_RING_IDX_VALUE(get_left_one_sel
							     (i));

			break;
		case 2:
			i = ggc_get_10case_0_index(tx_selb);
			tx_selb &=
			    ~GENERATE_TUNING_RING_IDX_VALUE(get_left_one_sel
							     (i));
			i = ggc_get_01case_0_index(tx_selb);
			tx_selb &=
			    ~GENERATE_TUNING_RING_IDX_VALUE(get_right_one_sel
							     (i));
			break;
		default:
			pr_debug("BHT MSG:>= 3 point case\n");
		}
		org_selb &= tx_selb;
	}

	pr_debug("BHT MSG:will check continuous 0bits: 0x%x\n", org_selb);

	memset(group_pos, 0, sizeof(group_pos));
	for (i = ggc_get_01case_1_index(org_selb);
		i < TUNING_PHASE_SIZE && i >= 0 && group_cnt < TUNING_PHASE_SIZE;) {
		for (j = 1; j < TUNING_PHASE_SIZE; j++)	{
			if (GET_TUNING_RING_IDX_VALUE(org_selb, i+j) != 0)
				continue;
			else
				break;
		}
		group_pos[group_cnt][0] = i;
		group_pos[group_cnt][1] = (i + j - 1) % TUNING_PHASE_SIZE;
		group_pos[group_cnt][2] = j;
		group_cnt++;
		if (group_pos[group_cnt-1][0] > group_pos[group_cnt-1][1])
			break;
		i = ggc_get_next_1_index(org_selb, (i+j)%TUNING_PHASE_SIZE);
		for (j = 0; j < group_cnt; j++)	{
			if (i == group_pos[j][0])
				break;
		}
		if (j < group_cnt)
			break;
	}

	if (group_cnt > 1) {
		pr_err("BHT ERR:After inject, selb 0x%x has %d continuous 0 bits\n",
				org_selb, group_cnt);

		for (i = 0; i < group_cnt; i++) {
			if (max_len < group_pos[i][2]) {
				max_len = group_pos[i][2];
				max_len_group = i;
			}
		}
		for (i = (group_pos[max_len_group][1] + 1) % TUNING_PHASE_SIZE;
			i != group_pos[max_len_group][0]; i = (i+1)%TUNING_PHASE_SIZE) {
			org_selb &= ~(1 << i);
		}
		pr_err("BHT ERR:After merge incontious 0 group, selb changed to 0x%x\n", org_selb);
	} else if (group_cnt > 0) {
		pr_err("BHT ERR:After merge incontious 0 group, selb = 0x%x\n", org_selb);
	} else {
		pr_err("BHT ERR:selb 0x%x has no bit is 0\n", org_selb);
	}

	return org_selb;
}

void tx_selb_inject_policy(struct sdhci_host *host, int tx_selb)
{

	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *vendor_host = sdhci_pltfm_priv(pltfm_host);

	pr_debug("BHT MSG:before inject, failed ragen 0x%x, tx_selb 0x%x\n",
			vendor_host->ggc.ggc_cmd_tx_selb_failed_range, tx_selb);
	vendor_host->ggc.ggc_cmd_tx_selb_failed_range =
		_tx_selb_inject_policy(tx_selb, vendor_host->ggc.ggc_cmd_tx_selb_failed_range);
	tx_selb_failed_history_update(host, vendor_host->ggc.ggc_cmd_tx_selb_failed_range);
	pr_debug("BHT MSG:after inject %xh range:%xh\n", tx_selb,
		 vendor_host->ggc.ggc_cmd_tx_selb_failed_range);
	if (is_bus_mode_sdr104(host))
		vendor_host->ggc.sdr104.fail_phase = vendor_host->ggc.ggc_cmd_tx_selb_failed_range;
	else
		vendor_host->ggc.sdr50.fail_phase = vendor_host->ggc.ggc_cmd_tx_selb_failed_range;
}

int get_selb_failure_point(int start, u64 raw_tx_selb, int tuning_cnt)
{
	int last = start + (tuning_cnt - 1);
	int i = 0;
	int j = 0;
	int phase = start;
	int vct = BIT_PASS_MASK;

	pr_debug("BHT MSG:%s start:%d tuning_cnt:%d\n", __func__, start,
			 tuning_cnt);

	for (i = 0; i < tuning_cnt; i++) {
		if ((raw_tx_selb & GENERATE_64_IDX_VALUE(last - i)) == 0)
			break;
	}
	if (i == tuning_cnt) {
		phase = last % TUNING_PHASE_SIZE;
		vct &= (~(1 << phase));
		goto exit;
	}

	for (i = 0; i < tuning_cnt; i++) {
		if ((raw_tx_selb & GENERATE_64_IDX_VALUE(last - i)) != 0)
			break;
	}
	for (j = i - 2; j >= 0; j--)
		raw_tx_selb |= (1 << (last - j));

	for (j = 0; j < tuning_cnt; j++) {
		if (0 == (raw_tx_selb & GENERATE_64_IDX_VALUE(last - j)))
			vct &= (~(1 << (last-j)));
	}

exit:
	pr_debug("BHT MSG:%s: after adjust raw_tx_selb: 0x%llx, vct 0x%x\n",
			 __func__, raw_tx_selb, vct);

	return vct;
}

bool selx_failure_point_exist(u32 val)
{
	return (val & BIT_PASS_MASK) != BIT_PASS_MASK;
}

static int _bits_vct_get_left_index(int base)
{
	return TUNING_RING_IDX(base + TUNING_PHASE_SIZE - 1);
}

int _get_best_window_phase(u32 vct, int *pmax_pass_win, int shif_left_flg)
{
	u8 tuning_win[TUNING_PHASE_SIZE] = { 0 };
	u8 tuning_pass[TUNING_PHASE_SIZE];
	int tuning_pass_start[TUNING_PHASE_SIZE];
	int tuning_pass_num_max = 0;
	int first_0 = 0;
	int i = 0, j = 0;
	int i_mode = 0, selb_mode = 0;

	memset(tuning_pass, 0, sizeof(tuning_pass));
	memset(tuning_pass_start, 0, sizeof(tuning_pass_start));

	for (i = 0; i < TUNING_PHASE_SIZE; i++) {
		if (GET_IDX_VALUE(vct, i))
			tuning_win[i] = 1;
		else
			tuning_win[i] = 0;
	}

	for (i = 0; i < TUNING_PHASE_SIZE; i++) {
		if (tuning_win[i] == 0) {
			first_0 = i;
			break;
		}
	}

	for (i = 0; i < TUNING_PHASE_SIZE; i++) {
		i_mode = TUNING_RING_IDX(first_0 + i);
		if (tuning_win[i_mode] == 1)
			tuning_pass[j]++;
		else if (tuning_pass[j])
			j++;
		if (tuning_pass[j] == 1)
			tuning_pass_start[j] = i_mode;
	}

	for (i = 0; i < TUNING_PHASE_SIZE; i++) {
		if (tuning_pass_num_max < tuning_pass[i]) {
			tuning_pass_num_max = tuning_pass[i];
			i_mode = i;
		}
	}

	if (tuning_pass_num_max == 0) {
		pr_err("BHT ERR:Get max pass window fail, there is no any pass phase!!\n");
		selb_mode = 0;
	} else {
		if (tuning_pass_num_max % 2)	{
			selb_mode = tuning_pass_start[i_mode] + (tuning_pass_num_max - 1) / 2;
		} else {
			selb_mode = tuning_pass_start[i_mode] + (tuning_pass_num_max) / 2;
			if (shif_left_flg) {
				selb_mode = _bits_vct_get_left_index(selb_mode);
				pr_debug("BHT MSG:shift left index\n");
			}
		}
		selb_mode = TUNING_RING_IDX(selb_mode);
	}
	if (pmax_pass_win)
		*pmax_pass_win = tuning_pass_num_max;

	return selb_mode;
}


int get_best_window_phase(u32 vct, int *pmax_pass_win)
{
	return _get_best_window_phase(vct, pmax_pass_win, 0);
}

static int _ggc_get_suitable_selb_for_next_tuning(struct sdhci_host *host)
{
	int selb = 0;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *vendor_host = sdhci_pltfm_priv(pltfm_host);
	u32 inj_tx_selb = BIT_PASS_MASK;

	if (selx_failure_point_exist(vendor_host->ggc.ggc_cmd_tx_selb_failed_range)) {
		selb = vendor_host->ggc.ggc_sw_selb_tuning_first_selb;
	} else {
		pr_debug("BHT MSG:manual inject for all pass case\n");
		if (is_bus_mode_sdr104(host))
			inj_tx_selb &= SDR104_MANUAL_INJECT;
		else
			inj_tx_selb &= SDR50_MANUAL_INJECT;

		pr_debug("BHT MSG:manual inject for all pass case, inj_tx_selb=0x%x\n",
				inj_tx_selb);
		selb = get_best_window_phase(inj_tx_selb, NULL);
		pr_debug("BHT MSG:select selb %d for all pass case\n", selb);
	}
	return selb;
}

static int ggc_get_tuning_cnt_from_buffer(struct sdhci_host *host)
{
	int cnt = 0;
	u8 data[512];
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *vendor_host = sdhci_pltfm_priv(pltfm_host);

	get_gg_reg_cur_val(&vendor_host->ggc, data, 64);
	cnt = (int)cfg_read_bits_ofs_mask(data, &vendor_host->ggc.cmd19_cnt_cfg);

	pr_debug("BHT MSG:tuning cnt=%d\n", cnt);
	return cnt;
}

bool ggc_hw_inject_ext(struct sdhci_host *host, bool *card_status,
			u32 sel200, u32 sel100, bool writetobh201)
{
	bool ret = true;
	u8 data[512];
	struct ggc_reg_op gg_reg_arr[10];
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *vendor_host = sdhci_pltfm_priv(pltfm_host);
	u32 i = 0;
	u32 reg;

	pr_debug("BHT MSG:%s sel200:%xh,sel100:%xh\n", __func__, sel200, sel100);
	get_gg_reg_cur_val(&vendor_host->ggc, data, 64);
	memcpy(&gg_reg_arr[0], &vendor_host->ggc.inject_failure_for_tuning_enable_cfg,
				sizeof(struct ggc_reg_op));
	memcpy(&gg_reg_arr[1], &vendor_host->ggc.inject_failure_for_200m_tuning_cfg,
				sizeof(struct ggc_reg_op));
	memcpy(&gg_reg_arr[2], &vendor_host->ggc.inject_failure_for_100m_tuning_cfg,
				sizeof(struct ggc_reg_op));
	gg_reg_arr[0].value = 1;
	gg_reg_arr[1].value = sel200;
	gg_reg_arr[2].value = sel100;

	chg_gg_reg_cur_val(&vendor_host->ggc, data, gg_reg_arr, 3, true);
	if (writetobh201)
		ret = gg_emulator_write_ext(host, card_status, data, 512);
	else {
		pr_debug("BHT MSG:%s: dump config data instead write to bh201\n", __func__);
		for (i = 0; i < 128; i++) {
			memcpy(&reg, data+i*sizeof(u32), sizeof(u32));
			pr_debug("BHT MSG:    ggc_reg32[%03d]=0x%08x\n", i, reg);
		}
	}
	return ret;
}

bool _ggc_hw_inject_may_recursive(struct sdhci_host *host, u32 sel200,
			u32 sel100, int max_recur, bool writetobh201)
{
	bool ret = true, card_status = true;
	int selb = BIT_PASS_MASK;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *vendor_host = sdhci_pltfm_priv(pltfm_host);

	ret = ggc_hw_inject_ext(host, &card_status, vendor_host->ggc.ggc_cmd_tx_selb_failed_range,
			vendor_host->ggc.ggc_cmd_tx_selb_failed_range, writetobh201);
	pr_debug("BHT MSG:ret:%x\n", ret);
	if ((ret == false) && (card_status == false)) {
		pr_debug("BHT MSG:inject again when hw inject\n");
		selb &= ~GENERATE_IDX_VALUE(vendor_host->ggc.ggc_sw_selb_tuning_first_selb);
		tx_selb_inject_policy(host, selb);
		_ggc_update_cur_setting_for_sw_selb_tuning(host,
			vendor_host->ggc.ggc_cmd_tx_selb_failed_range);

		if (((11 - get_bit_number(vendor_host->ggc.ggc_cmd_tx_selb_failed_range)) >= 5)) {
			pr_err("BHT ERR:pass windows too small,reinit recursive\n");
			return false;
		}

		if (max_recur--)
			return _ggc_hw_inject_may_recursive(host,
			vendor_host->ggc.ggc_cmd_tx_selb_failed_range,
			vendor_host->ggc.ggc_cmd_tx_selb_failed_range, max_recur, writetobh201);
		else
			return false;
	} else
		return true;
}

bool ggc_hw_inject_may_recursive(struct sdhci_host *host, u32 sel200,
			u32 sel100, bool writetobh201)
{
	return _ggc_hw_inject_may_recursive(host, sel200, sel100, 4, writetobh201);
}

bool get_next_dll_voltage(int cur, int *next, u32 *dll_voltage_unlock_cnt,
			int *dll_voltage_scan_map)
{
	int min_idx = 0, cur_cnt = 0, next_cnt = 0;
	int cur_flg = 0;
	int i = 0;
	u8 ret = 0;

	pr_warn("BHT WARN:dll_voltage_unlock_cnt:%x %x %x %x\n",
			dll_voltage_unlock_cnt[0], dll_voltage_unlock_cnt[1],
			dll_voltage_unlock_cnt[2], dll_voltage_unlock_cnt[3]);
	pr_warn("BHT WARN:dll_voltage_scan_map:%x %x %x %x\n",
			dll_voltage_scan_map[0], dll_voltage_scan_map[1],
			dll_voltage_scan_map[2], dll_voltage_scan_map[3]);
	for (i = 1; i < 4; i++) {
		if (cur_flg == 0) {
			if (dll_voltage_scan_map[(cur + i) % 4] != 0)
				continue;
			cur_cnt = dll_voltage_unlock_cnt[(cur + i) % 4];
			cur_flg = 1;
			min_idx = (cur + i) % 4;
			continue;
		} else {
			if (dll_voltage_scan_map[(cur + i) % 4] != 0)
				continue;
			next_cnt = dll_voltage_unlock_cnt[(cur + i) % 4];
			if (cur_cnt > next_cnt) {
				cur_cnt = next_cnt;
				min_idx = (cur + i) % 4;
			}
		}
	}
	if (cur_flg == 0) {
		pr_err("BHT ERR:no find available voltage\n");
		ret = false;
	} else {
		*next = min_idx;
		pr_err("BHT ERR:next available voltage %d\n", min_idx);
		ret = true;
	}
	return ret;
}

bool ggc_sw_calc_tuning_result(struct sdhci_host *host, bool *card_status,
			       bool *read_status, u32 *tx_selb, u32 *all_selb, u64 *raw_tx_selb)
{
	bool ret = false;
	bool card_ret = false;
	bool read_ret = false;
	u32 selb_status_tx_low32 = 0, selb_status_tx_high32 = 0;
	u32 selb_status_ggc_low32 = 0, selb_status_ggc_high32 = 0;
	struct ggc_reg_op gg_reg_arr[8] = {{0}};
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *vendor_host = sdhci_pltfm_priv(pltfm_host);

	memcpy(&gg_reg_arr[0], &vendor_host->ggc.pha_stas_tx_low32,
		sizeof(struct ggc_reg_op));
	memcpy(&gg_reg_arr[1], &vendor_host->ggc.pha_stas_tx_high32,
		sizeof(struct ggc_reg_op));
	memcpy(&gg_reg_arr[2], &vendor_host->ggc.pha_stas_rx_low32,
		sizeof(struct ggc_reg_op));
	memcpy(&gg_reg_arr[3], &vendor_host->ggc.pha_stas_rx_high32,
		sizeof(struct ggc_reg_op));
	memcpy(&gg_reg_arr[4], &vendor_host->ggc.dll_sela_after_mask,
		sizeof(struct ggc_reg_op));
	memcpy(&gg_reg_arr[5], &vendor_host->ggc.dll_selb_after_mask,
		sizeof(struct ggc_reg_op));
	memcpy(&gg_reg_arr[6], &vendor_host->ggc.dll_delay_100m_backup,
		sizeof(struct ggc_reg_op));
	memcpy(&gg_reg_arr[7], &vendor_host->ggc.dll_delay_200m_backup,
		sizeof(struct ggc_reg_op));

	ret = ggc_read_registers_ext(host, &card_ret, &read_ret, gg_reg_arr, 8);
	if (read_ret == true) {
		selb_status_tx_low32 = gg_reg_arr[0].value;
		pr_debug("BHT MSG:[205-236]:\n");
		log_bin(selb_status_tx_low32);
		selb_status_tx_high32 = gg_reg_arr[1].value;
		pr_debug("BHT MSG:[237-268]:\n");
		log_bin(selb_status_tx_high32);
		selb_status_ggc_low32 = gg_reg_arr[2].value;
		pr_debug("BHT MSG:[14-45]:\n");
		log_bin(selb_status_ggc_low32);
		selb_status_ggc_high32 = gg_reg_arr[3].value;
		pr_debug("BHT MSG:[46-77]:\n");
		log_bin(selb_status_ggc_high32);
		pr_debug("BHT MSG:dll  sela after mask=%xh", gg_reg_arr[4].value);
		pr_debug("BHT MSG:dll  selb after mask=%xh", gg_reg_arr[5].value);

		if (raw_tx_selb) {
			*raw_tx_selb = gg_reg_arr[1].value;
			(*raw_tx_selb) <<= 32;
			*raw_tx_selb += gg_reg_arr[0].value;
			pr_debug("BHT MSG:raw_tx_selb:%llxh\n", *raw_tx_selb);
		}

		if (tx_selb) {
			gen_array_data(gg_reg_arr[0].value, gg_reg_arr[1].value,
				       tx_selb);
			pr_debug("BHT MSG:tx_selb:%xh\n", *tx_selb);
		}
		if (all_selb) {
			gen_array_data(gg_reg_arr[2].value, gg_reg_arr[3].value,
				       all_selb);
			pr_debug("BHT MSG:all_selb:%xh\n", *all_selb);
		}
	}

	if (read_status)
		(*read_status) = read_ret;
	if (card_status)
		(*card_status) = card_ret;

	if (card_status && read_status)
		pr_debug("BHT MSG:card_status,read_status:%x %x\n", *card_status, *read_status);
	return ret;
}

bool _ggc_calc_cur_sela_tuning_result(struct sdhci_host *host, int cur_sela, int start_selb)
{
	bool read_status = false;
	bool card_status = false;
	bool ret = true;
	u32 tx_selb, all_selb;
	u64 raw_tx_selb = 0;
	bool retuning_flg = false;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *vendor_host = sdhci_pltfm_priv(pltfm_host);
	int selb;
	struct ggc_adaptor *ggc = &vendor_host->ggc;
	enum tuning_state *psela_tuning_result = ggc->ggc_sela_tuning_result;

	ret = ggc_sw_calc_tuning_result(host, &card_status,
			&read_status, &tx_selb, &all_selb, &raw_tx_selb);

	if (card_status == false) {
		if (read_status == true) {
			selb = get_selb_failure_point(start_selb, raw_tx_selb,
					ggc_get_tuning_cnt_from_buffer(host));
			pr_debug("BHT MSG:inject selb %03x for CMD7 read timeout\n", selb);
			tx_selb_inject_policy(host, selb);
		} else {
			pr_debug("BHT MSG:%s dll:%xh read status failed\n",
					__func__, cur_sela);
		}
		ret = false;
		goto exit;
	} else {
		if (read_status == true) {
			if (selx_failure_point_exist(tx_selb)) {
				if ((11-get_bit_number(tx_selb)) <= 3) {
					tx_selb_inject_policy(host, tx_selb);
					all_selb_failed_tb_update(host, cur_sela, all_selb);
					tx_selb_failed_tb_update(host, cur_sela, tx_selb);
					tx_selb_failed_history_update(host, tx_selb);
				} else if (get_bit_number(tx_selb) == 0) {
					selb = get_selb_failure_point(start_selb, raw_tx_selb,
							ggc_get_tuning_cnt_from_buffer(host));
					tx_selb_inject_policy(host, selb);
					all_selb_failed_tb_update(host, cur_sela, all_selb);
					tx_selb_failed_tb_update(host, cur_sela, selb);
					tx_selb_failed_history_update(host, selb);
					retuning_flg = true;
				} else {
					tx_selb_inject_policy(host, tx_selb);
					all_selb_failed_tb_update(host, cur_sela, all_selb);
					tx_selb_failed_tb_update(host, cur_sela, tx_selb);
					tx_selb_failed_history_update(host, tx_selb);
					retuning_flg = true;
				}

				_ggc_update_cur_setting_for_sw_selb_tuning(host,
						ggc->ggc_cmd_tx_selb_failed_range);
				ggc_hw_inject_may_recursive(host, ggc->ggc_cmd_tx_selb_failed_range,
						ggc->ggc_cmd_tx_selb_failed_range, true);
			} else {
				all_selb_failed_tb_update(host, cur_sela, all_selb);
				tx_selb_failed_tb_update(host, cur_sela, tx_selb);
				tx_selb_failed_history_update(host, tx_selb);
			}

			if (retuning_flg == true) {
				pr_debug("BHT MSG: %s dll:%xh need retuning\n",
						__func__, cur_sela);
				psela_tuning_result[cur_sela] = RETUNING_CASE;
			} else {
				pr_debug("BHT MSG: %s dll:%xh pass\n",
						__func__, cur_sela);
				psela_tuning_result[cur_sela] = OUTPUT_PASS_TYPE;
			}
		} else {
			psela_tuning_result[cur_sela] = READ_STATUS_FAIL_TYPE;
			all_selb_failed_tb_update(host, cur_sela, 0);
			pr_debug("BHT MSG:== %s dll:%xh read status failed ==\n",
					__func__, cur_sela);
		}
	}
exit:
	return ret;
}
static int sdhci_bht_sdr104_execute_tuning(struct sdhci_host *host, u32 opcode)
{
	struct mmc_host *mmc = host->mmc;

	return sdhci_msm_bayhub_execute_tuning(mmc, opcode);
}

static int sd_tuning_sw(struct sdhci_host *host)
{
	int ret = 0;

	if (is_bus_mode_sdr104(host))
		ret = sdhci_bht_sdr104_execute_tuning(host, 0x13);
	else
		ret = sdhci_bht_sdr50_execute_tuning(host, 0x13);

	return ret;
}

static bool sd_gg_tuning_status(struct sdhci_host *host,
			 u32 *tx_selb, u32 *all_selb, u64 *raw_tx_selb,
			 bool *status_ret, bool *first_cmd19_status)
{
	bool ret = true;
	int err = sd_tuning_sw(host);

	ret = err == 0 ? true : false;
	if (err == -ETIMEDOUT) {
		ret = false;
		if (first_cmd19_status)
			*first_cmd19_status = false;
		goto exit;
	}

	if (status_ret) {
		*status_ret =
			gg_tuning_result(host, tx_selb, all_selb,
						raw_tx_selb);
	} else {
		gg_tuning_result(host, 0, 0, 0);
	}

exit:
	return ret;
}

static bool ggc_sd_tuning(struct sdhci_host *host,
			 bool *first_cmd19_status)
{
	bool ret = true;
	int err = sd_tuning_sw(host);

	ret = err == 0 ? true : false;
	if (err == -ETIMEDOUT) {
		ret = false;
		if (first_cmd19_status)
			*first_cmd19_status = false;
		goto exit;
	}

exit:
	return ret;
}

static bool _ggc_output_tuning(struct sdhci_host *host, u8 *selb_pass_win)
{
	int cur_sela = 0, dll_sela_cnt = 0;
	int dll_sela_basis = 0;
	bool ret = false;
	u8 win_tb[12] = { 0 };
	u8 win_mid = 0;
	u8 win_max = 0;
	u32 tx_tmp = 0;
	int target_sela = 0;
	int target_selb = 0;
	u32 all_sela, tx_selb, all_selb;
	u64 raw_tx_selb;
	bool status_ret = false;
	int cur_selb = 0;
	int tuning_error_type[16] = { 0 };
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *vendor_host = sdhci_pltfm_priv(pltfm_host);
	struct ggc_adaptor *ggc = &vendor_host->ggc;
	enum tuning_state *psela_tuning_result = ggc->ggc_sela_tuning_result;
	int i = 0;
	u32 idx_r, idx_c;
	u32 min_pos = 0;
	u32 all_selb_ar[TUNING_PHASE_SIZE] = { 0 };
	u32 pass_cnt[TUNING_PHASE_SIZE] = { 0 };
	u32 cfg = 0;
	int rescan_selb_cnt = 0;
	int returning_selb_cnt = 0;
	bool first_cmd19_sta = true;
	int next = 0;
	bool card_status = true;
	int selb =	BIT_PASS_MASK;
	u8 all_str[TUNING_PHASE_SIZE + 1], tx_str[TUNING_PHASE_SIZE + 1];

	ggc->driver_strength_reinit_flg = 0;
	vendor_host->ggc.dll_unlock_reinit_flg = 0;
	dll_sela_basis = get_config_sela_setting(host);
	cur_selb = get_config_selb_setting(host);

	if (ggc->tuning_cmd7_timeout_reinit_flg) {
		ggc->tuning_cmd7_timeout_reinit_flg = 0;
		dll_sela_basis = vendor_host->ggc.ggc_cur_sela;
		cur_selb = vendor_host->ggc.ggc_sw_selb_tuning_first_selb;
		pr_debug
		("BHT MSG:Tuning start sela: 0x%x, selb: 0x%x where CMD7 timeout\n",
				dll_sela_basis, cur_selb);
	}

	for (dll_sela_cnt = 0; dll_sela_cnt < TUNING_PHASE_SIZE; dll_sela_cnt++) {
		cur_sela =
				(dll_sela_cnt + dll_sela_basis) % TUNING_PHASE_SIZE;
		ggc->ggc_cur_sela = cur_sela;
		pr_debug("BHT MSG: %s select sela dll: %x, selb dll: %x\n",
				__func__, cur_sela, cur_selb);
		if (psela_tuning_result[cur_sela] != NO_TUNING) {
			pr_debug("BHT MSG:sela tuning=%d already tuning,so skip it\n", cur_sela);
			continue;
		}
rescan_selb:
		host_cmddat_line_reset(host);

		if (dll_sela_cnt == 0) {
			if (!selx_failure_point_exist
					(vendor_host->ggc.ggc_cmd_tx_selb_failed_range)) {
				rescan_selb_cnt = 3;
				pr_debug("BHT MSG:no need rescan case\n");
			}
			status_ret = false;
			ret = ggc_sd_tuning(host, &first_cmd19_sta);

			if (first_cmd19_sta == false) {
				_check_bus_mode(host);
				ggc->cur_bus_mode->dll_voltage_unlock_cnt
							[ggc->cur_dll_voltage_idx]++;
				ggc->dll_voltage_scan_map[ggc->cur_dll_voltage_idx] = 1;
				if (get_next_dll_voltage(ggc->cur_dll_voltage_idx, &next,
					ggc->cur_bus_mode->dll_voltage_unlock_cnt,
					ggc->dll_voltage_scan_map) == true)
					ggc->cur_dll_voltage_idx = next;
				else
					ggc->cur_dll_voltage_idx =
							(ggc->cur_dll_voltage_idx + 1) % 4;

				pr_err("BHT ERR:first cmd19 timeout\n");
				vendor_host->ggc.dll_unlock_reinit_flg = 1;
				_ggc_reset_tuning_result_for_dll(host);
				ret = false;
				goto exit;
			}
		} else if ((is_bus_mode_sdr104(host) == false)
				&& vendor_host->ggc.sdr50_notuning_sela_inject_flag == 1
				&& !GET_IDX_VALUE(vendor_host->ggc.sdr50_notuning_sela_rx_inject,
				cur_sela)) {
			pr_debug("BHT MSG:skip %d\n", cur_sela);
			tuning_error_type[cur_sela] = READ_STATUS_FAIL_TYPE;
			goto cur_sela_failed;
		} else {
			ret = ggc_set_output_tuning_phase_ext(host, &card_status,
				cur_sela, update_selb(host, cur_selb));
			if (ret == false || card_status == false) {
				pr_err
				("BHT ERR: output_tuning fail at phase %d,ret=%d,card_status=%d\n",
						cur_sela, ret, card_status);
				if (card_status == false) {
					selb &= ~GENERATE_IDX_VALUE(cur_selb);
					pr_err("BHT ERR:inject selb %d for update sela/selb fail\n",
							selb);
					tx_selb_inject_policy(host, selb);
					_ggc_update_cur_setting_for_sw_selb_tuning(host,
						ggc->ggc_cmd_tx_selb_failed_range);
					ggc_hw_inject_may_recursive(host,
							ggc->ggc_cmd_tx_selb_failed_range,
							ggc->ggc_cmd_tx_selb_failed_range, true);

					if (((11 - get_bit_number(
						ggc->ggc_cmd_tx_selb_failed_range))	>= 5)) {
						u8 current_ds = (u8)(ggc->gg_reg_cur[15] >> 28);

						pr_err("BHT ERR:pass windows too small\n");

						ggc->driver_strength_reinit_flg  =
						current_ds < 7 ? current_ds + 1 : 7;

						ggc->gg_reg_cur[15] &= 0x0F0FFFFF;
						ggc->gg_reg_cur[15] |=
						(ggc->driver_strength_reinit_flg << 28)
						| (ggc->driver_strength_reinit_flg << 20);
						ret = false;

						goto exit;
					}
					cur_selb = _ggc_get_suitable_selb_for_next_tuning(host);
				}
				psela_tuning_result[cur_sela] = RETUNING_CASE;
				goto retuning_case;
			}
			ret = ggc_sd_tuning(host, NULL);
		}

		if (ret == false) {
			pr_err("BHT ERR:Error when output_tuning, fail at phase %d\n",
					cur_sela);
			psela_tuning_result[cur_sela] = TUNING_FAIL_TYPE;
			all_selb_failed_tb_update(host, cur_sela, 0);
			continue;
		}

		ret = _ggc_calc_cur_sela_tuning_result(host, cur_sela, cur_selb);

		if ((11 - get_bit_number(vendor_host->ggc.ggc_cmd_tx_selb_failed_range)) >= 5) {
			u8 current_ds = (u8)(ggc->gg_reg_cur[15] >> 28);

			pr_err("BHT ERR:pass windows too small after result calculate, reinit\n");
			if (current_ds < 7)
				ggc->driver_strength_reinit_flg = current_ds + 1;
			else
				ggc->driver_strength_reinit_flg = 7;

			ggc->gg_reg_cur[15] &= 0x0F0FFFFF;
			ggc->gg_reg_cur[15] |=
				(ggc->driver_strength_reinit_flg << 28)
				| (ggc->driver_strength_reinit_flg << 20);
			ret = false;
			pr_err("BHT ERR:will change driver strength from %d to %d\n",
						 current_ds,
						 ggc->driver_strength_reinit_flg);
			goto exit;
		}

		if (ret == false) {
			pr_err("BHT ERR:cmd7 timeout fail,reinit\n");
			vendor_host->ggc.tuning_cmd7_timeout_reinit_flg = 1;

			_ggc_update_cur_setting_for_sw_selb_tuning(host,
							ggc->ggc_cmd_tx_selb_failed_range);
			ggc_hw_inject_may_recursive(host, ggc->ggc_cmd_tx_selb_failed_range,
							ggc->ggc_cmd_tx_selb_failed_range, false);
			if ((11 - get_bit_number(vendor_host->ggc.ggc_cmd_tx_selb_failed_range))
										>= 5) {
				u8 current_ds = (u8)(ggc->gg_reg_cur[15] >> 28);

				pr_err("BHT ERR:pass windows too small, driver strength reinit\n");

				vendor_host->ggc.tuning_cmd7_timeout_reinit_flg = 0;

				ggc->driver_strength_reinit_flg  =
						current_ds < 7 ? current_ds + 1 : 7;

				ggc->gg_reg_cur[15] &= 0x0F0FFFFF;
				ggc->gg_reg_cur[15] |=
					(ggc->driver_strength_reinit_flg << 28)
					| (ggc->driver_strength_reinit_flg << 20);
				ret = false;
				pr_err("BHT ERR:will change driver strength from %d to %d\n",
						current_ds,
						ggc->driver_strength_reinit_flg);
				goto exit;
			}
			goto exit;
		}

		cur_selb = _ggc_get_suitable_selb_for_next_tuning(host);

		pr_debug("BHT MSG: output sela:%xh pass\n", cur_sela);
		rescan_selb_cnt++;
		if ((rescan_selb_cnt < 3) &&
			(selx_failure_point_exist(vendor_host->ggc.ggc_cmd_tx_selb_failed_range))) {
			pr_debug("BHT MSG:rescan cnt %d, ggc_cmd_tx_selb_failed_range=0x%x\n",
						rescan_selb_cnt,
						vendor_host->ggc.ggc_cmd_tx_selb_failed_range);
			goto rescan_selb;
		}

retuning_case:
		if (psela_tuning_result[cur_sela] == RETUNING_CASE) {
			returning_selb_cnt++;
			if (returning_selb_cnt < 3) {
				rescan_selb_cnt = 0;
				pr_debug("BHT MSG:retuning %d\n", rescan_selb_cnt);
				goto rescan_selb;
			} else {
				psela_tuning_result[cur_sela] = SET_PHASE_FAIL_TYPE;
				all_selb_failed_tb_update(host, cur_sela, 0);
				continue;
			}
		}

		goto next_dll_sela;

cur_sela_failed:
		pr_debug("BHT MSG:read status failedB\n");
		all_selb = 0;
		all_selb_failed_tb_update(host, cur_sela, all_selb);
		pr_debug("BHT MSG: output sela:%xh failed ===\n", cur_sela);
next_dll_sela:
		if ((is_bus_mode_sdr104(host) == false)
				&& vendor_host->ggc.sdr50_notuning_crc_error_flag) {
			u32 fp = 0;

			fp = GENERATE_IDX_VALUE(cur_sela);
			fp |= GENERATE_IDX_VALUE((cur_sela + 1) % TUNING_PHASE_SIZE);
			fp |= GENERATE_IDX_VALUE((cur_sela + 10) % TUNING_PHASE_SIZE);
			vendor_host->ggc.sdr50_notuning_sela_rx_inject &= ~fp;
			vendor_host->ggc.sdr50_notuning_sela_inject_flag = 1;
			pr_debug("BHT MSG:sdr50_notuning_sela_rx_inject:%x\n",
						vendor_host->ggc.sdr50_notuning_sela_rx_inject);
			ret = false;
			goto exit;
		};
	}

	for (i = 0; i < TUNING_PHASE_SIZE; i++) {
		phase_str(all_str, all_selb_failed_tb_get(host, i));
		phase_str(tx_str, tx_selb_failed_tb_get(host, i));
		pr_debug
				("BHT MSG:DLL sela[%x]  all selb: %s   tx selb: %s [%xh,%xh] %s\n",
					i, all_str, tx_str,
					all_selb_failed_tb_get(host, i),
					tx_selb_failed_tb_get(host, i),
					op_dbg_str[tuning_error_type[i]]);

	}

	/* remove margin passed all selb phase */
	for (i = 0; i < TUNING_PHASE_SIZE; i++)
		all_selb_ar[i] = all_selb_failed_tb_get(host, i);

	/* calculate cumulation of diagonal bits */
	for (idx_c = 0; idx_c < TUNING_PHASE_SIZE; idx_c++) {
		for (idx_r = 0; idx_r < TUNING_PHASE_SIZE;
					idx_r++) {
			pass_cnt[idx_c] +=	((all_selb_ar[idx_r] >>
					((idx_r + idx_c) % TUNING_PHASE_SIZE)) & 0x01);
		}
		if (idx_c == 0)
			min_pos = 0;
		else if (pass_cnt[idx_c] < pass_cnt[min_pos])
			min_pos = idx_c;
	}
	for (i = 0; i < TUNING_PHASE_SIZE; i++) {
		all_selb_ar[i] &= ~(1 << (min_pos + i) % TUNING_PHASE_SIZE);
		all_selb_failed_tb_update(host, i, all_selb_ar[i]);
	}

	tx_selb = tx_selb_failed_history_get(host);

	pr_debug("inject sw selb & merge tx_selb failed point to all_selb\n");
	for (i = 0; i < TUNING_PHASE_SIZE; i++)
		all_selb_failed_tb_update(host, i, tx_selb);

	pr_debug("BHT MSG:inject sw sela failed point to all_selb\n");
	if (is_bus_mode_sdr104(host))
		cfg = 0x7ff;
	else
		cfg = 0x7ff;

	for (i = 0; i < TUNING_PHASE_SIZE; i++) {
		if (GET_IDX_VALUE(cfg, i) == 0)
			all_selb_failed_tb_update(host, i, 0);
	}

	for (i = 0; i < TUNING_PHASE_SIZE; i++) {
		phase_str(all_str, all_selb_failed_tb_get(host, i));
		phase_str(tx_str, tx_selb_failed_tb_get(host, i));
		pr_debug("BHT MSG:DLL sela[%x]  all selb: %s tx selb: %s [%xh,%xh] %s\n",
				i, all_str, tx_str,
				all_selb_failed_tb_get(host, i),
				tx_selb_failed_tb_get(host, i),
				op_dbg_str[tuning_error_type[i]]);
	}

	tx_selb = tx_selb_failed_history_get(host);
	tx_selb &= 0x7ff;
	tx_tmp = tx_selb;

	pr_debug("BHT MSG:---selb merge---\n");
	if ((tx_selb&0x7ff) == 0x7ff) {
		if (is_bus_mode_sdr104(host)) {
			u32 cfg = SDR104_MANUAL_INJECT;

			tx_selb &= cfg;
			pr_debug("tx selb:%xh SDR104 inject:%xh merge tx_selb:%xh\n",
						tx_tmp, cfg, tx_selb);
		} else {
			u32 cfg = SDR50_MANUAL_INJECT;

			tx_selb &= cfg;
			pr_debug("tx selb:%xh SDR50 inject:%xh merge tx_selb:%xh\n",
						tx_tmp, cfg, tx_selb);
		}
	}

	if (tx_selb == 0) {
		pr_err("all failed, force fixed phase sela selb to default\n");
		target_selb =
				get_config_selb_setting(host);
		target_sela =
				get_config_sela_setting(host);
		goto final;
	}
	phase_str(win_tb, tx_selb);
	pr_debug("BHT MSG: tx selb[%xh] 11 bit: %s\n",
						tx_selb, win_tb);
	bits_generate_array(win_tb, tx_selb);
	chk_phase_window(win_tb, &win_mid, &win_max);
	target_selb = win_mid;

	all_sela = 0;

	for (i = 0; i < TUNING_PHASE_SIZE; i++) {
		u32 all_selb = all_selb_failed_tb_get(host, i);

		phase_str(win_tb, all_selb);
		pr_debug("BHT MSG: all_selb[%xh] 11 bit: %s\n",
				all_selb, win_tb);
		bits_generate_array(win_tb, all_selb);
		chk_phase_window(win_tb, &win_mid,
					&win_max);
		*selb_pass_win = win_max;
		if (all_selb & (1 << target_selb))
			all_sela |= 1 << i;
	}

	phase_str(win_tb, all_sela);
	pr_debug("BHT MSG: all_sela[%xh] 11 bit: %s\n",
			all_sela, win_tb);
	bits_generate_array(win_tb, all_sela);
	chk_phase_window(win_tb, &win_mid, &win_max);
	target_sela = win_mid;

final:

	gg_fix_output_tuning_phase(host, target_sela,
						target_selb);

	ret = sd_gg_tuning_status(host, &tx_selb,
			&all_selb, &raw_tx_selb, &status_ret, NULL);
	if (ret == false) {
		pr_err("Error when output_tuning,  sd_tuning fail\n");
		ret = false;
		goto exit;
	}

	/* use final pass windows */
	phase_str(win_tb, all_selb);
	pr_debug("BHT MSG: all_selb[%xh] 11 bit: %s\n",
			all_selb, win_tb);
	bits_generate_array(win_tb, all_selb);
	chk_phase_window(win_tb, &win_mid, &win_max);
	*selb_pass_win = win_max;

	vendor_host->ggc.selx_tuning_done_flag = 1;

exit:
	pr_debug("BHT MSG:exit:%s  %d\n", __func__, ret);
	return ret;
}

static int sdhci_bht_execute_tuning(struct mmc_host *mmc, u32 opcode)
{
	struct sdhci_host *host = mmc_priv(mmc);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *vendor_host = sdhci_pltfm_priv(pltfm_host);
	u8 tw = 0;
	int ret = 0;

	pr_debug("BHT MSG:enter bht tuning\n");
	if (host->clock < CORE_FREQ_100MHZ) {
		pr_debug("BHT MSG:%d less 100Mhz,no tuning\n", host->clock);
		return 0;
	}

	if (vendor_host->ggc.tuning_in_progress) {
		pr_debug("BHT MSG:tuning_in_progress\n");
		return 0;
	}
	vendor_host->ggc.tuning_in_progress = true;

	if (vendor_host->ggc.selx_tuning_done_flag) {
		pr_debug("BHT MSG:GGC tuning is done, just do vendor host tuning");
		if (is_bus_mode_sdr104(host))
			ret = sdhci_bht_sdr104_execute_tuning(host, 0x13);
		else
			ret = sdhci_bht_sdr50_execute_tuning(host, 0x13);
	} else {
		ret = !_ggc_output_tuning(host, &tw);
	}
	vendor_host->ggc.tuning_in_progress = false;
	return ret;
}
/* Bayhub patch part-3 end */

/* The code porting from sdhci-msm.c part-3 start */
/*
 * sdhci_msm_bayhub_hs400 - Calibrate the DLL for HS400 bus speed mode operation.
 * This needs to be done for both tuning and enhanced_strobe mode.
 * DLL operation is only needed for clock > 100MHz. For clock <= 100MHz
 * fixed feedback clock is used.
 */
static void sdhci_msm_bayhub_hs400(struct sdhci_host *host, struct mmc_ios *ios)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *msm_bayhub_host = sdhci_pltfm_priv(pltfm_host);
	int ret;

	if (host->clock > CORE_FREQ_100MHZ &&
	    (msm_bayhub_host->tuning_done || ios->enhanced_strobe) &&
	    !msm_bayhub_host->calibration_done) {
		ret = sdhci_msm_bayhub_hs400_dll_calibration(host);
		if (!ret)
			msm_bayhub_host->calibration_done = true;
		else
			pr_err("%s: Failed to calibrate DLL for hs400 mode (%d)\n",
			       mmc_hostname(host->mmc), ret);
	}
}

static void sdhci_msm_bayhub_set_uhs_signaling(struct sdhci_host *host,
					unsigned int uhs)
{
	struct mmc_host *mmc = host->mmc;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *msm_bayhub_host = sdhci_pltfm_priv(pltfm_host);
	u16 ctrl_2;
	u32 config;
	const struct sdhci_msm_bayhub_offset *msm_bayhub_offset =
					msm_bayhub_host->offset;

	ctrl_2 = sdhci_readw(host, SDHCI_HOST_CONTROL2);
	/* Select Bus Speed Mode for host */
	ctrl_2 &= ~SDHCI_CTRL_UHS_MASK;
	switch (uhs) {
	case MMC_TIMING_UHS_SDR12:
		ctrl_2 |= SDHCI_CTRL_UHS_SDR12;
		break;
	case MMC_TIMING_UHS_SDR25:
		ctrl_2 |= SDHCI_CTRL_UHS_SDR25;
		break;
	case MMC_TIMING_UHS_SDR50:
		ctrl_2 |= SDHCI_CTRL_UHS_SDR50;
		break;
	case MMC_TIMING_MMC_HS400:
	case MMC_TIMING_MMC_HS200:
	case MMC_TIMING_UHS_SDR104:
		ctrl_2 |= SDHCI_CTRL_UHS_SDR104;
		break;
	case MMC_TIMING_UHS_DDR50:
	case MMC_TIMING_MMC_DDR52:
		ctrl_2 |= SDHCI_CTRL_UHS_DDR50;
		break;
	}

	/*
	 * When clock frequency is less than 100MHz, the feedback clock must be
	 * provided and DLL must not be used so that tuning can be skipped. To
	 * provide feedback clock, the mode selection can be any value less
	 * than 3'b011 in bits [2:0] of HOST CONTROL2 register.
	 */
	if (host->clock <= CORE_FREQ_100MHZ) {
		if (uhs == MMC_TIMING_MMC_HS400 ||
		    uhs == MMC_TIMING_MMC_HS200 ||
		    uhs == MMC_TIMING_UHS_SDR104)
			ctrl_2 &= ~SDHCI_CTRL_UHS_MASK;
		/*
		 * DLL is not required for clock <= 100MHz
		 * Thus, make sure DLL it is disabled when not required
		 */
		config = readl_relaxed(host->ioaddr +
				msm_bayhub_offset->core_dll_config);
		config |= CORE_DLL_RST;
		writel_relaxed(config, host->ioaddr +
				msm_bayhub_offset->core_dll_config);

		config = readl_relaxed(host->ioaddr +
				msm_bayhub_offset->core_dll_config);
		config |= CORE_DLL_PDN;
		writel_relaxed(config, host->ioaddr +
				msm_bayhub_offset->core_dll_config);

		/*
		 * The DLL needs to be restored and CDCLP533 recalibrated
		 * when the clock frequency is set back to 400MHz.
		 */
		msm_bayhub_host->calibration_done = false;
	}

	dev_dbg(mmc_dev(mmc), "%s: clock=%u uhs=%u ctrl_2=0x%x\n",
		mmc_hostname(host->mmc), host->clock, uhs, ctrl_2);
	sdhci_writew(host, ctrl_2, SDHCI_HOST_CONTROL2);

	if (mmc->ios.timing == MMC_TIMING_MMC_HS400)
		sdhci_msm_bayhub_hs400(host, &mmc->ios);
}

static int sdhci_msm_bayhub_set_pincfg(struct sdhci_msm_bayhub_host *msm_bayhub_host, bool level)
{
	struct platform_device *pdev = msm_bayhub_host->pdev;
	int ret;

	if (level)
		ret = pinctrl_pm_select_default_state(&pdev->dev);
	else
		ret = pinctrl_pm_select_sleep_state(&pdev->dev);

	return ret;
}

static int sdhci_msm_bayhub_set_vmmc(struct mmc_host *mmc)
{
	if (IS_ERR(mmc->supply.vmmc))
		return 0;

	return mmc_regulator_set_ocr(mmc, mmc->supply.vmmc, mmc->ios.vdd);
}

static int msm_bayhub_toggle_vqmmc(struct sdhci_msm_bayhub_host *msm_bayhub_host,
			      struct mmc_host *mmc, bool level)
{
	int ret;
	struct mmc_ios ios;

	if (msm_bayhub_host->vqmmc_enabled == level)
		return 0;

	if (level) {
		/* Set the IO voltage regulator to default voltage level */
		if (msm_bayhub_host->caps_0 & CORE_3_0V_SUPPORT)
			ios.signal_voltage = MMC_SIGNAL_VOLTAGE_330;
		else if (msm_bayhub_host->caps_0 & CORE_1_8V_SUPPORT)
			ios.signal_voltage = MMC_SIGNAL_VOLTAGE_180;

		if (msm_bayhub_host->caps_0 & CORE_VOLT_SUPPORT) {
			ret = mmc_regulator_set_vqmmc(mmc, &ios);
			if (ret < 0) {
				dev_err(mmc_dev(mmc), "%s: vqmmc set volgate failed: %d\n",
					mmc_hostname(mmc), ret);
				goto out;
			}
		}
		ret = regulator_enable(mmc->supply.vqmmc);
	} else {
		ret = regulator_disable(mmc->supply.vqmmc);
	}

	if (ret)
		dev_err(mmc_dev(mmc), "%s: vqmm %sable failed: %d\n",
			mmc_hostname(mmc), level ? "en":"dis", ret);
	else
		msm_bayhub_host->vqmmc_enabled = level;
out:
	return ret;
}

static int msm_bayhub_config_vqmmc_mode(struct sdhci_msm_bayhub_host *msm_bayhub_host,
			      struct mmc_host *mmc, bool hpm)
{
	int load, ret;

	load = hpm ? MMC_VQMMC_MAX_LOAD_UA : 0;
	ret = regulator_set_load(mmc->supply.vqmmc, load);
	if (ret)
		dev_err(mmc_dev(mmc), "%s: vqmmc set load failed: %d\n",
			mmc_hostname(mmc), ret);
	return ret;
}

static int sdhci_msm_bayhub_set_vqmmc(struct sdhci_msm_bayhub_host *msm_bayhub_host,
			      struct mmc_host *mmc, bool level)
{
	int ret;
	bool always_on;

	if (IS_ERR(mmc->supply.vqmmc) ||
			(mmc->ios.power_mode == MMC_POWER_UNDEFINED))
		return 0;
	/*
	 * For eMMC don't turn off Vqmmc, Instead just configure it in LPM
	 * and HPM modes by setting the corresponding load.
	 *
	 * Till eMMC is initialized (i.e. always_on == 0), just turn on/off
	 * Vqmmc. Vqmmc gets turned off only if init fails and mmc_power_off
	 * gets invoked. Once eMMC is initialized (i.e. always_on == 1),
	 * Vqmmc should remain ON, So just set the load instead of turning it
	 * off/on.
	 */
	always_on = !mmc_card_is_removable(mmc) &&
			mmc->card && mmc_card_mmc(mmc->card);

	if (always_on)
		ret = msm_bayhub_config_vqmmc_mode(msm_bayhub_host, mmc, level);
	else
		ret = msm_bayhub_toggle_vqmmc(msm_bayhub_host, mmc, level);

	return ret;
}

static inline void sdhci_msm_bayhub_init_pwr_irq_wait(struct sdhci_msm_bayhub_host *msm_bayhub_host)
{
	init_waitqueue_head(&msm_bayhub_host->pwr_irq_wait);
}

static inline void sdhci_msm_bayhub_complete_pwr_irq_wait(
		struct sdhci_msm_bayhub_host *msm_bayhub_host)
{
	wake_up(&msm_bayhub_host->pwr_irq_wait);
}

/*
 * sdhci_msm_bayhub_check_power_status API should be called when registers writes
 * which can toggle sdhci IO bus ON/OFF or change IO lines HIGH/LOW happens.
 * To what state the register writes will change the IO lines should be passed
 * as the argument req_type. This API will check whether the IO line's state
 * is already the expected state and will wait for power irq only if
 * power irq is expected to be triggered based on the current IO line state
 * and expected IO line state.
 */
static void sdhci_msm_bayhub_check_power_status(struct sdhci_host *host, u32 req_type)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *msm_bayhub_host = sdhci_pltfm_priv(pltfm_host);
	bool done = false;
	u32 val = SWITCHABLE_SIGNALING_VOLTAGE;
	const struct sdhci_msm_bayhub_offset *msm_bayhub_offset =
					msm_bayhub_host->offset;

	pr_debug("%s: %s: request %d curr_pwr_state %x curr_io_level %x\n",
			mmc_hostname(host->mmc), __func__, req_type,
			msm_bayhub_host->curr_pwr_state, msm_bayhub_host->curr_io_level);

	/*
	 * The power interrupt will not be generated for signal voltage
	 * switches if SWITCHABLE_SIGNALING_VOLTAGE in MCI_GENERICS is not set.
	 * Since sdhci-msm_bayhub-v5, this bit has been removed and SW must consider
	 * it as always set.
	 */
	if (!msm_bayhub_host->mci_removed)
		val = msm_bayhub_host_readl(msm_bayhub_host, host,
				msm_bayhub_offset->core_generics);
	if ((req_type & REQ_IO_HIGH || req_type & REQ_IO_LOW) &&
	    !(val & SWITCHABLE_SIGNALING_VOLTAGE)) {
		return;
	}

	/*
	 * The IRQ for request type IO High/LOW will be generated when -
	 * there is a state change in 1.8V enable bit (bit 3) of
	 * SDHCI_HOST_CONTROL2 register. The reset state of that bit is 0
	 * which indicates 3.3V IO voltage. So, when MMC core layer tries
	 * to set it to 3.3V before card detection happens, the
	 * IRQ doesn't get triggered as there is no state change in this bit.
	 * The driver already handles this case by changing the IO voltage
	 * level to high as part of controller power up sequence. Hence, check
	 * for host->pwr to handle a case where IO voltage high request is
	 * issued even before controller power up.
	 */
	if ((req_type & REQ_IO_HIGH) && !host->pwr) {
		pr_debug("%s: do not wait for power IRQ that never comes, req_type: %d\n",
				mmc_hostname(host->mmc), req_type);
		return;
	}
	if ((req_type & msm_bayhub_host->curr_pwr_state) ||
			(req_type & msm_bayhub_host->curr_io_level))
		done = true;
	/*
	 * This is needed here to handle cases where register writes will
	 * not change the current bus state or io level of the controller.
	 * In this case, no power irq will be triggerred and we should
	 * not wait.
	 */
	if (!done) {
		if (!wait_event_timeout(msm_bayhub_host->pwr_irq_wait,
				msm_bayhub_host->pwr_irq_flag,
				msecs_to_jiffies(MSM_PWR_IRQ_TIMEOUT_MS)))
			dev_warn(&msm_bayhub_host->pdev->dev,
				 "%s: pwr_irq for req: (%d) timed out\n",
				 mmc_hostname(host->mmc), req_type);
	}
	pr_debug("%s: %s: request %d done\n", mmc_hostname(host->mmc),
			__func__, req_type);
}

static void sdhci_msm_bayhub_dump_pwr_ctrl_regs(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *msm_bayhub_host = sdhci_pltfm_priv(pltfm_host);
	const struct sdhci_msm_bayhub_offset *msm_bayhub_offset =
					msm_bayhub_host->offset;

	pr_err("%s: PWRCTL_STATUS: 0x%08x | PWRCTL_MASK: 0x%08x | PWRCTL_CTL: 0x%08x\n",
		mmc_hostname(host->mmc),
		msm_bayhub_host_readl(msm_bayhub_host, host, msm_bayhub_offset->core_pwrctl_status),
		msm_bayhub_host_readl(msm_bayhub_host, host, msm_bayhub_offset->core_pwrctl_mask),
		msm_bayhub_host_readl(msm_bayhub_host, host, msm_bayhub_offset->core_pwrctl_ctl));
}

static void sdhci_msm_bayhub_handle_pwr_irq(struct sdhci_host *host, int irq)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *msm_bayhub_host = sdhci_pltfm_priv(pltfm_host);
	struct mmc_host *mmc = host->mmc;
	u32 irq_status, irq_ack = 0;
	int retry = 10, ret;
	u32 pwr_state = 0, io_level = 0;
	u32 config;
	const struct sdhci_msm_bayhub_offset *msm_bayhub_offset = msm_bayhub_host->offset;

	irq_status = msm_bayhub_host_readl(msm_bayhub_host, host,
			msm_bayhub_offset->core_pwrctl_status);
	irq_status &= INT_MASK;

	msm_bayhub_host_writel(msm_bayhub_host, irq_status, host,
			msm_bayhub_offset->core_pwrctl_clear);

	/*
	 * There is a rare HW scenario where the first clear pulse could be
	 * lost when actual reset and clear/read of status register is
	 * happening at a time. Hence, retry for at least 10 times to make
	 * sure status register is cleared. Otherwise, this will result in
	 * a spurious power IRQ resulting in system instability.
	 */
	while (irq_status & msm_bayhub_host_readl(msm_bayhub_host, host,
				msm_bayhub_offset->core_pwrctl_status)) {
		if (retry == 0) {
			pr_err("%s: Timedout clearing (0x%x) pwrctl status register\n",
					mmc_hostname(host->mmc), irq_status);
			sdhci_msm_bayhub_dump_pwr_ctrl_regs(host);
			WARN_ON(1);
			break;
		}
		msm_bayhub_host_writel(msm_bayhub_host, irq_status, host,
			msm_bayhub_offset->core_pwrctl_clear);
		retry--;
		udelay(10);
	}

	/* Handle BUS ON/OFF*/
	if (irq_status & CORE_PWRCTL_BUS_ON) {
		/* GGC chip power on */
		bh201_signal_voltage_on_off(host, 1);
		pwr_state = REQ_BUS_ON;
		io_level = REQ_IO_HIGH;
	}
	if (irq_status & CORE_PWRCTL_BUS_OFF) {
		/* GGC chip power off */
		bh201_signal_voltage_on_off(host, 0);
		pwr_state = REQ_BUS_OFF;
		io_level = REQ_IO_LOW;
	}

	if (pwr_state) {
		ret = sdhci_msm_bayhub_set_vmmc(mmc);
		if (!ret)
			ret = sdhci_msm_bayhub_set_vqmmc(msm_bayhub_host, mmc,
					pwr_state & REQ_BUS_ON);
		if (!ret)
			ret = sdhci_msm_bayhub_set_pincfg(msm_bayhub_host,
					pwr_state & REQ_BUS_ON);
		if (!ret)
			irq_ack |= CORE_PWRCTL_BUS_SUCCESS;
		else
			irq_ack |= CORE_PWRCTL_BUS_FAIL;
	}

	/* Handle IO LOW/HIGH */
	if (irq_status & CORE_PWRCTL_IO_LOW)
		io_level = REQ_IO_LOW;

	if (irq_status & CORE_PWRCTL_IO_HIGH)
		io_level = REQ_IO_HIGH;

	if (io_level)
		irq_ack |= CORE_PWRCTL_IO_SUCCESS;

	if (io_level && !IS_ERR(mmc->supply.vqmmc) && !pwr_state) {
		ret = mmc_regulator_set_vqmmc(mmc, &mmc->ios);
		if (ret < 0) {
			dev_err(mmc_dev(mmc), "%s: IO_level setting failed(%d). signal_voltage: %d, vdd: %d irq_status: 0x%08x\n",
					mmc_hostname(mmc), ret,
					mmc->ios.signal_voltage, mmc->ios.vdd,
					irq_status);
			irq_ack |= CORE_PWRCTL_IO_FAIL;
		}
	}

	/*
	 * The driver has to acknowledge the interrupt, switch voltages and
	 * report back if it succeeded or not to this register. The voltage
	 * switches are handled by the sdhci core, so just report success.
	 */
	msm_bayhub_host_writel(msm_bayhub_host, irq_ack, host,
			msm_bayhub_offset->core_pwrctl_ctl);

	/*
	 * If we don't have info regarding the voltage levels supported by
	 * regulators, don't change the IO PAD PWR SWITCH.
	 */
	if (msm_bayhub_host->caps_0 & CORE_VOLT_SUPPORT) {
		u32 new_config;
		/*
		 * We should unset IO PAD PWR switch only if the register write
		 * can set IO lines high and the regulator also switches to 3 V.
		 * Else, we should keep the IO PAD PWR switch set.
		 * This is applicable to certain targets where eMMC vccq supply
		 * is only 1.8V. In such targets, even during REQ_IO_HIGH, the
		 * IO PAD PWR switch must be kept set to reflect actual
		 * regulator voltage. This way, during initialization of
		 * controllers with only 1.8V, we will set the IO PAD bit
		 * without waiting for a REQ_IO_LOW.
		 */
		config = readl_relaxed(host->ioaddr +
				msm_bayhub_offset->core_vendor_spec);
		new_config = config;

		if ((io_level & REQ_IO_HIGH) &&
				(msm_bayhub_host->caps_0 & CORE_3_0V_SUPPORT))
			new_config &= ~CORE_IO_PAD_PWR_SWITCH;
		else if ((io_level & REQ_IO_LOW) ||
				(msm_bayhub_host->caps_0 & CORE_1_8V_SUPPORT))
			new_config |= CORE_IO_PAD_PWR_SWITCH;

		if (config ^ new_config)
			writel_relaxed(new_config, host->ioaddr +
					msm_bayhub_offset->core_vendor_spec);
	}

	if (pwr_state)
		msm_bayhub_host->curr_pwr_state = pwr_state;
	if (io_level)
		msm_bayhub_host->curr_io_level = io_level;

	dev_dbg(mmc_dev(mmc), "%s: %s: Handled IRQ(%d), irq_status=0x%x, ack=0x%x\n",
		mmc_hostname(msm_bayhub_host->mmc), __func__, irq, irq_status,
		irq_ack);
}

static irqreturn_t sdhci_msm_bayhub_pwr_irq(int irq, void *data)
{
	struct sdhci_host *host = (struct sdhci_host *)data;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *msm_bayhub_host = sdhci_pltfm_priv(pltfm_host);

	sdhci_msm_bayhub_handle_pwr_irq(host, irq);
	msm_bayhub_host->pwr_irq_flag = 1;
	sdhci_msm_bayhub_complete_pwr_irq_wait(msm_bayhub_host);


	return IRQ_HANDLED;
}

static unsigned int sdhci_msm_bayhub_get_max_clock(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *msm_bayhub_host = sdhci_pltfm_priv(pltfm_host);
	struct clk *core_clk = msm_bayhub_host->bulk_clks[0].clk;

	return clk_round_rate(core_clk, ULONG_MAX);
}

static unsigned int sdhci_msm_bayhub_get_min_clock(struct sdhci_host *host)
{
	return SDHCI_MSM_MIN_CLOCK;
}

/**
 * __sdhci_msm_bayhub_set_clock - sdhci_msm_bayhub clock control.
 *
 * Description:
 * MSM controller does not use internal divider and
 * instead directly control the GCC clock as per
 * HW recommendation.
 **/
static void __sdhci_msm_bayhub_set_clock(struct sdhci_host *host, unsigned int clock)
{
	u16 clk;

	sdhci_writew(host, 0, SDHCI_CLOCK_CONTROL);

	if (clock == 0)
		return;

	/*
	 * MSM controller do not use clock divider.
	 * Thus read SDHCI_CLOCK_CONTROL and only enable
	 * clock with no divider value programmed.
	 */
	clk = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
	sdhci_enable_clk(host, clk);
}

/* sdhci_msm_bayhub_set_clock - Called with (host->lock) spinlock held. */
static void sdhci_msm_bayhub_set_clock(struct sdhci_host *host, unsigned int clock)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *msm_bayhub_host = sdhci_pltfm_priv(pltfm_host);

	if (!clock) {
		host->mmc->actual_clock = msm_bayhub_host->clk_rate = 0;
		goto out;
	}

	sdhci_msm_bayhub_hc_select_mode(host);

	msm_bayhub_set_clock_rate_for_bus_mode(host, clock);
out:
	__sdhci_msm_bayhub_set_clock(host, clock);
}

/*****************************************************************************\
 *                                                                           *
 * MSM Command Queue Engine (CQE)                                            *
 *                                                                           *
\*****************************************************************************/

static u32 sdhci_msm_bayhub_cqe_irq(struct sdhci_host *host, u32 intmask)
{
	int cmd_error = 0;
	int data_error = 0;

	if (!sdhci_cqe_irq(host, intmask, &cmd_error, &data_error))
		return intmask;

	cqhci_irq(host->mmc, intmask, cmd_error, data_error);
	return 0;
}

static void sdhci_msm_bayhub_cqe_disable(struct mmc_host *mmc, bool recovery)
{
	struct sdhci_host *host = mmc_priv(mmc);
	unsigned long flags;
	u32 ctrl;

	/*
	 * When CQE is halted, the legacy SDHCI path operates only
	 * on 16-byte descriptors in 64bit mode.
	 */
	if (host->flags & SDHCI_USE_64_BIT_DMA)
		host->desc_sz = 16;

	spin_lock_irqsave(&host->lock, flags);

	/*
	 * During CQE command transfers, command complete bit gets latched.
	 * So s/w should clear command complete interrupt status when CQE is
	 * either halted or disabled. Otherwise unexpected SDCHI legacy
	 * interrupt gets triggered when CQE is halted/disabled.
	 */
	ctrl = sdhci_readl(host, SDHCI_INT_ENABLE);
	ctrl |= SDHCI_INT_RESPONSE;
	sdhci_writel(host,  ctrl, SDHCI_INT_ENABLE);
	sdhci_writel(host, SDHCI_INT_RESPONSE, SDHCI_INT_STATUS);

	spin_unlock_irqrestore(&host->lock, flags);

	sdhci_cqe_disable(mmc, recovery);
}

static const struct cqhci_host_ops sdhci_msm_bayhub_cqhci_ops = {
	.enable		= sdhci_cqe_enable,
	.disable	= sdhci_msm_bayhub_cqe_disable,
};

static int sdhci_msm_bayhub_cqe_add_host(struct sdhci_host *host,
				struct platform_device *pdev)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *msm_bayhub_host = sdhci_pltfm_priv(pltfm_host);
	struct cqhci_host *cq_host;
	bool dma64;
	u32 cqcfg;
	int ret;

	/*
	 * When CQE is halted, SDHC operates only on 16byte ADMA descriptors.
	 * So ensure ADMA table is allocated for 16byte descriptors.
	 */
	if (host->caps & SDHCI_CAN_64BIT)
		host->alloc_desc_sz = 16;

	ret = sdhci_setup_host(host);
	if (ret)
		return ret;

	cq_host = cqhci_pltfm_init(pdev);
	if (IS_ERR(cq_host)) {
		ret = PTR_ERR(cq_host);
		dev_err(&pdev->dev, "cqhci-pltfm init: failed: %d\n", ret);
		goto cleanup;
	}

	msm_bayhub_host->mmc->caps2 |= MMC_CAP2_CQE | MMC_CAP2_CQE_DCMD;
	cq_host->ops = &sdhci_msm_bayhub_cqhci_ops;

	dma64 = host->flags & SDHCI_USE_64_BIT_DMA;

	ret = cqhci_init(cq_host, host->mmc, dma64);
	if (ret) {
		dev_err(&pdev->dev, "%s: CQE init: failed (%d)\n",
				mmc_hostname(host->mmc), ret);
		goto cleanup;
	}

	/* Disable cqe reset due to cqe enable signal */
	cqcfg = cqhci_readl(cq_host, CQHCI_VENDOR_CFG1);
	cqcfg |= CQHCI_VENDOR_DIS_RST_ON_CQ_EN;
	cqhci_writel(cq_host, cqcfg, CQHCI_VENDOR_CFG1);

	/*
	 * SDHC expects 12byte ADMA descriptors till CQE is enabled.
	 * So limit desc_sz to 12 so that the data commands that are sent
	 * during card initialization (before CQE gets enabled) would
	 * get executed without any issues.
	 */
	if (host->flags & SDHCI_USE_64_BIT_DMA)
		host->desc_sz = 12;

	ret = __sdhci_add_host(host);
	if (ret)
		goto cleanup;

	dev_info(&pdev->dev, "%s: CQE init: success\n",
			mmc_hostname(host->mmc));
	return ret;

cleanup:
	sdhci_cleanup_host(host);
	return ret;
}

/*
 * Platform specific register write functions. This is so that, if any
 * register write needs to be followed up by platform specific actions,
 * they can be added here. These functions can go to sleep when writes
 * to certain registers are done.
 * These functions are relying on sdhci_set_ios not using spinlock.
 */
static int __sdhci_msm_bayhub_check_write(struct sdhci_host *host, u16 val, int reg)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *msm_bayhub_host = sdhci_pltfm_priv(pltfm_host);
	u32 req_type = 0;

	switch (reg) {
	case SDHCI_HOST_CONTROL2:
		req_type = (val & SDHCI_CTRL_VDD_180) ? REQ_IO_LOW :
			REQ_IO_HIGH;
		break;
	case SDHCI_SOFTWARE_RESET:
		if (host->pwr && (val & SDHCI_RESET_ALL))
			req_type = REQ_BUS_OFF;
		break;
	case SDHCI_POWER_CONTROL:
		req_type = !val ? REQ_BUS_OFF : REQ_BUS_ON;
		break;
	case SDHCI_TRANSFER_MODE:
		msm_bayhub_host->transfer_mode = val;
		break;
	case SDHCI_COMMAND:
		if (!msm_bayhub_host->use_cdr)
			break;
		if ((msm_bayhub_host->transfer_mode & SDHCI_TRNS_READ) &&
		    SDHCI_GET_CMD(val) != MMC_SEND_TUNING_BLOCK_HS200 &&
		    SDHCI_GET_CMD(val) != MMC_SEND_TUNING_BLOCK)
			sdhci_msm_bayhub_set_cdr(host, true);
		else
			sdhci_msm_bayhub_set_cdr(host, false);
		break;
	}

	if (req_type) {
		msm_bayhub_host->pwr_irq_flag = 0;
		/*
		 * Since this register write may trigger a power irq, ensure
		 * all previous register writes are complete by this point.
		 */
		mb();
	}
	return req_type;
}

/* This function may sleep*/
static void sdhci_msm_bayhub_writew(struct sdhci_host *host, u16 val, int reg)
{
	u32 req_type;

	req_type = __sdhci_msm_bayhub_check_write(host, val, reg);
	writew_relaxed(val, host->ioaddr + reg);

	if (req_type)
		sdhci_msm_bayhub_check_power_status(host, req_type);
}

/* This function may sleep*/
static void sdhci_msm_bayhub_writeb(struct sdhci_host *host, u8 val, int reg)
{
	u32 req_type;

	req_type = __sdhci_msm_bayhub_check_write(host, val, reg);

	writeb_relaxed(val, host->ioaddr + reg);

	if (req_type)
		sdhci_msm_bayhub_check_power_status(host, req_type);
}

static void sdhci_msm_bayhub_set_regulator_caps(struct sdhci_msm_bayhub_host *msm_bayhub_host)
{
	struct mmc_host *mmc = msm_bayhub_host->mmc;
	struct regulator *supply = mmc->supply.vqmmc;
	u32 caps = 0, config;
	struct sdhci_host *host = mmc_priv(mmc);
	const struct sdhci_msm_bayhub_offset *msm_bayhub_offset = msm_bayhub_host->offset;

	if (!IS_ERR(mmc->supply.vqmmc)) {
		if (regulator_is_supported_voltage(supply, 1700000, 1950000))
			caps |= CORE_1_8V_SUPPORT;
		if (regulator_is_supported_voltage(supply, 2700000, 3600000))
			caps |= CORE_3_0V_SUPPORT;

		if (!caps)
			pr_warn("%s: 1.8/3V not supported for vqmmc\n",
					mmc_hostname(mmc));
	}

	if (caps) {
		/*
		 * Set the PAD_PWR_SWITCH_EN bit so that the PAD_PWR_SWITCH
		 * bit can be used as required later on.
		 */
		u32 io_level = msm_bayhub_host->curr_io_level;

		config = readl_relaxed(host->ioaddr +
				msm_bayhub_offset->core_vendor_spec);
		config |= CORE_IO_PAD_PWR_SWITCH_EN;

		if ((io_level & REQ_IO_HIGH) && (caps &	CORE_3_0V_SUPPORT))
			config &= ~CORE_IO_PAD_PWR_SWITCH;
		else if ((io_level & REQ_IO_LOW) || (caps & CORE_1_8V_SUPPORT))
			config |= CORE_IO_PAD_PWR_SWITCH;

		writel_relaxed(config,
				host->ioaddr + msm_bayhub_offset->core_vendor_spec);
	}
	msm_bayhub_host->caps_0 |= caps;
	pr_debug("%s: supported caps: 0x%08x\n", mmc_hostname(mmc), caps);
}

static void sdhci_msm_bayhub_reset(struct sdhci_host *host, u8 mask)
{
	if ((host->mmc->caps2 & MMC_CAP2_CQE) && (mask & SDHCI_RESET_ALL))
		cqhci_deactivate(host->mmc);
	sdhci_reset(host, mask);
}

static int sdhci_msm_bayhub_register_vreg(struct sdhci_msm_bayhub_host *msm_bayhub_host)
{
	int ret;

	ret = mmc_regulator_get_supply(msm_bayhub_host->mmc);
	if (ret)
		return ret;

	sdhci_msm_bayhub_set_regulator_caps(msm_bayhub_host);

	return 0;
}

static int sdhci_msm_bayhub_start_signal_voltage_switch(struct mmc_host *mmc,
				      struct mmc_ios *ios)
{
	struct sdhci_host *host = mmc_priv(mmc);
	u16 ctrl, status;

	/*
	 * Signal Voltage Switching is only applicable for Host Controllers
	 * v3.00 and above.
	 */
	if (host->version < SDHCI_SPEC_300)
		return 0;

	ctrl = sdhci_readw(host, SDHCI_HOST_CONTROL2);

	switch (ios->signal_voltage) {
	case MMC_SIGNAL_VOLTAGE_330:
		if (!(host->flags & SDHCI_SIGNALING_330))
			return -EINVAL;

		/* Set 1.8V Signal Enable in the Host Control2 register to 0 */
		ctrl &= ~SDHCI_CTRL_VDD_180;
		break;
	case MMC_SIGNAL_VOLTAGE_180:
		if (!(host->flags & SDHCI_SIGNALING_180))
			return -EINVAL;

		/* Enable 1.8V Signal Enable in the Host Control2 register */
		ctrl |= SDHCI_CTRL_VDD_180;
		break;

	default:
		return -EINVAL;
	}

	sdhci_writew(host, ctrl, SDHCI_HOST_CONTROL2);

	/* Wait for 5ms */
	usleep_range(5000, 5500);

	/* regulator output should be stable within 5 ms */
	status = ctrl & SDHCI_CTRL_VDD_180;
	ctrl = sdhci_readw(host, SDHCI_HOST_CONTROL2);
	if ((ctrl & SDHCI_CTRL_VDD_180) == status)
		return 0;

	dev_warn(mmc_dev(mmc), "%s: Regulator output did not became stable\n",
		mmc_hostname(mmc));

	return -EAGAIN;
}

#define DRIVER_NAME "sdhci_msm_bayhub"
#define SDHCI_MSM_DUMP(f, x...) \
	pr_err("%s: " DRIVER_NAME ": " f, mmc_hostname(host->mmc), ## x)

static void sdhci_msm_bayhub_dump_vendor_regs(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *msm_bayhub_host = sdhci_pltfm_priv(pltfm_host);
	const struct sdhci_msm_bayhub_offset *msm_bayhub_offset = msm_bayhub_host->offset;

	SDHCI_MSM_DUMP("----------- VENDOR REGISTER DUMP -----------\n");

	SDHCI_MSM_DUMP(
			"DLL sts: 0x%08x | DLL cfg:  0x%08x | DLL cfg2: 0x%08x\n",
		readl_relaxed(host->ioaddr + msm_bayhub_offset->core_dll_status),
		readl_relaxed(host->ioaddr + msm_bayhub_offset->core_dll_config),
		readl_relaxed(host->ioaddr + msm_bayhub_offset->core_dll_config_2));
	SDHCI_MSM_DUMP(
			"DLL cfg3: 0x%08x | DLL usr ctl:  0x%08x | DDR cfg: 0x%08x\n",
		readl_relaxed(host->ioaddr + msm_bayhub_offset->core_dll_config_3),
		readl_relaxed(host->ioaddr + msm_bayhub_offset->core_dll_usr_ctl),
		readl_relaxed(host->ioaddr + msm_bayhub_offset->core_ddr_config));
	SDHCI_MSM_DUMP(
			"Vndr func: 0x%08x | Vndr func2 : 0x%08x Vndr func3: 0x%08x\n",
		readl_relaxed(host->ioaddr + msm_bayhub_offset->core_vendor_spec),
		readl_relaxed(host->ioaddr +
			msm_bayhub_offset->core_vendor_spec_func2),
		readl_relaxed(host->ioaddr + msm_bayhub_offset->core_vendor_spec3));
}

static const struct sdhci_msm_bayhub_variant_ops v5_var_ops = {
	.msm_bayhub_readl_relaxed = sdhci_msm_bayhub_v5_variant_readl_relaxed,
	.msm_bayhub_writel_relaxed = sdhci_msm_bayhub_v5_variant_writel_relaxed,
};

static const struct sdhci_msm_bayhub_variant_info sdhci_msm_bayhub_v5_var = {
	.mci_removed = true,
	.var_ops = &v5_var_ops,
	.offset = &sdhci_msm_bayhub_v5_offset,
};

static const struct of_device_id sdhci_msm_bayhub_dt_match[] = {
	{.compatible = "qcom,sdhci-msm-bayhub-v5", .data = &sdhci_msm_bayhub_v5_var},
	{},
};

MODULE_DEVICE_TABLE(of, sdhci_msm_bayhub_dt_match);


static const struct sdhci_ops sdhci_msm_bayhub_ops = {
	.reset = sdhci_msm_bayhub_reset,
	.set_clock = sdhci_msm_bayhub_set_clock,
	.get_min_clock = sdhci_msm_bayhub_get_min_clock,
	.get_max_clock = sdhci_msm_bayhub_get_max_clock,
	.set_bus_width = sdhci_set_bus_width,
	.set_uhs_signaling = sdhci_msm_bayhub_set_uhs_signaling,
	.write_w = sdhci_msm_bayhub_writew,
	.write_b = sdhci_msm_bayhub_writeb,
	.irq	= sdhci_msm_bayhub_cqe_irq,
	.dump_vendor_regs = sdhci_msm_bayhub_dump_vendor_regs,
	.set_power = sdhci_set_power_noreg,
};

static const struct sdhci_pltfm_data sdhci_msm_bayhub_pdata = {
	.quirks = SDHCI_QUIRK_BROKEN_CARD_DETECTION |
		  SDHCI_QUIRK_SINGLE_POWER_WRITE |
		  SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN |
		  SDHCI_QUIRK_MULTIBLOCK_READ_ACMD12,

	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN,
	.ops = &sdhci_msm_bayhub_ops,
};

static inline void sdhci_msm_bayhub_get_of_property(struct platform_device *pdev,
		struct sdhci_host *host)
{
	struct device_node *node = pdev->dev.of_node;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *msm_bayhub_host = sdhci_pltfm_priv(pltfm_host);

	if (of_property_read_u32(node, "qcom,ddr-config",
				&msm_bayhub_host->ddr_config))
		msm_bayhub_host->ddr_config = DDR_CONFIG_POR_VAL;

	of_property_read_u32(node, "qcom,dll-config", &msm_bayhub_host->dll_config);
}

static int sdhci_msm_bayhub_probe(struct platform_device *pdev)
{
	struct sdhci_host *host;
	struct sdhci_pltfm_host *pltfm_host;
	struct sdhci_msm_bayhub_host *msm_bayhub_host;
	struct resource *core_memres;
	struct clk *clk;
	int ret;
	u16 host_version, core_minor;
	u32 core_version, config;
	u8 core_major;
	const struct sdhci_msm_bayhub_offset *msm_bayhub_offset;
	const struct sdhci_msm_bayhub_variant_info *var_info;
	struct device_node *node = pdev->dev.of_node;

	host = sdhci_pltfm_init(pdev, &sdhci_msm_bayhub_pdata, sizeof(*msm_bayhub_host));
	if (IS_ERR(host))
		return PTR_ERR(host);

	host->sdma_boundary = 0;
	pltfm_host = sdhci_priv(host);
	msm_bayhub_host = sdhci_pltfm_priv(pltfm_host);
	msm_bayhub_host->mmc = host->mmc;
	msm_bayhub_host->pdev = pdev;

	ret = mmc_of_parse(host->mmc);
	if (ret)
		goto pltfm_free;

	/* Parse parameters from dtsi and assign resource */
	sdhci_bh201_parse(msm_bayhub_host->mmc);

	/* Overload the mmc->detect routine for GGC chip mode switch */
	INIT_DELAYED_WORK(&host->mmc->detect, mmc_rescan_bayhub);

	/*
	 * Based on the compatible string, load the required msm_bayhub host info from
	 * the data associated with the version info.
	 */
	var_info = of_device_get_match_data(&pdev->dev);

	msm_bayhub_host->mci_removed = var_info->mci_removed;
	msm_bayhub_host->restore_dll_config = var_info->restore_dll_config;
	msm_bayhub_host->var_ops = var_info->var_ops;
	msm_bayhub_host->offset = var_info->offset;
	msm_bayhub_host->uses_tassadar_dll = var_info->uses_tassadar_dll;

	msm_bayhub_offset = msm_bayhub_host->offset;

	sdhci_get_of_property(pdev);
	sdhci_msm_bayhub_get_of_property(pdev, host);

	msm_bayhub_host->saved_tuning_phase = INVALID_TUNING_PHASE;

	/* Setup SDCC bus voter clock. */
	msm_bayhub_host->bus_clk = devm_clk_get(&pdev->dev, "bus");
	if (!IS_ERR(msm_bayhub_host->bus_clk)) {
		/* Vote for max. clk rate for max. performance */
		ret = clk_set_rate(msm_bayhub_host->bus_clk, INT_MAX);
		if (ret)
			goto pltfm_free;
		ret = clk_prepare_enable(msm_bayhub_host->bus_clk);
		if (ret)
			goto pltfm_free;
	}

	/* Setup main peripheral bus clock */
	clk = devm_clk_get(&pdev->dev, "iface");
	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		dev_err(&pdev->dev, "Peripheral clk setup failed (%d)\n", ret);
		goto bus_clk_disable;
	}
	msm_bayhub_host->bulk_clks[1].clk = clk;

	/* Setup SDC MMC clock */
	clk = devm_clk_get(&pdev->dev, "core");
	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		dev_err(&pdev->dev, "SDC MMC clk setup failed (%d)\n", ret);
		goto bus_clk_disable;
	}
	msm_bayhub_host->bulk_clks[0].clk = clk;

	 /* Check for optional interconnect paths */
	ret = dev_pm_opp_of_find_icc_paths(&pdev->dev, NULL);
	if (ret)
		goto bus_clk_disable;

	msm_bayhub_host->opp_table = dev_pm_opp_set_clkname(&pdev->dev, "core");
	if (IS_ERR(msm_bayhub_host->opp_table)) {
		ret = PTR_ERR(msm_bayhub_host->opp_table);
		goto bus_clk_disable;
	}

	/* OPP table is optional */
	ret = dev_pm_opp_of_add_table(&pdev->dev);
	if (!ret) {
		msm_bayhub_host->has_opp_table = true;
	} else if (ret != -ENODEV) {
		dev_err(&pdev->dev, "Invalid OPP table in Device tree\n");
		goto opp_cleanup;
	}

	/* Vote for maximum clock rate for maximum performance */
	ret = dev_pm_opp_set_rate(&pdev->dev, INT_MAX);
	if (ret)
		dev_warn(&pdev->dev, "core clock boost failed\n");

	clk = devm_clk_get(&pdev->dev, "cal");
	if (IS_ERR(clk))
		clk = NULL;
	msm_bayhub_host->bulk_clks[2].clk = clk;

	clk = devm_clk_get(&pdev->dev, "sleep");
	if (IS_ERR(clk))
		clk = NULL;
	msm_bayhub_host->bulk_clks[3].clk = clk;

	ret = clk_bulk_prepare_enable(ARRAY_SIZE(msm_bayhub_host->bulk_clks),
				      msm_bayhub_host->bulk_clks);
	if (ret)
		goto opp_cleanup;

	/*
	 * xo clock is needed for FLL feature of cm_dll.
	 * In case if xo clock is not mentioned in DT, warn and proceed.
	 */
	msm_bayhub_host->xo_clk = devm_clk_get(&pdev->dev, "xo");
	if (IS_ERR(msm_bayhub_host->xo_clk)) {
		ret = PTR_ERR(msm_bayhub_host->xo_clk);
		dev_warn(&pdev->dev, "TCXO clk not present (%d)\n", ret);
	}

	if (!msm_bayhub_host->mci_removed) {
		core_memres = platform_get_resource(pdev, IORESOURCE_MEM, 1);
		msm_bayhub_host->core_mem = devm_ioremap_resource(&pdev->dev,
				core_memres);

		if (IS_ERR(msm_bayhub_host->core_mem)) {
			ret = PTR_ERR(msm_bayhub_host->core_mem);
			goto clk_disable;
		}
	}

	/* Reset the vendor spec register to power on reset state */
	writel_relaxed(CORE_VENDOR_SPEC_POR_VAL,
			host->ioaddr + msm_bayhub_offset->core_vendor_spec);

	if (!msm_bayhub_host->mci_removed) {
		/* Set HC_MODE_EN bit in HC_MODE register */
		msm_bayhub_host_writel(msm_bayhub_host, HC_MODE_EN, host,
				msm_bayhub_offset->core_hc_mode);
		config = msm_bayhub_host_readl(msm_bayhub_host, host,
				msm_bayhub_offset->core_hc_mode);
		config |= FF_CLK_SW_RST_DIS;
		msm_bayhub_host_writel(msm_bayhub_host, config, host,
				msm_bayhub_offset->core_hc_mode);
	}

	host_version = readw_relaxed((host->ioaddr + SDHCI_HOST_VERSION));
	dev_dbg(&pdev->dev, "Host Version: 0x%x Vendor Version 0x%x\n",
		host_version, ((host_version & SDHCI_VENDOR_VER_MASK) >>
			       SDHCI_VENDOR_VER_SHIFT));

	core_version = msm_bayhub_host_readl(msm_bayhub_host, host,
			msm_bayhub_offset->core_mci_version);
	core_major = (core_version & CORE_VERSION_MAJOR_MASK) >>
		      CORE_VERSION_MAJOR_SHIFT;
	core_minor = core_version & CORE_VERSION_MINOR_MASK;
	dev_dbg(&pdev->dev, "MCI Version: 0x%08x, major: 0x%04x, minor: 0x%02x\n",
		core_version, core_major, core_minor);

	if (core_major == 1 && core_minor >= 0x42)
		msm_bayhub_host->use_14lpp_dll_reset = true;

	/*
	 * SDCC 5 controller with major version 1, minor version 0x34 and later
	 * with HS 400 mode support will use CM DLL instead of CDC LP 533 DLL.
	 */
	if (core_major == 1 && core_minor < 0x34)
		msm_bayhub_host->use_cdclp533 = true;

	/*
	 * Support for some capabilities is not advertised by newer
	 * controller versions and must be explicitly enabled.
	 */
	if (core_major >= 1 && core_minor != 0x11 && core_minor != 0x12) {
		config = readl_relaxed(host->ioaddr + SDHCI_CAPABILITIES);
		config |= SDHCI_CAN_VDD_300 | SDHCI_CAN_DO_8BIT;
		writel_relaxed(config, host->ioaddr +
				msm_bayhub_offset->core_vendor_spec_capabilities0);
	}

	if (core_major == 1 && core_minor >= 0x49)
		msm_bayhub_host->updated_ddr_cfg = true;

	ret = sdhci_msm_bayhub_register_vreg(msm_bayhub_host);
	if (ret)
		goto clk_disable;

	/*
	 * Power on reset state may trigger power irq if previous status of
	 * PWRCTL was either BUS_ON or IO_HIGH_V. So before enabling pwr irq
	 * interrupt in GIC, any pending power irq interrupt should be
	 * acknowledged. Otherwise power irq interrupt handler would be
	 * fired prematurely.
	 */
	sdhci_msm_bayhub_handle_pwr_irq(host, 0);

	/*
	 * Ensure that above writes are propogated before interrupt enablement
	 * in GIC.
	 */
	mb();

	/* Setup IRQ for handling power/voltage tasks with PMIC */
	msm_bayhub_host->pwr_irq = platform_get_irq_byname(pdev, "pwr_irq");
	if (msm_bayhub_host->pwr_irq < 0) {
		ret = msm_bayhub_host->pwr_irq;
		goto clk_disable;
	}

	sdhci_msm_bayhub_init_pwr_irq_wait(msm_bayhub_host);
	/* Enable pwr irq interrupts */
	msm_bayhub_host_writel(msm_bayhub_host, INT_MASK, host,
		msm_bayhub_offset->core_pwrctl_mask);

	ret = devm_request_threaded_irq(&pdev->dev, msm_bayhub_host->pwr_irq, NULL,
					sdhci_msm_bayhub_pwr_irq, IRQF_ONESHOT,
					dev_name(&pdev->dev), host);
	if (ret) {
		dev_err(&pdev->dev, "Request IRQ failed (%d)\n", ret);
		goto clk_disable;
	}

	msm_bayhub_host->mmc->caps |= MMC_CAP_WAIT_WHILE_BUSY | MMC_CAP_NEED_RSP_BUSY;

	pm_runtime_get_noresume(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);
	pm_runtime_set_autosuspend_delay(&pdev->dev,
					 MSM_MMC_AUTOSUSPEND_DELAY_MS);
	pm_runtime_use_autosuspend(&pdev->dev);

	host->mmc_host_ops.start_signal_voltage_switch =
		sdhci_msm_bayhub_start_signal_voltage_switch;

	/* Overload the mmc_host_ops.execute_tuning routine with GGC special tuning flow */
	host->mmc_host_ops.execute_tuning = sdhci_bht_execute_tuning;

	if (of_property_read_bool(node, "supports-cqe"))
		ret = sdhci_msm_bayhub_cqe_add_host(host, pdev);
	else
		ret = sdhci_add_host(host);
	if (ret)
		goto pm_runtime_disable;

	pm_runtime_mark_last_busy(&pdev->dev);
	pm_runtime_put_autosuspend(&pdev->dev);

	return 0;

pm_runtime_disable:
	pm_runtime_disable(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);
	pm_runtime_put_noidle(&pdev->dev);
clk_disable:
	clk_bulk_disable_unprepare(ARRAY_SIZE(msm_bayhub_host->bulk_clks),
				   msm_bayhub_host->bulk_clks);
opp_cleanup:
	if (msm_bayhub_host->has_opp_table)
		dev_pm_opp_of_remove_table(&pdev->dev);
	dev_pm_opp_put_clkname(msm_bayhub_host->opp_table);
bus_clk_disable:
	if (!IS_ERR(msm_bayhub_host->bus_clk))
		clk_disable_unprepare(msm_bayhub_host->bus_clk);
pltfm_free:
	/* Release the GGC patch resource */
	if (gpio_is_valid(msm_bayhub_host->ggc.det_gpio))
		devm_gpio_free(&pdev->dev, msm_bayhub_host->ggc.det_gpio);
	if (gpio_is_valid(msm_bayhub_host->ggc.pwr_gpio))
		devm_gpio_free(&pdev->dev, msm_bayhub_host->ggc.pwr_gpio);

	sdhci_pltfm_free(pdev);
	return ret;
}

static int sdhci_msm_bayhub_remove(struct platform_device *pdev)
{
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *msm_bayhub_host = sdhci_pltfm_priv(pltfm_host);
	int dead = (readl_relaxed(host->ioaddr + SDHCI_INT_STATUS) ==
		    0xffffffff);

	sdhci_remove_host(host, dead);
	/* Release the GGC patch resource */
	if (gpio_is_valid(msm_bayhub_host->ggc.det_gpio))
		devm_gpio_free(&pdev->dev, msm_bayhub_host->ggc.det_gpio);
	if (gpio_is_valid(msm_bayhub_host->ggc.pwr_gpio))
		devm_gpio_free(&pdev->dev, msm_bayhub_host->ggc.pwr_gpio);

	if (msm_bayhub_host->has_opp_table)
		dev_pm_opp_of_remove_table(&pdev->dev);
	dev_pm_opp_put_clkname(msm_bayhub_host->opp_table);
	pm_runtime_get_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	pm_runtime_put_noidle(&pdev->dev);

	clk_bulk_disable_unprepare(ARRAY_SIZE(msm_bayhub_host->bulk_clks),
				   msm_bayhub_host->bulk_clks);
	if (!IS_ERR(msm_bayhub_host->bus_clk))
		clk_disable_unprepare(msm_bayhub_host->bus_clk);
	sdhci_pltfm_free(pdev);
	return 0;
}

static __maybe_unused int sdhci_msm_bayhub_runtime_suspend(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *msm_bayhub_host = sdhci_pltfm_priv(pltfm_host);

	/* Drop the performance vote */
	dev_pm_opp_set_rate(dev, 0);
	clk_bulk_disable_unprepare(ARRAY_SIZE(msm_bayhub_host->bulk_clks),
				   msm_bayhub_host->bulk_clks);

	return 0;
}

static __maybe_unused int sdhci_msm_bayhub_runtime_resume(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_msm_bayhub_host *msm_bayhub_host = sdhci_pltfm_priv(pltfm_host);
	int ret;

	ret = clk_bulk_prepare_enable(ARRAY_SIZE(msm_bayhub_host->bulk_clks),
				       msm_bayhub_host->bulk_clks);
	if (ret)
		return ret;
	/*
	 * Whenever core-clock is gated dynamically, it's needed to
	 * restore the SDR DLL settings when the clock is ungated.
	 */
	if (msm_bayhub_host->restore_dll_config && msm_bayhub_host->clk_rate)
		ret = sdhci_msm_bayhub_restore_sdr_dll_config(host);

	dev_pm_opp_set_rate(dev, msm_bayhub_host->clk_rate);

	return ret;
}

static const struct dev_pm_ops sdhci_msm_bayhub_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(sdhci_msm_bayhub_runtime_suspend,
			   sdhci_msm_bayhub_runtime_resume,
			   NULL)
};

static struct platform_driver sdhci_msm_bayhub_driver = {
	.probe = sdhci_msm_bayhub_probe,
	.remove = sdhci_msm_bayhub_remove,
	.driver = {
		   .name = "sdhci_msm_bayhub",
		   .of_match_table = sdhci_msm_bayhub_dt_match,
		   .pm = &sdhci_msm_bayhub_pm_ops,
		   .probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
};

module_platform_driver(sdhci_msm_bayhub_driver);
MODULE_DESCRIPTION("Qualcomm-bayhub Secure Digital Host Controller Interface driver");
/* The code porting from sdhci-msm.c part-3 end */
MODULE_LICENSE("GPL v2");
