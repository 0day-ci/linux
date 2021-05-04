// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Copyright 2017 Microsemi Corporation
 * Copyright 2018-2019 NXP Semiconductors
 */
#include <soc/mscc/ocelot_qsys.h>
#include <soc/mscc/ocelot_vcap.h>
#include <soc/mscc/ocelot_ptp.h>
#include <soc/mscc/ocelot_sys.h>
#include <soc/mscc/ocelot.h>
#include <linux/spi/spi.h>
#include <linux/packing.h>
#include <linux/pcs-lynx.h>
#include <net/pkt_sched.h>
#include <linux/iopoll.h>
#include <linux/kconfig.h>
#include <linux/mdio.h>
#include "felix.h"

#define VSC7512_TAS_GCL_ENTRY_MAX 63

// Note: These addresses and offsets are all shifted down
// by two. This is because the SPI protocol needs them to
// be before they get sent out.
//
// An alternative is to keep them standardized, but then
// a separate spi_bus regmap would need to be defined.
//
// That might be optimal though. The 'Read' protocol of
// the VSC driver might be much quicker if we add padding
// bytes, which I don't think regmap supports.
static const u32 vsc7512_ana_regmap[] = {
	REG(ANA_ADVLEARN,			0x2400),
	REG(ANA_VLANMASK,			0x2401),
	REG_RESERVED(ANA_PORT_B_DOMAIN),
	REG(ANA_ANAGEFIL,			0x2403),
	REG(ANA_ANEVENTS,			0x2404),
	REG(ANA_STORMLIMIT_BURST,		0x2405),
	REG(ANA_STORMLIMIT_CFG,			0x2406),
	REG(ANA_ISOLATED_PORTS,			0x240a),
	REG(ANA_COMMUNITY_PORTS,		0x240b),
	REG(ANA_AUTOAGE,			0x240c),
	REG(ANA_MACTOPTIONS,			0x240d),
	REG(ANA_LEARNDISC,			0x240e),
	REG(ANA_AGENCTRL,			0x240f),
	REG(ANA_MIRRORPORTS,			0x2410),
	REG(ANA_EMIRRORPORTS,			0x2411),
	REG(ANA_FLOODING,			0x2412),
	REG(ANA_FLOODING_IPMC,			0x2413),
	REG(ANA_SFLOW_CFG,			0x2414),
	REG(ANA_PORT_MODE,			0x2420),
	REG(ANA_PGID_PGID,			0x2300),
	REG(ANA_TABLES_ANMOVED,			0x22cc),
	REG(ANA_TABLES_MACHDATA,		0x22cd),
	REG(ANA_TABLES_MACLDATA,		0x22ce),
	REG(ANA_TABLES_MACACCESS,		0x22cf),
	REG(ANA_TABLES_MACTINDX,		0x22d0),
	REG(ANA_TABLES_VLANACCESS,		0x22d1),
	REG(ANA_TABLES_VLANTIDX,		0x22d2),
	REG(ANA_TABLES_ENTRYLIM,		0x22c0),
	REG(ANA_TABLES_PTP_ID_HIGH,		0x22d5),
	REG(ANA_TABLES_PTP_ID_LOW,		0x22d6),
	REG(ANA_PORT_VLAN_CFG,			0x1c00),
	REG(ANA_PORT_DROP_CFG,			0x1c01),
	REG(ANA_PORT_QOS_CFG,			0x1c02),
	REG(ANA_PORT_VCAP_CFG,			0x1c03),
	REG(ANA_PORT_VCAP_S1_KEY_CFG,		0x1c04),
	REG(ANA_PORT_VCAP_S2_CFG,		0x1c07),
	REG(ANA_PORT_PCP_DEI_MAP,		0x1c08),
	REG(ANA_PORT_CPU_FWD_CFG,		0x1c18),
	REG(ANA_PORT_CPU_FWD_BPDU_CFG,		0x1c19),
	REG(ANA_PORT_CPU_FWD_GARP_CFG,		0x1c1a),
	REG(ANA_PORT_CPU_FWD_CCM_CFG,		0x1c1b),
	REG(ANA_PORT_PORT_CFG,			0x1c1c),
	REG(ANA_PORT_POL_CFG,			0x1c1d),
	REG(ANA_PORT_PTP_CFG,			0x1c1e),
	REG(ANA_PORT_PTP_DLY1_CFG,		0x1c1f),
	REG(ANA_PORT_PTP_DLY2_CFG,		0x1c20),
	REG(ANA_PFC_PFC_CFG,			0x2200),
	REG_RESERVED(ANA_PFC_PFC_TIMER),
	REG_RESERVED(ANA_IPT_OAM_MEP_CFG),
	REG_RESERVED(ANA_IPT_IPT),
	REG_RESERVED(ANA_PPT_PPT),
	REG_RESERVED(ANA_FID_MAP_FID_MAP),
	REG(ANA_AGGR_CFG,			0x242d),
	REG(ANA_CPUQ_CFG,			0x242e),
	REG_RESERVED(ANA_CPUQ_CFG2),
	REG(ANA_CPUQ_8021_CFG,			0x242f),
	REG(ANA_DSCP_CFG,			0x2440),
	REG(ANA_DSCP_REWR_CFG,			0x2480),
	REG(ANA_VCAP_RNG_TYPE_CFG,		0x2490),
	REG(ANA_VCAP_RNG_VAL_CFG,		0x2498),
	REG_RESERVED(ANA_VRAP_CFG),
	REG_RESERVED(ANA_VRAP_HDR_DATA),
	REG_RESERVED(ANA_VRAP_HDR_MASK),
	REG(ANA_DISCARD_CFG,			0x24a3),
	REG(ANA_FID_CFG,			0x24a4),
	REG(ANA_POL_PIR_CFG,			0x1000),
	REG(ANA_POL_CIR_CFG,			0x1001),
	REG(ANA_POL_MODE_CFG,			0x1002),
	REG(ANA_POL_PIR_STATE,			0x1003),
	REG(ANA_POL_CIR_STATE,			0x1004),
	REG_RESERVED(ANA_POL_STATE),
	REG(ANA_POL_FLOWC,			0x22e0),
	REG(ANA_POL_HYST,			0x22fb),
	REG_RESERVED(ANA_POL_MISC_CFG),
};

static const u32 vsc7512_qs_regmap[] = {
	REG(QS_XTR_GRP_CFG,			0x0000),
	REG(QS_XTR_RD,				0x0002),
	REG(QS_XTR_FRM_PRUNING,			0x004),
	REG(QS_XTR_FLUSH,			0x0006),
	REG(QS_XTR_DATA_PRESENT,		0x0007),

	REG(QS_INJ_GRP_CFG,			0x0009),
	REG(QS_INJ_WR,				0x000b),
	REG(QS_INJ_CTRL,			0x000d),
	REG(QS_INJ_STATUS,			0x000f),
	REG(QS_INJ_ERR,				0x0010),
	REG_RESERVED(QS_INH_DBG),
};

static const u32 vsc7512_vcap_regmap[] = {
	REG(VCAP_CORE_UPDATE_CTRL,		0x000000),
	REG(VCAP_CORE_MV_CFG,			0x000001),
	REG(VCAP_CACHE_ENTRY_DAT,		0x000002),
	REG(VCAP_CACHE_MASK_DAT,		0x000042),
	REG(VCAP_CACHE_ACTION_DAT,		0x000082),
	REG(VCAP_CACHE_CNT_DAT,			0x0000c2),
	REG(VCAP_CACHE_TG_DAT,			0x0000e2),
	REG(VCAP_CONST_VCAP_VER,		0x0000e6),
	REG(VCAP_CONST_ENTRY_WIDTH,		0x0000e7),
	REG(VCAP_CONST_ENTRY_CNT,		0x0000e8),
	REG(VCAP_CONST_ENTRY_SWCNT,		0x0000e9),
	REG(VCAP_CONST_ENTRY_TG_WIDTH,		0x0000ea),
	REG(VCAP_CONST_ACTION_DEF_CNT,		0x0000eb),
	REG(VCAP_CONST_ACTION_WIDTH,		0x0000ec),
	REG(VCAP_CONST_CNT_WIDTH,		0x0000ed),
	REG(VCAP_CONST_CORE_CNT,		0x0000ee),
	REG(VCAP_CONST_IF_CNT,			0x0000ef),
};

static const u32 vsc7512_qsys_regmap[] = {
	REG(QSYS_PORT_MODE,			0x4480),
	REG(QSYS_SWITCH_PORT_MODE,		0x448d),
	REG(QSYS_STAT_CNT_CFG,			0x4499),
	REG(QSYS_EEE_CFG,			0x449a),
	REG(QSYS_EEE_THRES,			0x44a5),
	REG(QSYS_IGR_NO_SHARING,		0x44a6),
	REG(QSYS_EGR_NO_SHARING,		0x44a7),
	REG(QSYS_SW_STATUS,			0x44a8),
	REG(QSYS_EXT_CPU_CFG,			0x44b4),
	REG_RESERVED(QSYS_PAD_CFG),
	REG(QSYS_CPU_GROUP_MAP,			0x44b6),
	REG_RESERVED(QSYS_QMAP),
	REG_RESERVED(QSYS_ISDX_SGRP),
	REG_RESERVED(QSYS_TIMED_FRAME_ENTRY),
	REG(QSYS_TFRM_MISC,			0x44c4),
	REG(QSYS_TFRM_PORT_DLY,			0x44c5),
	REG(QSYS_TFRM_TIMER_CFG_1,		0x44c6),
	REG(QSYS_TFRM_TIMER_CFG_2,		0x44c7),
	REG(QSYS_TFRM_TIMER_CFG_3,		0x44c8),
	REG(QSYS_TFRM_TIMER_CFG_4,		0x44c9),
	REG(QSYS_TFRM_TIMER_CFG_5,		0x44ca),
	REG(QSYS_TFRM_TIMER_CFG_6,		0x44cb),
	REG(QSYS_TFRM_TIMER_CFG_7,		0x44cc),
	REG(QSYS_TFRM_TIMER_CFG_8,		0x44cd),
	REG(QSYS_RED_PROFILE,			0x44ce),
	REG(QSYS_RES_QOS_MODE,			0x44de),
	REG(QSYS_RES_CFG,			0x4800),
	REG(QSYS_RES_STAT,			0x4801),
	REG(QSYS_EGR_DROP_MODE,			0x44df),
	REG(QSYS_EQ_CTRL,			0x44e0),
	REG_RESERVED(QSYS_EVENTS_CORE),
	REG(QSYS_CIR_CFG,			0x0000),
	REG(QSYS_EIR_CFG,			0x0001),
	REG(QSYS_SE_CFG,			0x0002),
	REG(QSYS_SE_DWRR_CFG,			0x0003),
	REG_RESERVED(QSYS_SE_CONNECT),
	REG(QSYS_SE_DLB_SENSE,			0x0010),
	REG(QSYS_CIR_STATE,			0x0011),
	REG(QSYS_EIR_STATE,			0x0012),
	REG_RESERVED(QSYS_SE_STATE),
	REG(QSYS_HSCH_MISC_CFG,			0x44e2),
};

static const u32 vsc7512_rew_regmap[] = {
	REG(REW_PORT_VLAN_CFG,			0x000),
	REG(REW_TAG_CFG,			0x001),
	REG(REW_PORT_CFG,			0x002),
	REG(REW_DSCP_CFG,			0x003),
	REG(REW_PCP_DEI_QOS_MAP_CFG,		0x004),
	REG(REW_PTP_CFG,			0x014),
	REG(REW_PTP_DLY1_CFG,			0x015),
	REG(REW_RED_TAG_CFG,			0x016),
	REG(REW_DSCP_REMAP_DP1_CFG,		0x104),
	REG(REW_DSCP_REMAP_CFG,			0x144),
	REG_RESERVED(REW_STAT_CFG),
	REG_RESERVED(REW_REW_STICKY),
	REG_RESERVED(REW_PPT),
};

static const u32 vsc7512_sys_regmap[] = {
	REG(SYS_COUNT_RX_OCTETS,		0x000),
	REG(SYS_COUNT_RX_MULTICAST,		0x002),
	REG(SYS_COUNT_RX_SHORTS,		0x004),
	REG(SYS_COUNT_RX_FRAGMENTS,		0x005),
	REG(SYS_COUNT_RX_JABBERS,		0x006),
	REG(SYS_COUNT_RX_64,			0x009),
	REG(SYS_COUNT_RX_65_127,		0x00a),
	REG(SYS_COUNT_RX_128_255,		0x00b),
	REG(SYS_COUNT_RX_256_1023,		0x00c),
	REG(SYS_COUNT_RX_1024_1526,		0x00d),
	REG(SYS_COUNT_RX_1527_MAX,		0x00e),
	REG(SYS_COUNT_RX_LONGS,			0x011),
	REG(SYS_COUNT_TX_OCTETS,		0x080),
	REG(SYS_COUNT_TX_COLLISION,		0x084),
	REG(SYS_COUNT_TX_DROPS,			0x085),
	REG(SYS_COUNT_TX_64,			0x087),
	REG(SYS_COUNT_TX_65_127,		0x088),
	REG(SYS_COUNT_TX_128_511,		0x089),
	REG(SYS_COUNT_TX_512_1023,		0x08a),
	REG(SYS_COUNT_TX_1024_1526,		0x08b),
	REG(SYS_COUNT_TX_1527_MAX,		0x08c),
	REG(SYS_COUNT_TX_AGING,			0x09e),
	REG(SYS_RESET_CFG,			0x142),
	REG(SYS_VLAN_ETYPE_CFG,			0x144),
	REG(SYS_PORT_MODE,			0x145),
	REG(SYS_FRONT_PORT_MODE,		0x152),
	REG(SYS_FRM_AGING,			0x15d),
	REG(SYS_STAT_CFG,			0x15e),
	REG_RESERVED(SYS_MISC_CFG),
	REG(SYS_REW_MAC_HIGH_CFG,		0x16c),
	REG(SYS_REW_MAC_LOW_CFG,		0x177),
	REG(SYS_PAUSE_CFG,			0x182),
	REG(SYS_PAUSE_TOT_CFG,			0x18e),
	REG(SYS_ATOP,				0x18f),
	REG(SYS_ATOP_TOT_CFG,			0x19b),
	REG(SYS_MAC_FC_CFG,			0x19c),
	REG(SYS_MMGT,				0x1a7),
	REG_RESERVED(SYS_MMGT_FAST),
	REG_RESERVED(SYS_EVENTS_DIF),
	REG_RESERVED(SYS_EVENTS_CORE),
	REG_RESERVED(SYS_CNT),
	REG(SYS_PTP_STATUS,			0x1ae),
	REG(SYS_PTP_TXSTAMP,			0x1af),
	REG(SYS_PTP_NXT,			0x1b0),
	REG(SYS_PTP_CFG,			0x1b1),
	REG_RESERVED(SYS_CM_ADDR),
	REG_RESERVED(SYS_CM_DATA_WR),
	REG_RESERVED(SYS_CM_DATA_RD),
	REG_RESERVED(SYS_CM_OP),
	REG_RESERVED(SYS_CM_DATA),
};

static const u32 vsc7512_ptp_regmap[] = {
	REG(PTP_PIN_CFG,			0x000000),
	REG(PTP_PIN_TOD_SEC_MSB,		0x000001),
	REG(PTP_PIN_TOD_SEC_LSB,		0x000002),
	REG(PTP_PIN_TOD_NSEC,			0x000003),
	REG(PTP_PIN_WF_HIGH_PERIOD,		0x000005),
	REG(PTP_PIN_WF_LOW_PERIOD,		0x000006),
	REG(PTP_CFG_MISC,			0x000028),
	REG(PTP_CLK_CFG_ADJ_CFG,		0x000029),
	REG(PTP_CLK_CFG_ADJ_FREQ,		0x00002a),
};

static const u32 vsc7512_gcb_regmap[] = {
	REG(GCB_SOFT_RST,			0xc002),
};

static const u32 vsc7512_dev_gmii_regmap[] = {
	REG(DEV_CLOCK_CFG,			0x0),
	REG(DEV_PORT_MISC,			0x1),
	REG(DEV_EEE_CFG,			0x3),
	REG(DEV_RX_PATH_DELAY,			0x4),
	REG(DEV_TX_PATH_DELAY,			0x5),
	REG(DEV_PTP_PREDICT_CFG,		0x6),
	REG(DEV_MAC_ENA_CFG,			0x7),
	REG(DEV_MAC_MODE_CFG,			0x8),
	REG(DEV_MAC_MAXLEN_CFG,			0x9),
	REG(DEV_MAC_TAGS_CFG,			0xa),
	REG(DEV_MAC_ADV_CHK_CFG,		0xb),
	REG(DEV_MAC_IFG_CFG,			0xc),
	REG(DEV_MAC_HDX_CFG,			0xd),
	REG(DEV_MAC_DBG_CFG,			0xe),
	REG(DEV_MAC_FC_MAC_LOW_CFG,		0xf),
	REG(DEV_MAC_FC_MAC_HIGH_CFG,		0x10),
	REG(DEV_MAC_STICKY,			0x11),
	REG_RESERVED(PCS1G_CFG),
	REG_RESERVED(PCS1G_MODE_CFG),
	REG_RESERVED(PCS1G_SD_CFG),
	REG_RESERVED(PCS1G_ANEG_CFG),
	REG_RESERVED(PCS1G_ANEG_NP_CFG),
	REG_RESERVED(PCS1G_LB_CFG),
	REG_RESERVED(PCS1G_DBG_CFG),
	REG_RESERVED(PCS1G_CDET_CFG),
	REG_RESERVED(PCS1G_ANEG_STATUS),
	REG_RESERVED(PCS1G_ANEG_NP_STATUS),
	REG_RESERVED(PCS1G_LINK_STATUS),
	REG_RESERVED(PCS1G_LINK_DOWN_CNT),
	REG_RESERVED(PCS1G_STICKY),
	REG_RESERVED(PCS1G_DEBUG_STATUS),
	REG_RESERVED(PCS1G_LPI_CFG),
	REG_RESERVED(PCS1G_LPI_WAKE_ERROR_CNT),
	REG_RESERVED(PCS1G_LPI_STATUS),
	REG_RESERVED(PCS1G_TSTPAT_MODE_CFG),
	REG_RESERVED(PCS1G_TSTPAT_STATUS),
	REG_RESERVED(DEV_PCS_FX100_CFG),
	REG_RESERVED(DEV_PCS_FX100_STATUS),
};

static const u32 vsc7512_cpu_org_regmap[] = {
	REG(DEV_CPUORG_IF_CTRL,			0x0000),
	REG(DEV_CPUORG_IF_CFGSTAT,		0x0001),
	REG(DEV_CPUORG_ORG_CFG,			0x0002),
	REG(DEV_CPUORG_ERR_CNTS,		0x0003),
	REG(DEV_CPUORG_TIMEOUT_CFG,		0x0004),
	REG(DEV_CPUORG_GPR,			0x0005),
	REG(DEV_CPUORG_MAILBOX_SET,		0x0006),
	REG(DEV_CPUORG_MAILBOX_CLR,		0x0007),
	REG(DEV_CPUORG_MAILBOX,			0x0008),
	REG(DEV_CPUORG_SEMA_CFG,		0x0009),
	REG(DEV_CPUORG_SEMA0,			0x000a),
	REG(DEV_CPUORG_SEMA0_OWNER,		0x000b),
	REG(DEV_CPUORG_SEMA1,			0x000c),
	REG(DEV_CPUORG_SEMA1_OWNER,		0x000d),
};

static const u32 *vsc7512_regmap[TARGET_MAX] = {
	[ANA] = vsc7512_ana_regmap,
	[QS] = vsc7512_qs_regmap,
	[QSYS] = vsc7512_qsys_regmap,
	[REW] = vsc7512_rew_regmap,
	[SYS] = vsc7512_sys_regmap,
	[S0] = vsc7512_vcap_regmap,
	[S1] = vsc7512_vcap_regmap,
	[S2] = vsc7512_vcap_regmap,
	[PTP] = vsc7512_ptp_regmap,
	[GCB] = vsc7512_gcb_regmap,
	[DEV_GMII] = vsc7512_dev_gmii_regmap,
	[DEV_CPUORG] = vsc7512_cpu_org_regmap,
};

static int vsc7512_soft_rst_status(struct ocelot *ocelot)
{
	int val;

	ocelot_field_read(ocelot, GCB_SOFT_RST_SWC_RST, &val);

	return val;
}

#define VSC7512_INIT_TIMEOUT 50000
#define VSC7512_GCB_RST_SLEEP 100

static int vsc7512_reset(struct ocelot *ocelot)
{
	int val, err;

	ocelot_field_write(ocelot, GCB_SOFT_RST_SWC_RST, 1);

	err = readx_poll_timeout(vsc7512_soft_rst_status, ocelot, val, !val,
				 VSC7512_GCB_RST_SLEEP, VSC7512_INIT_TIMEOUT);
	if (err) {
		dev_err(ocelot->dev, "timeout: switch core reset\n");
		return err;
	}

	return 0;
}

static u16 vsc7512_wm_enc(u16 value)
{
	WARN_ON(value >= 16 * BIT(8));

	if (value >= BIT(8))
		return BIT(8) | (value / 16);

	return value;
}

static const struct ocelot_ops vsc7512_ops = {
	.reset		= vsc7512_reset,
	.wm_enc		= vsc7512_wm_enc,
	.port_to_netdev	= felix_port_to_netdev,
	.netdev_to_port	= felix_netdev_to_port,
};

/* Addresses are relative to the SPI device's base address, downshifted by 2*/
static const struct resource vsc7512_target_io_res[TARGET_MAX] = {
	[ANA] = {
		.start	= 0x1c620000,
		.end	= 0x1c623fff,
		.name	= "ana",
	},
	[QS] = {
		.start	= 0x1c420000,
		.end	= 0x1c42003f,
		.name	= "qs",
	},
	[QSYS] = {
		.start	= 0x1c600000,
		.end	= 0x1c607fff,
		.name	= "qsys",
	},
	[REW] = {
		.start	= 0x1c40c000,
		.end	= 0x1c40ffff,
		.name	= "rew",
	},
	[SYS] = {
		.start	= 0x1c404000,
		.end	= 0x1c407fff,
		.name	= "sys",
	},
	[S0] = {
		.start	= 0x1c410000,
		.end	= 0x1c4100ff,
		.name	= "s0",
	},
	[S1] = {
		.start	= 0x1c414000,
		.end	= 0x1c4140ff,
		.name	= "s1",
	},
	[S2] = {
		.start	= 0x1c418000,
		.end	= 0x1c4180ff,
		.name	= "s2",
	},
	[GCB] =	{
		.start	= 0x1c41c000,
		.end	= 0x1c41c07f,
		.name	= "devcpu_gcb",
	},
	[DEV_CPUORG] = {
		.start	= 0x1c400000,
		.end	= 0x1c4000ff,
		.name	= "devcpu_org",
	},
};

static const struct resource vsc7512_port_io_res[] = {
	{
		.start	= 0x1c478000,
		.end	= 0x1c47bfff,
		.name	= "port0",
	},
	{
		.start	= 0x1c47c000,
		.end	= 0x1c47ffff,
		.name	= "port1",
	},
	{
		.start	= 0x1c480000,
		.end	= 0x1c483fff,
		.name	= "port2",
	},
	{
		.start	= 0x1c484000,
		.end	= 0x1c487fff,
		.name	= "port3",
	},
	{
		.start	= 0x1c488000,
		.end	= 0x1c48bfff,
		.name	= "port4",
	},
	{
		.start	= 0x1c48c000,
		.end	= 0x1c48ffff,
		.name	= "port5",
	},
};

static const struct reg_field vsc7512_regfields[REGFIELD_MAX] = {
	[ANA_ADVLEARN_VLAN_CHK] = REG_FIELD(ANA_ADVLEARN, 11, 11),
	[ANA_ADVLEARN_LEARN_MIRROR] = REG_FIELD(ANA_ADVLEARN, 0, 10),
	[ANA_ANEVENTS_MSTI_DROP] = REG_FIELD(ANA_ANEVENTS, 27, 27),
	[ANA_ANEVENTS_ACLKILL] = REG_FIELD(ANA_ANEVENTS, 26, 26),
	[ANA_ANEVENTS_ACLUSED] = REG_FIELD(ANA_ANEVENTS, 25, 25),
	[ANA_ANEVENTS_AUTOAGE] = REG_FIELD(ANA_ANEVENTS, 24, 24),
	[ANA_ANEVENTS_VS2TTL1] = REG_FIELD(ANA_ANEVENTS, 23, 23),
	[ANA_ANEVENTS_STORM_DROP] = REG_FIELD(ANA_ANEVENTS, 22, 22),
	[ANA_ANEVENTS_LEARN_DROP] = REG_FIELD(ANA_ANEVENTS, 21, 21),
	[ANA_ANEVENTS_AGED_ENTRY] = REG_FIELD(ANA_ANEVENTS, 20, 20),
	[ANA_ANEVENTS_CPU_LEARN_FAILED] = REG_FIELD(ANA_ANEVENTS, 19, 19),
	[ANA_ANEVENTS_AUTO_LEARN_FAILED] = REG_FIELD(ANA_ANEVENTS, 18, 18),
	[ANA_ANEVENTS_LEARN_REMOVE] = REG_FIELD(ANA_ANEVENTS, 17, 17),
	[ANA_ANEVENTS_AUTO_LEARNED] = REG_FIELD(ANA_ANEVENTS, 16, 16),
	[ANA_ANEVENTS_AUTO_MOVED] = REG_FIELD(ANA_ANEVENTS, 15, 15),
	[ANA_ANEVENTS_DROPPED] = REG_FIELD(ANA_ANEVENTS, 14, 14),
	[ANA_ANEVENTS_CLASSIFIED_DROP] = REG_FIELD(ANA_ANEVENTS, 13, 13),
	[ANA_ANEVENTS_CLASSIFIED_COPY] = REG_FIELD(ANA_ANEVENTS, 12, 12),
	[ANA_ANEVENTS_VLAN_DISCARD] = REG_FIELD(ANA_ANEVENTS, 11, 11),
	[ANA_ANEVENTS_FWD_DISCARD] = REG_FIELD(ANA_ANEVENTS, 10, 10),
	[ANA_ANEVENTS_MULTICAST_FLOOD] = REG_FIELD(ANA_ANEVENTS, 9, 9),
	[ANA_ANEVENTS_UNICAST_FLOOD] = REG_FIELD(ANA_ANEVENTS, 8, 8),
	[ANA_ANEVENTS_DEST_KNOWN] = REG_FIELD(ANA_ANEVENTS, 7, 7),
	[ANA_ANEVENTS_BUCKET3_MATCH] = REG_FIELD(ANA_ANEVENTS, 6, 6),
	[ANA_ANEVENTS_BUCKET2_MATCH] = REG_FIELD(ANA_ANEVENTS, 5, 5),
	[ANA_ANEVENTS_BUCKET1_MATCH] = REG_FIELD(ANA_ANEVENTS, 4, 4),
	[ANA_ANEVENTS_BUCKET0_MATCH] = REG_FIELD(ANA_ANEVENTS, 3, 3),
	[ANA_ANEVENTS_CPU_OPERATION] = REG_FIELD(ANA_ANEVENTS, 2, 2),
	[ANA_ANEVENTS_DMAC_LOOKUP] = REG_FIELD(ANA_ANEVENTS, 1, 1),
	[ANA_ANEVENTS_SMAC_LOOKUP] = REG_FIELD(ANA_ANEVENTS, 0, 0),
	[ANA_TABLES_MACACCESS_B_DOM] = REG_FIELD(ANA_TABLES_MACACCESS, 18, 18),
	[ANA_TABLES_MACTINDX_BUCKET] = REG_FIELD(ANA_TABLES_MACTINDX, 10, 11),
	[ANA_TABLES_MACTINDX_M_INDEX] = REG_FIELD(ANA_TABLES_MACTINDX, 0, 9),
	[GCB_SOFT_RST_SWC_RST] = REG_FIELD(GCB_SOFT_RST, 0, 0),
	[QSYS_TIMED_FRAME_ENTRY_TFRM_VLD] = REG_FIELD(QSYS_TIMED_FRAME_ENTRY, 20, 20),
	[QSYS_TIMED_FRAME_ENTRY_TFRM_FP] = REG_FIELD(QSYS_TIMED_FRAME_ENTRY, 8, 19),
	[QSYS_TIMED_FRAME_ENTRY_TFRM_PORTNO] = REG_FIELD(QSYS_TIMED_FRAME_ENTRY, 4, 7),
	[QSYS_TIMED_FRAME_ENTRY_TFRM_TM_SEL] = REG_FIELD(QSYS_TIMED_FRAME_ENTRY, 1, 3),
	[QSYS_TIMED_FRAME_ENTRY_TFRM_TM_T] = REG_FIELD(QSYS_TIMED_FRAME_ENTRY, 0, 0),
	[SYS_RESET_CFG_CORE_ENA] = REG_FIELD(SYS_RESET_CFG, 2, 2),
	[SYS_RESET_CFG_MEM_ENA] = REG_FIELD(SYS_RESET_CFG, 1, 1),
	[SYS_RESET_CFG_MEM_INIT] = REG_FIELD(SYS_RESET_CFG, 0, 0),
	/* Replicated per number of ports (12), register size 4 per port */
	[QSYS_SWITCH_PORT_MODE_PORT_ENA] = REG_FIELD_ID(QSYS_SWITCH_PORT_MODE, 14, 14, 12, 4),
	[QSYS_SWITCH_PORT_MODE_SCH_NEXT_CFG] = REG_FIELD_ID(QSYS_SWITCH_PORT_MODE, 11, 13, 12, 4),
	[QSYS_SWITCH_PORT_MODE_YEL_RSRVD] = REG_FIELD_ID(QSYS_SWITCH_PORT_MODE, 10, 10, 12, 4),
	[QSYS_SWITCH_PORT_MODE_INGRESS_DROP_MODE] = REG_FIELD_ID(QSYS_SWITCH_PORT_MODE, 9, 9, 12, 4),
	[QSYS_SWITCH_PORT_MODE_TX_PFC_ENA] = REG_FIELD_ID(QSYS_SWITCH_PORT_MODE, 1, 8, 12, 4),
	[QSYS_SWITCH_PORT_MODE_TX_PFC_MODE] = REG_FIELD_ID(QSYS_SWITCH_PORT_MODE, 0, 0, 12, 4),
	[SYS_PORT_MODE_DATA_WO_TS] = REG_FIELD_ID(SYS_PORT_MODE, 5, 6, 12, 4),
	[SYS_PORT_MODE_INCL_INJ_HDR] = REG_FIELD_ID(SYS_PORT_MODE, 3, 4, 12, 4),
	[SYS_PORT_MODE_INCL_XTR_HDR] = REG_FIELD_ID(SYS_PORT_MODE, 1, 2, 12, 4),
	[SYS_PORT_MODE_INCL_HDR_ERR] = REG_FIELD_ID(SYS_PORT_MODE, 0, 0, 12, 4),
	[SYS_PAUSE_CFG_PAUSE_START] = REG_FIELD_ID(SYS_PAUSE_CFG, 10, 18, 12, 4),
	[SYS_PAUSE_CFG_PAUSE_STOP] = REG_FIELD_ID(SYS_PAUSE_CFG, 1, 9, 12, 4),
	[SYS_PAUSE_CFG_PAUSE_ENA] = REG_FIELD_ID(SYS_PAUSE_CFG, 0, 1, 12, 4),
};

static const struct ocelot_stat_layout vsc7512_stats_layout[] = {
	{ .offset = 0x00,	.name = "rx_octets", },
	{ .offset = 0x01,	.name = "rx_unicast", },
	{ .offset = 0x02,	.name = "rx_multicast", },
	{ .offset = 0x03,	.name = "rx_broadcast", },
	{ .offset = 0x04,	.name = "rx_shorts", },
	{ .offset = 0x05,	.name = "rx_fragments", },
	{ .offset = 0x06,	.name = "rx_jabbers", },
	{ .offset = 0x07,	.name = "rx_crc_align_errs", },
	{ .offset = 0x08,	.name = "rx_sym_errs", },
	{ .offset = 0x09,	.name = "rx_frames_below_65_octets", },
	{ .offset = 0x0A,	.name = "rx_frames_65_to_127_octets", },
	{ .offset = 0x0B,	.name = "rx_frames_128_to_255_octets", },
	{ .offset = 0x0C,	.name = "rx_frames_256_to_511_octets", },
	{ .offset = 0x0D,	.name = "rx_frames_512_to_1023_octets", },
	{ .offset = 0x0E,	.name = "rx_frames_1024_to_1526_octets", },
	{ .offset = 0x0F,	.name = "rx_frames_over_1526_octets", },
	{ .offset = 0x10,	.name = "rx_pause", },
	{ .offset = 0x11,	.name = "rx_control", },
	{ .offset = 0x12,	.name = "rx_longs", },
	{ .offset = 0x13,	.name = "rx_classified_drops", },
	{ .offset = 0x14,	.name = "rx_red_prio_0", },
	{ .offset = 0x15,	.name = "rx_red_prio_1", },
	{ .offset = 0x16,	.name = "rx_red_prio_2", },
	{ .offset = 0x17,	.name = "rx_red_prio_3", },
	{ .offset = 0x18,	.name = "rx_red_prio_4", },
	{ .offset = 0x19,	.name = "rx_red_prio_5", },
	{ .offset = 0x1A,	.name = "rx_red_prio_6", },
	{ .offset = 0x1B,	.name = "rx_red_prio_7", },
	{ .offset = 0x1C,	.name = "rx_yellow_prio_0", },
	{ .offset = 0x1D,	.name = "rx_yellow_prio_1", },
	{ .offset = 0x1E,	.name = "rx_yellow_prio_2", },
	{ .offset = 0x1F,	.name = "rx_yellow_prio_3", },
	{ .offset = 0x20,	.name = "rx_yellow_prio_4", },
	{ .offset = 0x21,	.name = "rx_yellow_prio_5", },
	{ .offset = 0x22,	.name = "rx_yellow_prio_6", },
	{ .offset = 0x23,	.name = "rx_yellow_prio_7", },
	{ .offset = 0x24,	.name = "rx_green_prio_0", },
	{ .offset = 0x25,	.name = "rx_green_prio_1", },
	{ .offset = 0x26,	.name = "rx_green_prio_2", },
	{ .offset = 0x27,	.name = "rx_green_prio_3", },
	{ .offset = 0x28,	.name = "rx_green_prio_4", },
	{ .offset = 0x29,	.name = "rx_green_prio_5", },
	{ .offset = 0x2A,	.name = "rx_green_prio_6", },
	{ .offset = 0x2B,	.name = "rx_green_prio_7", },
	{ .offset = 0x40,	.name = "tx_octets", },
	{ .offset = 0x41,	.name = "tx_unicast", },
	{ .offset = 0x42,	.name = "tx_multicast", },
	{ .offset = 0x43,	.name = "tx_broadcast", },
	{ .offset = 0x44,	.name = "tx_collision", },
	{ .offset = 0x45,	.name = "tx_drops", },
	{ .offset = 0x46,	.name = "tx_pause", },
	{ .offset = 0x47,	.name = "tx_frames_below_65_octets", },
	{ .offset = 0x48,	.name = "tx_frames_65_to_127_octets", },
	{ .offset = 0x49,	.name = "tx_frames_128_255_octets", },
	{ .offset = 0x4A,	.name = "tx_frames_256_511_octets", },
	{ .offset = 0x4B,	.name = "tx_frames_512_1023_octets", },
	{ .offset = 0x4C,	.name = "tx_frames_1024_1526_octets", },
	{ .offset = 0x4D,	.name = "tx_frames_over_1526_octets", },
	{ .offset = 0x4E,	.name = "tx_yellow_prio_0", },
	{ .offset = 0x4F,	.name = "tx_yellow_prio_1", },
	{ .offset = 0x50,	.name = "tx_yellow_prio_2", },
	{ .offset = 0x51,	.name = "tx_yellow_prio_3", },
	{ .offset = 0x52,	.name = "tx_yellow_prio_4", },
	{ .offset = 0x53,	.name = "tx_yellow_prio_5", },
	{ .offset = 0x54,	.name = "tx_yellow_prio_6", },
	{ .offset = 0x55,	.name = "tx_yellow_prio_7", },
	{ .offset = 0x56,	.name = "tx_green_prio_0", },
	{ .offset = 0x57,	.name = "tx_green_prio_1", },
	{ .offset = 0x58,	.name = "tx_green_prio_2", },
	{ .offset = 0x59,	.name = "tx_green_prio_3", },
	{ .offset = 0x5A,	.name = "tx_green_prio_4", },
	{ .offset = 0x5B,	.name = "tx_green_prio_5", },
	{ .offset = 0x5C,	.name = "tx_green_prio_6", },
	{ .offset = 0x5D,	.name = "tx_green_prio_7", },
	{ .offset = 0x5E,	.name = "tx_aged", },
	{ .offset = 0x80,	.name = "drop_local", },
	{ .offset = 0x81,	.name = "drop_tail", },
	{ .offset = 0x82,	.name = "drop_yellow_prio_0", },
	{ .offset = 0x83,	.name = "drop_yellow_prio_1", },
	{ .offset = 0x84,	.name = "drop_yellow_prio_2", },
	{ .offset = 0x85,	.name = "drop_yellow_prio_3", },
	{ .offset = 0x86,	.name = "drop_yellow_prio_4", },
	{ .offset = 0x87,	.name = "drop_yellow_prio_5", },
	{ .offset = 0x88,	.name = "drop_yellow_prio_6", },
	{ .offset = 0x89,	.name = "drop_yellow_prio_7", },
	{ .offset = 0x8A,	.name = "drop_green_prio_0", },
	{ .offset = 0x8B,	.name = "drop_green_prio_1", },
	{ .offset = 0x8C,	.name = "drop_green_prio_2", },
	{ .offset = 0x8D,	.name = "drop_green_prio_3", },
	{ .offset = 0x8E,	.name = "drop_green_prio_4", },
	{ .offset = 0x8F,	.name = "drop_green_prio_5", },
	{ .offset = 0x90,	.name = "drop_green_prio_6", },
	{ .offset = 0x91,	.name = "drop_green_prio_7", },
};

struct felix_spi_data {
	struct regmap *map;
	struct felix felix;
};

/* TODO: The regmap has overlaps for DEVCPU_ORG and other peripherals. I believe
 * this is handled with a page register, and should hopefully be easy to use
 * with the existing regmap_config with regmap_range_cfg
 */
static const struct regmap_config felix_spi_regmap_config = {
	.reg_bits = 24,
	.reg_stride = 1,
	.val_bits = 32,

	.write_flag_mask = BIT(7),
	.max_register = 0x3fffffff,
	.use_single_write = true,
	.use_single_read = true,
	.can_multi_write = false,

	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_NATIVE,
};

#define VSC7512_BYTE_ORDER_LE 0x00000000
#define VSC7512_BYTE_ORDER_BE 0x81818181
#define VSC7512_BIT_ORDER_MSB 0x00000000
#define VSC7512_BIT_ORDER_LSB 0x42424242

static int felix_spi_init_bus(struct spi_device *spi,
			      struct felix_spi_data *felix_spi)
{
	int err;
	u32 val, check;

	val = 0;

#ifdef __LITTLE_ENDIAN
	val |= VSC7512_BYTE_ORDER_LE;
#else
	val |= VSC7512_BYTE_ORDER_BE;
#endif

	// Hard code this write to set up the interface. After this is done,
	// ocelot_read interface should be able to be used.
	err = regmap_write(felix_spi->map, (0x71000000 >> 2) & 0x3fffff, val);
	if (err < 0) {
		dev_err(&spi->dev, "error %d configuring SPI interface\n", err);
		return err;
	}

	val = 0x00000000;
	err = regmap_write(felix_spi->map, (0x71000004 >> 2) & 0x3fffff, val);
	if (err < 0) {
		dev_err(&spi->dev, "error %d writing padding bytes\n", err);
		return err;
	}

	check = val | 0x02000000;

	err = regmap_read(felix_spi->map, (0x71000004 >> 2) & 0x3fffff, &val);
	if (err < 0) {
		dev_err(&spi->dev, "Error %d writing padding bytes\n", err);
		return err;
	} else if (check != val) {
		dev_err(&spi->dev,
			"Error configuring SPI bus. V: 0x%08x != 0x%08x\n", val,
			check);
		return -ENODEV;
	}

	return err;
}

static void vsc7512_phylink_validate(struct ocelot *ocelot, int port,
				     unsigned long *supported,
				     struct phylink_link_state *state)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	__ETHTOOL_DECLARE_LINK_MODE_MASK(mask) = {
		0,
	};

	if (state->interface != PHY_INTERFACE_MODE_NA &&
	    state->interface != ocelot_port->phy_mode) {
		bitmap_zero(supported, __ETHTOOL_LINK_MODE_MASK_NBITS);
		return;
	}

	phylink_set_port_modes(mask);
	phylink_set(mask, Autoneg);
	phylink_set(mask, Pause);
	phylink_set(mask, Asym_Pause);
	phylink_set(mask, 10baseT_Half);
	phylink_set(mask, 10baseT_Full);
	phylink_set(mask, 100baseT_Half);
	phylink_set(mask, 100baseT_Full);
	phylink_set(mask, 1000baseT_Half);
	phylink_set(mask, 1000baseT_Full);

	if (state->interface == PHY_INTERFACE_MODE_INTERNAL ||
	    state->interface == PHY_INTERFACE_MODE_INTERNAL ||
	    state->interface == PHY_INTERFACE_MODE_INTERNAL) {
		phylink_set(mask, 2500baseT_Full);
		phylink_set(mask, 2500baseX_Full);
	}

	bitmap_and(supported, supported, mask, __ETHTOOL_LINK_MODE_MASK_NBITS);
	bitmap_and(state->advertising, state->advertising, mask,
		   __ETHTOOL_LINK_MODE_MASK_NBITS);
}

static int vsc7512_prevalidate_phy_mode(struct ocelot *ocelot, int port,
					phy_interface_t phy_mode)
{
	switch (phy_mode) {
	case PHY_INTERFACE_MODE_INTERNAL:
		// TODO: I don't know what ports would not be supported
		// internally.
		return 0;
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_QSGMII:
	case PHY_INTERFACE_MODE_USXGMII:
	case PHY_INTERFACE_MODE_2500BASEX:
		if (port == 4 || port == 5)
			return -EOPNOTSUPP;
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int vsc7512_port_setup_tc(struct dsa_switch *ds, int port,
				 enum tc_setup_type type, void *type_data)
{
	return -EOPNOTSUPP;
}

static void vsc7512_sched_speed_set(struct ocelot *ocelot, int port, u32 speed)
{
	u8 tas_speed;

	switch (speed) {
	case SPEED_10:
		tas_speed = OCELOT_SPEED_10;
		break;

	case SPEED_100:
		tas_speed = OCELOT_SPEED_100;
		break;

	case SPEED_1000:
		tas_speed = OCELOT_SPEED_1000;
		break;

	case SPEED_2500:
		tas_speed = OCELOT_SPEED_2500;
		break;

	default:
		tas_speed = OCELOT_SPEED_1000;
		break;
	}

	ocelot_rmw_rix(ocelot, QSYS_TAG_CONFIG_LINK_SPEED(tas_speed),
		       QSYS_TAG_CONFIG_LINK_SPEED_M, QSYS_TAG_CONFIG, port);
}

static const struct vcap_field vsc7512_vcap_es0_keys[] = {
	[VCAP_ES0_EGR_PORT]                     = { 0,   4 },
	[VCAP_ES0_IGR_PORT]                     = { 4,   4 },
	[VCAP_ES0_RSV]                          = { 8,   2 },
	[VCAP_ES0_L2_MC]                        = { 10,  1 },
	[VCAP_ES0_L2_BC]                        = { 11,  1 },
	[VCAP_ES0_VID]                          = { 12, 12 },
	[VCAP_ES0_DP]                           = { 24,  1 },
	[VCAP_ES0_PCP]                          = { 25,  3 },
};

static const struct vcap_field vsc7512_vcap_es0_actions[]   = {
	[VCAP_ES0_ACT_PUSH_OUTER_TAG]           = { 0,   2 },
	[VCAP_ES0_ACT_PUSH_INNER_TAG]           = { 2,   1 },
	[VCAP_ES0_ACT_TAG_A_TPID_SEL]           = { 3,   2 },
	[VCAP_ES0_ACT_TAG_A_VID_SEL]            = { 5,   1 },
	[VCAP_ES0_ACT_TAG_A_PCP_SEL]            = { 6,   2 },
	[VCAP_ES0_ACT_TAG_A_DEI_SEL]            = { 8,   2 },
	[VCAP_ES0_ACT_TAG_B_TPID_SEL]           = { 10,  2 },
	[VCAP_ES0_ACT_TAG_B_VID_SEL]            = { 12,  1 },
	[VCAP_ES0_ACT_TAG_B_PCP_SEL]            = { 13,  2 },
	[VCAP_ES0_ACT_TAG_B_DEI_SEL]            = { 15,  2 },
	[VCAP_ES0_ACT_VID_A_VAL]                = { 17, 12 },
	[VCAP_ES0_ACT_PCP_A_VAL]                = { 29,  3 },
	[VCAP_ES0_ACT_DEI_A_VAL]                = { 32,  1 },
	[VCAP_ES0_ACT_VID_B_VAL]                = { 33, 12 },
	[VCAP_ES0_ACT_PCP_B_VAL]                = { 45,  3 },
	[VCAP_ES0_ACT_DEI_B_VAL]                = { 48,  1 },
	[VCAP_ES0_ACT_RSV]                      = { 49, 24 },
	[VCAP_ES0_ACT_HIT_STICKY]               = { 73,  1 },
};

static const struct vcap_field vsc7512_vcap_is1_keys[] = {
	[VCAP_IS1_HK_TYPE]                      = { 0,    1 },
	[VCAP_IS1_HK_LOOKUP]                    = { 1,    2 },
	[VCAP_IS1_HK_IGR_PORT_MASK]             = { 3,   12 },
	[VCAP_IS1_HK_RSV]                       = { 15,   9 },
	[VCAP_IS1_HK_OAM_Y1731]                 = { 24,   1 },
	[VCAP_IS1_HK_L2_MC]                     = { 25,   1 },
	[VCAP_IS1_HK_L2_BC]                     = { 26,   1 },
	[VCAP_IS1_HK_IP_MC]                     = { 27,   1 },
	[VCAP_IS1_HK_VLAN_TAGGED]               = { 28,   1 },
	[VCAP_IS1_HK_VLAN_DBL_TAGGED]           = { 29,   1 },
	[VCAP_IS1_HK_TPID]                      = { 30,   1 },
	[VCAP_IS1_HK_VID]                       = { 31,  12 },
	[VCAP_IS1_HK_DEI]                       = { 43,   1 },
	[VCAP_IS1_HK_PCP]                       = { 44,   3 },
	/* Specific Fields for IS1 Half Key S1_NORMAL */
	[VCAP_IS1_HK_L2_SMAC]                   = { 47,  48 },
	[VCAP_IS1_HK_ETYPE_LEN]                 = { 95,   1 },
	[VCAP_IS1_HK_ETYPE]                     = { 96,  16 },
	[VCAP_IS1_HK_IP_SNAP]                   = { 112,  1 },
	[VCAP_IS1_HK_IP4]                       = { 113,  1 },
	/* Layer-3 Information */
	[VCAP_IS1_HK_L3_FRAGMENT]               = { 114,  1 },
	[VCAP_IS1_HK_L3_FRAG_OFS_GT0]           = { 115,  1 },
	[VCAP_IS1_HK_L3_OPTIONS]                = { 116,  1 },
	[VCAP_IS1_HK_L3_DSCP]                   = { 117,  6 },
	[VCAP_IS1_HK_L3_IP4_SIP]                = { 123, 32 },
	/* Layer-4 Information */
	[VCAP_IS1_HK_TCP_UDP]                   = { 155,  1 },
	[VCAP_IS1_HK_TCP]                       = { 156,  1 },
	[VCAP_IS1_HK_L4_SPORT]                  = { 157, 16 },
	[VCAP_IS1_HK_L4_RNG]                    = { 173,  8 },
	/* Specific Fields for IS1 Half Key S1_5TUPLE_IP4 */
	[VCAP_IS1_HK_IP4_INNER_TPID]            = { 47,   1 },
	[VCAP_IS1_HK_IP4_INNER_VID]             = { 48,  12 },
	[VCAP_IS1_HK_IP4_INNER_DEI]             = { 60,   1 },
	[VCAP_IS1_HK_IP4_INNER_PCP]             = { 61,   3 },
	[VCAP_IS1_HK_IP4_IP4]                   = { 64,   1 },
	[VCAP_IS1_HK_IP4_L3_FRAGMENT]           = { 65,   1 },
	[VCAP_IS1_HK_IP4_L3_FRAG_OFS_GT0]       = { 66,   1 },
	[VCAP_IS1_HK_IP4_L3_OPTIONS]            = { 67,   1 },
	[VCAP_IS1_HK_IP4_L3_DSCP]               = { 68,   6 },
	[VCAP_IS1_HK_IP4_L3_IP4_DIP]            = { 74,  32 },
	[VCAP_IS1_HK_IP4_L3_IP4_SIP]            = { 106, 32 },
	[VCAP_IS1_HK_IP4_L3_PROTO]              = { 138,  8 },
	[VCAP_IS1_HK_IP4_TCP_UDP]               = { 146,  1 },
	[VCAP_IS1_HK_IP4_TCP]                   = { 147,  1 },
	[VCAP_IS1_HK_IP4_L4_RNG]                = { 148,  8 },
	[VCAP_IS1_HK_IP4_IP_PAYLOAD_S1_5TUPLE]  = { 156, 32 },
};

static const struct vcap_field vsc7512_vcap_is1_actions[] = {
	[VCAP_IS1_ACT_DSCP_ENA]                 = { 0,   1 },
	[VCAP_IS1_ACT_DSCP_VAL]                 = { 1,   6 },
	[VCAP_IS1_ACT_QOS_ENA]                  = { 7,   1 },
	[VCAP_IS1_ACT_QOS_VAL]                  = { 8,   3 },
	[VCAP_IS1_ACT_DP_ENA]                   = { 11,  1 },
	[VCAP_IS1_ACT_DP_VAL]                   = { 12,  1 },
	[VCAP_IS1_ACT_PAG_OVERRIDE_MASK]        = { 13,  8 },
	[VCAP_IS1_ACT_PAG_VAL]                  = { 21,  8 },
	[VCAP_IS1_ACT_RSV]                      = { 29,  9 },
	/* The fields below are incorrectly shifted by 2 in the manual */
	[VCAP_IS1_ACT_VID_REPLACE_ENA]          = { 38,  1 },
	[VCAP_IS1_ACT_VID_ADD_VAL]              = { 39, 12 },
	[VCAP_IS1_ACT_FID_SEL]                  = { 51,  2 },
	[VCAP_IS1_ACT_FID_VAL]                  = { 53, 13 },
	[VCAP_IS1_ACT_PCP_DEI_ENA]              = { 66,  1 },
	[VCAP_IS1_ACT_PCP_VAL]                  = { 67,  3 },
	[VCAP_IS1_ACT_DEI_VAL]                  = { 70,  1 },
	[VCAP_IS1_ACT_VLAN_POP_CNT_ENA]         = { 71,  1 },
	[VCAP_IS1_ACT_VLAN_POP_CNT]             = { 72,  2 },
	[VCAP_IS1_ACT_CUSTOM_ACE_TYPE_ENA]      = { 74,  4 },
	[VCAP_IS1_ACT_HIT_STICKY]               = { 78,  1 },
};

static const struct vcap_field vsc7512_vcap_is2_keys[] = {
	/* Common: 46 bits */
	[VCAP_IS2_TYPE]                         = { 0,    4 },
	[VCAP_IS2_HK_FIRST]                     = { 4,    1 },
	[VCAP_IS2_HK_PAG]                       = { 5,    8 },
	[VCAP_IS2_HK_IGR_PORT_MASK]             = { 13,  12 },
	[VCAP_IS2_HK_RSV2]                      = { 25,   1 },
	[VCAP_IS2_HK_HOST_MATCH]                = { 26,   1 },
	[VCAP_IS2_HK_L2_MC]                     = { 27,   1 },
	[VCAP_IS2_HK_L2_BC]                     = { 28,   1 },
	[VCAP_IS2_HK_VLAN_TAGGED]               = { 29,   1 },
	[VCAP_IS2_HK_VID]                       = { 30,  12 },
	[VCAP_IS2_HK_DEI]                       = { 42,   1 },
	[VCAP_IS2_HK_PCP]                       = { 43,   3 },
	/* MAC_ETYPE / MAC_LLC / MAC_SNAP / OAM common */
	[VCAP_IS2_HK_L2_DMAC]                   = { 46,  48 },
	[VCAP_IS2_HK_L2_SMAC]                   = { 94,  48 },
	/* MAC_ETYPE (TYPE=000) */
	[VCAP_IS2_HK_MAC_ETYPE_ETYPE]           = { 142, 16 },
	[VCAP_IS2_HK_MAC_ETYPE_L2_PAYLOAD0]     = { 158, 16 },
	[VCAP_IS2_HK_MAC_ETYPE_L2_PAYLOAD1]     = { 174,  8 },
	[VCAP_IS2_HK_MAC_ETYPE_L2_PAYLOAD2]     = { 182,  3 },
	/* MAC_LLC (TYPE=001) */
	[VCAP_IS2_HK_MAC_LLC_L2_LLC]            = { 142, 40 },
	/* MAC_SNAP (TYPE=010) */
	[VCAP_IS2_HK_MAC_SNAP_L2_SNAP]          = { 142, 40 },
	/* MAC_ARP (TYPE=011) */
	[VCAP_IS2_HK_MAC_ARP_SMAC]              = { 46,  48 },
	[VCAP_IS2_HK_MAC_ARP_ADDR_SPACE_OK]     = { 94,   1 },
	[VCAP_IS2_HK_MAC_ARP_PROTO_SPACE_OK]    = { 95,   1 },
	[VCAP_IS2_HK_MAC_ARP_LEN_OK]            = { 96,   1 },
	[VCAP_IS2_HK_MAC_ARP_TARGET_MATCH]      = { 97,   1 },
	[VCAP_IS2_HK_MAC_ARP_SENDER_MATCH]      = { 98,   1 },
	[VCAP_IS2_HK_MAC_ARP_OPCODE_UNKNOWN]    = { 99,   1 },
	[VCAP_IS2_HK_MAC_ARP_OPCODE]            = { 100,  2 },
	[VCAP_IS2_HK_MAC_ARP_L3_IP4_DIP]        = { 102, 32 },
	[VCAP_IS2_HK_MAC_ARP_L3_IP4_SIP]        = { 134, 32 },
	[VCAP_IS2_HK_MAC_ARP_DIP_EQ_SIP]        = { 166,  1 },
	/* IP4_TCP_UDP / IP4_OTHER common */
	[VCAP_IS2_HK_IP4]                       = { 46,   1 },
	[VCAP_IS2_HK_L3_FRAGMENT]               = { 47,   1 },
	[VCAP_IS2_HK_L3_FRAG_OFS_GT0]           = { 48,   1 },
	[VCAP_IS2_HK_L3_OPTIONS]                = { 49,   1 },
	[VCAP_IS2_HK_IP4_L3_TTL_GT0]            = { 50,   1 },
	[VCAP_IS2_HK_L3_TOS]                    = { 51,   8 },
	[VCAP_IS2_HK_L3_IP4_DIP]                = { 59,  32 },
	[VCAP_IS2_HK_L3_IP4_SIP]                = { 91,  32 },
	[VCAP_IS2_HK_DIP_EQ_SIP]                = { 123,  1 },
	/* IP4_TCP_UDP (TYPE=100) */
	[VCAP_IS2_HK_TCP]                       = { 124,  1 },
	[VCAP_IS2_HK_L4_DPORT]                  = { 125, 16 },
	[VCAP_IS2_HK_L4_SPORT]                  = { 141, 16 },
	[VCAP_IS2_HK_L4_RNG]                    = { 157,  8 },
	[VCAP_IS2_HK_L4_SPORT_EQ_DPORT]         = { 165,  1 },
	[VCAP_IS2_HK_L4_SEQUENCE_EQ0]           = { 166,  1 },
	[VCAP_IS2_HK_L4_FIN]                    = { 167,  1 },
	[VCAP_IS2_HK_L4_SYN]                    = { 168,  1 },
	[VCAP_IS2_HK_L4_RST]                    = { 169,  1 },
	[VCAP_IS2_HK_L4_PSH]                    = { 170,  1 },
	[VCAP_IS2_HK_L4_ACK]                    = { 171,  1 },
	[VCAP_IS2_HK_L4_URG]                    = { 172,  1 },
	[VCAP_IS2_HK_L4_1588_DOM]               = { 173,  8 },
	[VCAP_IS2_HK_L4_1588_VER]               = { 181,  4 },
	/* IP4_OTHER (TYPE=101) */
	[VCAP_IS2_HK_IP4_L3_PROTO]              = { 124,  8 },
	[VCAP_IS2_HK_L3_PAYLOAD]                = { 132, 56 },
	/* IP6_STD (TYPE=110) */
	[VCAP_IS2_HK_IP6_L3_TTL_GT0]            = { 46,   1 },
	[VCAP_IS2_HK_L3_IP6_SIP]                = { 47, 128 },
	[VCAP_IS2_HK_IP6_L3_PROTO]              = { 175,  8 },
	/* OAM (TYPE=111) */
	[VCAP_IS2_HK_OAM_MEL_FLAGS]             = { 142,  7 },
	[VCAP_IS2_HK_OAM_VER]                   = { 149,  5 },
	[VCAP_IS2_HK_OAM_OPCODE]                = { 154,  8 },
	[VCAP_IS2_HK_OAM_FLAGS]                 = { 162,  8 },
	[VCAP_IS2_HK_OAM_MEPID]                 = { 170, 16 },
	[VCAP_IS2_HK_OAM_CCM_CNTS_EQ0]          = { 186,  1 },
	[VCAP_IS2_HK_OAM_IS_Y1731]              = { 187,  1 },
};

static const struct vcap_field vsc7512_vcap_is2_actions[] = {
	[VCAP_IS2_ACT_HIT_ME_ONCE]              = { 0,   1 },
	[VCAP_IS2_ACT_CPU_COPY_ENA]             = { 1,   1 },
	[VCAP_IS2_ACT_CPU_QU_NUM]               = { 2,   3 },
	[VCAP_IS2_ACT_MASK_MODE]                = { 5,   2 },
	[VCAP_IS2_ACT_MIRROR_ENA]               = { 7,   1 },
	[VCAP_IS2_ACT_LRN_DIS]                  = { 8,   1 },
	[VCAP_IS2_ACT_POLICE_ENA]               = { 9,   1 },
	[VCAP_IS2_ACT_POLICE_IDX]               = { 10,  9 },
	[VCAP_IS2_ACT_POLICE_VCAP_ONLY]         = { 19,  1 },
	[VCAP_IS2_ACT_PORT_MASK]                = { 20, 11 },
	[VCAP_IS2_ACT_REW_OP]                   = { 31,  9 },
	[VCAP_IS2_ACT_SMAC_REPLACE_ENA]         = { 40,  1 },
	[VCAP_IS2_ACT_RSV]                      = { 41,  2 },
	[VCAP_IS2_ACT_ACL_ID]                   = { 43,  6 },
	[VCAP_IS2_ACT_HIT_CNT]                  = { 49, 32 },
};

static struct vcap_props vsc7512_vcap_props[] = {
	[VCAP_ES0] = {
		.action_type_width = 0,
		.action_table = {
			[ES0_ACTION_TYPE_NORMAL] = {
				.width = 73,
				.count = 1,
			},
		},
		.target = S0,
		.keys = vsc7512_vcap_es0_keys,
		.actions = vsc7512_vcap_es0_actions,
	},
	[VCAP_IS1] = {
		.action_type_width = 0,
		.action_table = {
			[IS1_ACTION_TYPE_NORMAL] = {
				.width = 78,
				.count = 4,
			},
		},
		.target = S1,
		.keys = vsc7512_vcap_is1_keys,
		.actions = vsc7512_vcap_is1_actions,
	},
	[VCAP_IS2] = {
		.action_type_width = 1,
		.action_table = {
			[IS2_ACTION_TYPE_NORMAL] = {
				.width = 49,
				.count = 2,
			},
			[IS2_ACTION_TYPE_SMAC_SIP] = {
				.width = 6,
				.count = 4,
			},
		},
		.target = S2,
		.keys = vsc7512_vcap_is2_keys,
		.actions = vsc7512_vcap_is2_actions,
	},
};

static struct regmap *vsc7512_regmap_init(struct ocelot *ocelot,
					  struct resource *res, u32 *offset)
{
	struct felix *felix = ocelot_to_felix(ocelot);
	struct felix_spi_data *felix_spi = container_of(felix, struct
			felix_spi_data, felix);

	// Use offset instead of res, since we don't have MMIO for SPI
	*offset = res->start;
	return felix_spi->map;
}

static const struct felix_info felix_spi_info = {
	.target_io_res =		vsc7512_target_io_res,
	.port_io_res =			vsc7512_port_io_res,
	.imdio_res =			NULL,
	.regfields =			vsc7512_regfields,
	.map =				vsc7512_regmap,
	.ops =				&vsc7512_ops,
	.stats_layout =			vsc7512_stats_layout,
	.num_stats =			ARRAY_SIZE(vsc7512_stats_layout),

	.vcap =				vsc7512_vcap_props,

	// Not sure about these
	.num_mact_rows =		2048,
	.num_ports =			6,
	.num_tx_queues =		OCELOT_NUM_TC,
	.switch_pci_bar =		0,
	.imdio_pci_bar =		0,

	// Force ocelot->ptp to 0
	.ptp_caps =			NULL,

	// No need for this?
	.mdio_bus_alloc =		NULL,
	.mdio_bus_free =		NULL,
	.phylink_validate =		vsc7512_phylink_validate,
	.prevalidate_phy_mode =		vsc7512_prevalidate_phy_mode,
	.port_setup_tc =		vsc7512_port_setup_tc,
	.port_sched_speed_set =		vsc7512_sched_speed_set,
	.init_regmap =			vsc7512_regmap_init,
};

static int felix_spi_probe(struct spi_device *spi)
{
	struct dsa_switch *ds;
	struct ocelot *ocelot;
	struct felix *felix;
	int err;
	struct felix_spi_data *felix_spi;

	pr_info("felix_spi: CS: %d M: %d, BPW: %d, MaxSpeed: %d\n",
		spi->chip_select, spi->mode, spi->bits_per_word,
		spi->max_speed_hz);

	felix_spi = devm_kzalloc(&spi->dev, sizeof(struct felix_spi_data),
				 GFP_KERNEL);

	if (!felix_spi)
		return -ENOMEM;

	dev_set_drvdata(&spi->dev, felix_spi);

	spi->bits_per_word = 8;

	/* TODO: There are a couple of goals to achieve. Step 1: Get the device
	 * working in slow SPI mode with a fixed bit and byte order. Fixing this
	 * should allow devm_regmap_init_spi to be used directly, though there
	 * are penalties incurred. Specifically the bus is running much slower
	 * than it could, hindering performance.
	 *
	 * Operating at a faster SPI rate could boost performance significantly
	 * with fixed delays or padding bytes. Especially on consecutive reads.
	 *
	 * Ideally the SPI bus would be configured to run at the configured
	 * speed / fastest speed possible with consecutive reads / writes
	 * enabled, if supported. For reads, we would have to reason about the
	 * speed vs the delay time or the number of padding bytes.
	 */

	spi->max_speed_hz = 500000;
	err = spi_setup(spi);
	if (err < 0) {
		dev_err(&spi->dev, "Error %d initializing SPI\n", err);
		return err;
	}

	felix_spi->map = devm_regmap_init_spi(spi, &felix_spi_regmap_config);

	if (IS_ERR(felix_spi->map)) {
		dev_err(&spi->dev, "regmap init failed\n");
		return PTR_ERR(felix_spi->map);
	}

	err = felix_spi_init_bus(spi, felix_spi);
	if (err < 0) {
		dev_err(&spi->dev, "device init failed: %d\n", err);
		return err;
	}

	felix = &felix_spi->felix;

	ocelot = &felix->ocelot;
	ocelot->dev = &spi->dev;

	// Not sure about this
	ocelot->num_flooding_pgids = OCELOT_NUM_TC;

	felix->info = &felix_spi_info;

	ocelot->ptp = 0;

	ds = kzalloc(sizeof(*ds), GFP_KERNEL);
	if (!ds) {
		err = -ENOMEM;
		dev_err(&spi->dev, "Failed to allocate DSA switch\n");
		goto err_alloc_ds;
	}

	ds->dev = &spi->dev;
	ds->num_ports = felix->info->num_ports;
	ds->num_tx_queues = felix->info->num_tx_queues;
	ds->ops = &felix_switch_ops;
	ds->priv = ocelot;

	err = dsa_register_switch(ds);
	if (err) {
		dev_err(&spi->dev, "Failed to register DSA switch: %d\n", err);
		goto err_register_ds;
	}

	return 0;

err_register_ds:
	kfree(ds);
err_alloc_ds:
	kfree(felix);
	return err;
}

static int felix_spi_remove(struct spi_device *pdev)
{
	struct felix *felix;

	felix = spi_get_drvdata(pdev);

	return 0;
}

const struct of_device_id vsc7512_of_match[] = { { .compatible =
							   "mscc,vsc7512" },
						 {} };
MODULE_DEVICE_TABLE(of, vsc7512_of_match);

static struct spi_driver felix_vsc7512_spi_driver = {
	.driver = {
			.name = "vsc7512",
			.of_match_table = of_match_ptr(vsc7512_of_match),
		},
	.probe = felix_spi_probe,
	.remove = felix_spi_remove,
};
module_spi_driver(felix_vsc7512_spi_driver);

MODULE_DESCRIPTION("Felix Switch SPI driver");
MODULE_LICENSE("GPL v2");
