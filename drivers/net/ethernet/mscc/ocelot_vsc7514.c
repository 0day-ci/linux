// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * Microsemi Ocelot Switch driver
 *
 * Copyright (c) 2017 Microsemi Corporation
 */
#include <linux/dsa/ocelot.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_net.h>
#include <linux/netdevice.h>
#include <linux/of_mdio.h>
#include <linux/of_platform.h>
#include <linux/mfd/syscon.h>
#include <linux/skbuff.h>
#include <net/switchdev.h>

#include <soc/mscc/ocelot_vcap.h>
#include <soc/mscc/ocelot_regs.h>
#include <soc/mscc/ocelot_hsio.h>
#include "ocelot.h"

static const u32 *ocelot_regmap[TARGET_MAX] = {
	[ANA] = ocelot_ana_regmap,
	[QS] = ocelot_qs_regmap,
	[QSYS] = ocelot_qsys_regmap,
	[REW] = ocelot_rew_regmap,
	[SYS] = ocelot_sys_regmap,
	[S0] = ocelot_vcap_regmap,
	[S1] = ocelot_vcap_regmap,
	[S2] = ocelot_vcap_regmap,
	[PTP] = ocelot_ptp_regmap,
	[DEV_GMII] = ocelot_dev_gmii_regmap,
};

static const struct reg_field ocelot_regfields[REGFIELD_MAX] = {
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

static const struct ocelot_stat_layout ocelot_stats_layout[] = {
	{ .name = "rx_octets", .offset = 0x00, },
	{ .name = "rx_unicast", .offset = 0x01, },
	{ .name = "rx_multicast", .offset = 0x02, },
	{ .name = "rx_broadcast", .offset = 0x03, },
	{ .name = "rx_shorts", .offset = 0x04, },
	{ .name = "rx_fragments", .offset = 0x05, },
	{ .name = "rx_jabbers", .offset = 0x06, },
	{ .name = "rx_crc_align_errs", .offset = 0x07, },
	{ .name = "rx_sym_errs", .offset = 0x08, },
	{ .name = "rx_frames_below_65_octets", .offset = 0x09, },
	{ .name = "rx_frames_65_to_127_octets", .offset = 0x0A, },
	{ .name = "rx_frames_128_to_255_octets", .offset = 0x0B, },
	{ .name = "rx_frames_256_to_511_octets", .offset = 0x0C, },
	{ .name = "rx_frames_512_to_1023_octets", .offset = 0x0D, },
	{ .name = "rx_frames_1024_to_1526_octets", .offset = 0x0E, },
	{ .name = "rx_frames_over_1526_octets", .offset = 0x0F, },
	{ .name = "rx_pause", .offset = 0x10, },
	{ .name = "rx_control", .offset = 0x11, },
	{ .name = "rx_longs", .offset = 0x12, },
	{ .name = "rx_classified_drops", .offset = 0x13, },
	{ .name = "rx_red_prio_0", .offset = 0x14, },
	{ .name = "rx_red_prio_1", .offset = 0x15, },
	{ .name = "rx_red_prio_2", .offset = 0x16, },
	{ .name = "rx_red_prio_3", .offset = 0x17, },
	{ .name = "rx_red_prio_4", .offset = 0x18, },
	{ .name = "rx_red_prio_5", .offset = 0x19, },
	{ .name = "rx_red_prio_6", .offset = 0x1A, },
	{ .name = "rx_red_prio_7", .offset = 0x1B, },
	{ .name = "rx_yellow_prio_0", .offset = 0x1C, },
	{ .name = "rx_yellow_prio_1", .offset = 0x1D, },
	{ .name = "rx_yellow_prio_2", .offset = 0x1E, },
	{ .name = "rx_yellow_prio_3", .offset = 0x1F, },
	{ .name = "rx_yellow_prio_4", .offset = 0x20, },
	{ .name = "rx_yellow_prio_5", .offset = 0x21, },
	{ .name = "rx_yellow_prio_6", .offset = 0x22, },
	{ .name = "rx_yellow_prio_7", .offset = 0x23, },
	{ .name = "rx_green_prio_0", .offset = 0x24, },
	{ .name = "rx_green_prio_1", .offset = 0x25, },
	{ .name = "rx_green_prio_2", .offset = 0x26, },
	{ .name = "rx_green_prio_3", .offset = 0x27, },
	{ .name = "rx_green_prio_4", .offset = 0x28, },
	{ .name = "rx_green_prio_5", .offset = 0x29, },
	{ .name = "rx_green_prio_6", .offset = 0x2A, },
	{ .name = "rx_green_prio_7", .offset = 0x2B, },
	{ .name = "tx_octets", .offset = 0x40, },
	{ .name = "tx_unicast", .offset = 0x41, },
	{ .name = "tx_multicast", .offset = 0x42, },
	{ .name = "tx_broadcast", .offset = 0x43, },
	{ .name = "tx_collision", .offset = 0x44, },
	{ .name = "tx_drops", .offset = 0x45, },
	{ .name = "tx_pause", .offset = 0x46, },
	{ .name = "tx_frames_below_65_octets", .offset = 0x47, },
	{ .name = "tx_frames_65_to_127_octets", .offset = 0x48, },
	{ .name = "tx_frames_128_255_octets", .offset = 0x49, },
	{ .name = "tx_frames_256_511_octets", .offset = 0x4A, },
	{ .name = "tx_frames_512_1023_octets", .offset = 0x4B, },
	{ .name = "tx_frames_1024_1526_octets", .offset = 0x4C, },
	{ .name = "tx_frames_over_1526_octets", .offset = 0x4D, },
	{ .name = "tx_yellow_prio_0", .offset = 0x4E, },
	{ .name = "tx_yellow_prio_1", .offset = 0x4F, },
	{ .name = "tx_yellow_prio_2", .offset = 0x50, },
	{ .name = "tx_yellow_prio_3", .offset = 0x51, },
	{ .name = "tx_yellow_prio_4", .offset = 0x52, },
	{ .name = "tx_yellow_prio_5", .offset = 0x53, },
	{ .name = "tx_yellow_prio_6", .offset = 0x54, },
	{ .name = "tx_yellow_prio_7", .offset = 0x55, },
	{ .name = "tx_green_prio_0", .offset = 0x56, },
	{ .name = "tx_green_prio_1", .offset = 0x57, },
	{ .name = "tx_green_prio_2", .offset = 0x58, },
	{ .name = "tx_green_prio_3", .offset = 0x59, },
	{ .name = "tx_green_prio_4", .offset = 0x5A, },
	{ .name = "tx_green_prio_5", .offset = 0x5B, },
	{ .name = "tx_green_prio_6", .offset = 0x5C, },
	{ .name = "tx_green_prio_7", .offset = 0x5D, },
	{ .name = "tx_aged", .offset = 0x5E, },
	{ .name = "drop_local", .offset = 0x80, },
	{ .name = "drop_tail", .offset = 0x81, },
	{ .name = "drop_yellow_prio_0", .offset = 0x82, },
	{ .name = "drop_yellow_prio_1", .offset = 0x83, },
	{ .name = "drop_yellow_prio_2", .offset = 0x84, },
	{ .name = "drop_yellow_prio_3", .offset = 0x85, },
	{ .name = "drop_yellow_prio_4", .offset = 0x86, },
	{ .name = "drop_yellow_prio_5", .offset = 0x87, },
	{ .name = "drop_yellow_prio_6", .offset = 0x88, },
	{ .name = "drop_yellow_prio_7", .offset = 0x89, },
	{ .name = "drop_green_prio_0", .offset = 0x8A, },
	{ .name = "drop_green_prio_1", .offset = 0x8B, },
	{ .name = "drop_green_prio_2", .offset = 0x8C, },
	{ .name = "drop_green_prio_3", .offset = 0x8D, },
	{ .name = "drop_green_prio_4", .offset = 0x8E, },
	{ .name = "drop_green_prio_5", .offset = 0x8F, },
	{ .name = "drop_green_prio_6", .offset = 0x90, },
	{ .name = "drop_green_prio_7", .offset = 0x91, },
};

static void ocelot_pll5_init(struct ocelot *ocelot)
{
	/* Configure PLL5. This will need a proper CCF driver
	 * The values are coming from the VTSS API for Ocelot
	 */
	regmap_write(ocelot->targets[HSIO], HSIO_PLL5G_CFG4,
		     HSIO_PLL5G_CFG4_IB_CTRL(0x7600) |
		     HSIO_PLL5G_CFG4_IB_BIAS_CTRL(0x8));
	regmap_write(ocelot->targets[HSIO], HSIO_PLL5G_CFG0,
		     HSIO_PLL5G_CFG0_CORE_CLK_DIV(0x11) |
		     HSIO_PLL5G_CFG0_CPU_CLK_DIV(2) |
		     HSIO_PLL5G_CFG0_ENA_BIAS |
		     HSIO_PLL5G_CFG0_ENA_VCO_BUF |
		     HSIO_PLL5G_CFG0_ENA_CP1 |
		     HSIO_PLL5G_CFG0_SELCPI(2) |
		     HSIO_PLL5G_CFG0_LOOP_BW_RES(0xe) |
		     HSIO_PLL5G_CFG0_SELBGV820(4) |
		     HSIO_PLL5G_CFG0_DIV4 |
		     HSIO_PLL5G_CFG0_ENA_CLKTREE |
		     HSIO_PLL5G_CFG0_ENA_LANE);
	regmap_write(ocelot->targets[HSIO], HSIO_PLL5G_CFG2,
		     HSIO_PLL5G_CFG2_EN_RESET_FRQ_DET |
		     HSIO_PLL5G_CFG2_EN_RESET_OVERRUN |
		     HSIO_PLL5G_CFG2_GAIN_TEST(0x8) |
		     HSIO_PLL5G_CFG2_ENA_AMPCTRL |
		     HSIO_PLL5G_CFG2_PWD_AMPCTRL_N |
		     HSIO_PLL5G_CFG2_AMPC_SEL(0x10));
}

static int ocelot_chip_init(struct ocelot *ocelot, const struct ocelot_ops *ops)
{
	int ret;

	ocelot->map = ocelot_regmap;
	ocelot->stats_layout = ocelot_stats_layout;
	ocelot->num_stats = ARRAY_SIZE(ocelot_stats_layout);
	ocelot->num_mact_rows = 1024;
	ocelot->ops = ops;

	ret = ocelot_regfields_init(ocelot, ocelot_regfields);
	if (ret)
		return ret;

	ocelot_pll5_init(ocelot);

	eth_random_addr(ocelot->base_mac);
	ocelot->base_mac[5] &= 0xf0;

	return 0;
}

static irqreturn_t ocelot_xtr_irq_handler(int irq, void *arg)
{
	struct ocelot *ocelot = arg;
	int grp = 0, err;

	while (ocelot_read(ocelot, QS_XTR_DATA_PRESENT) & BIT(grp)) {
		struct sk_buff *skb;

		err = ocelot_xtr_poll_frame(ocelot, grp, &skb);
		if (err)
			goto out;

		skb->dev->stats.rx_bytes += skb->len;
		skb->dev->stats.rx_packets++;

		if (!skb_defer_rx_timestamp(skb))
			netif_rx(skb);
	}

out:
	if (err < 0)
		ocelot_drain_cpu_queue(ocelot, 0);

	return IRQ_HANDLED;
}

static irqreturn_t ocelot_ptp_rdy_irq_handler(int irq, void *arg)
{
	struct ocelot *ocelot = arg;

	ocelot_get_txtstamp(ocelot);

	return IRQ_HANDLED;
}

static const struct of_device_id mscc_ocelot_match[] = {
	{ .compatible = "mscc,vsc7514-switch" },
	{ }
};
MODULE_DEVICE_TABLE(of, mscc_ocelot_match);

static int ocelot_reset(struct ocelot *ocelot)
{
	int retries = 100;
	u32 val;

	regmap_field_write(ocelot->regfields[SYS_RESET_CFG_MEM_INIT], 1);
	regmap_field_write(ocelot->regfields[SYS_RESET_CFG_MEM_ENA], 1);

	do {
		msleep(1);
		regmap_field_read(ocelot->regfields[SYS_RESET_CFG_MEM_INIT],
				  &val);
	} while (val && --retries);

	if (!retries)
		return -ETIMEDOUT;

	regmap_field_write(ocelot->regfields[SYS_RESET_CFG_MEM_ENA], 1);
	regmap_field_write(ocelot->regfields[SYS_RESET_CFG_CORE_ENA], 1);

	return 0;
}

/* Watermark encode
 * Bit 8:   Unit; 0:1, 1:16
 * Bit 7-0: Value to be multiplied with unit
 */
static u16 ocelot_wm_enc(u16 value)
{
	WARN_ON(value >= 16 * BIT(8));

	if (value >= BIT(8))
		return BIT(8) | (value / 16);

	return value;
}

static u16 ocelot_wm_dec(u16 wm)
{
	if (wm & BIT(8))
		return (wm & GENMASK(7, 0)) * 16;

	return wm;
}

static void ocelot_wm_stat(u32 val, u32 *inuse, u32 *maxuse)
{
	*inuse = (val & GENMASK(23, 12)) >> 12;
	*maxuse = val & GENMASK(11, 0);
}

static const struct ocelot_ops ocelot_ops = {
	.reset			= ocelot_reset,
	.wm_enc			= ocelot_wm_enc,
	.wm_dec			= ocelot_wm_dec,
	.wm_stat		= ocelot_wm_stat,
	.port_to_netdev		= ocelot_port_to_netdev,
	.netdev_to_port		= ocelot_netdev_to_port,
};

static const struct vcap_field vsc7514_vcap_es0_keys[] = {
	[VCAP_ES0_EGR_PORT]			= {  0,  4},
	[VCAP_ES0_IGR_PORT]			= {  4,  4},
	[VCAP_ES0_RSV]				= {  8,  2},
	[VCAP_ES0_L2_MC]			= { 10,  1},
	[VCAP_ES0_L2_BC]			= { 11,  1},
	[VCAP_ES0_VID]				= { 12, 12},
	[VCAP_ES0_DP]				= { 24,  1},
	[VCAP_ES0_PCP]				= { 25,  3},
};

static const struct vcap_field vsc7514_vcap_es0_actions[] = {
	[VCAP_ES0_ACT_PUSH_OUTER_TAG]		= {  0,  2},
	[VCAP_ES0_ACT_PUSH_INNER_TAG]		= {  2,  1},
	[VCAP_ES0_ACT_TAG_A_TPID_SEL]		= {  3,  2},
	[VCAP_ES0_ACT_TAG_A_VID_SEL]		= {  5,  1},
	[VCAP_ES0_ACT_TAG_A_PCP_SEL]		= {  6,  2},
	[VCAP_ES0_ACT_TAG_A_DEI_SEL]		= {  8,  2},
	[VCAP_ES0_ACT_TAG_B_TPID_SEL]		= { 10,  2},
	[VCAP_ES0_ACT_TAG_B_VID_SEL]		= { 12,  1},
	[VCAP_ES0_ACT_TAG_B_PCP_SEL]		= { 13,  2},
	[VCAP_ES0_ACT_TAG_B_DEI_SEL]		= { 15,  2},
	[VCAP_ES0_ACT_VID_A_VAL]		= { 17, 12},
	[VCAP_ES0_ACT_PCP_A_VAL]		= { 29,  3},
	[VCAP_ES0_ACT_DEI_A_VAL]		= { 32,  1},
	[VCAP_ES0_ACT_VID_B_VAL]		= { 33, 12},
	[VCAP_ES0_ACT_PCP_B_VAL]		= { 45,  3},
	[VCAP_ES0_ACT_DEI_B_VAL]		= { 48,  1},
	[VCAP_ES0_ACT_RSV]			= { 49, 24},
	[VCAP_ES0_ACT_HIT_STICKY]		= { 73,  1},
};

static const struct vcap_field vsc7514_vcap_is1_keys[] = {
	[VCAP_IS1_HK_TYPE]			= {  0,   1},
	[VCAP_IS1_HK_LOOKUP]			= {  1,   2},
	[VCAP_IS1_HK_IGR_PORT_MASK]		= {  3,  12},
	[VCAP_IS1_HK_RSV]			= { 15,   9},
	[VCAP_IS1_HK_OAM_Y1731]			= { 24,   1},
	[VCAP_IS1_HK_L2_MC]			= { 25,   1},
	[VCAP_IS1_HK_L2_BC]			= { 26,   1},
	[VCAP_IS1_HK_IP_MC]			= { 27,   1},
	[VCAP_IS1_HK_VLAN_TAGGED]		= { 28,   1},
	[VCAP_IS1_HK_VLAN_DBL_TAGGED]		= { 29,   1},
	[VCAP_IS1_HK_TPID]			= { 30,   1},
	[VCAP_IS1_HK_VID]			= { 31,  12},
	[VCAP_IS1_HK_DEI]			= { 43,   1},
	[VCAP_IS1_HK_PCP]			= { 44,   3},
	/* Specific Fields for IS1 Half Key S1_NORMAL */
	[VCAP_IS1_HK_L2_SMAC]			= { 47,  48},
	[VCAP_IS1_HK_ETYPE_LEN]			= { 95,   1},
	[VCAP_IS1_HK_ETYPE]			= { 96,  16},
	[VCAP_IS1_HK_IP_SNAP]			= {112,   1},
	[VCAP_IS1_HK_IP4]			= {113,   1},
	/* Layer-3 Information */
	[VCAP_IS1_HK_L3_FRAGMENT]		= {114,   1},
	[VCAP_IS1_HK_L3_FRAG_OFS_GT0]		= {115,   1},
	[VCAP_IS1_HK_L3_OPTIONS]		= {116,   1},
	[VCAP_IS1_HK_L3_DSCP]			= {117,   6},
	[VCAP_IS1_HK_L3_IP4_SIP]		= {123,  32},
	/* Layer-4 Information */
	[VCAP_IS1_HK_TCP_UDP]			= {155,   1},
	[VCAP_IS1_HK_TCP]			= {156,   1},
	[VCAP_IS1_HK_L4_SPORT]			= {157,  16},
	[VCAP_IS1_HK_L4_RNG]			= {173,   8},
	/* Specific Fields for IS1 Half Key S1_5TUPLE_IP4 */
	[VCAP_IS1_HK_IP4_INNER_TPID]            = { 47,   1},
	[VCAP_IS1_HK_IP4_INNER_VID]		= { 48,  12},
	[VCAP_IS1_HK_IP4_INNER_DEI]		= { 60,   1},
	[VCAP_IS1_HK_IP4_INNER_PCP]		= { 61,   3},
	[VCAP_IS1_HK_IP4_IP4]			= { 64,   1},
	[VCAP_IS1_HK_IP4_L3_FRAGMENT]		= { 65,   1},
	[VCAP_IS1_HK_IP4_L3_FRAG_OFS_GT0]	= { 66,   1},
	[VCAP_IS1_HK_IP4_L3_OPTIONS]		= { 67,   1},
	[VCAP_IS1_HK_IP4_L3_DSCP]		= { 68,   6},
	[VCAP_IS1_HK_IP4_L3_IP4_DIP]		= { 74,  32},
	[VCAP_IS1_HK_IP4_L3_IP4_SIP]		= {106,  32},
	[VCAP_IS1_HK_IP4_L3_PROTO]		= {138,   8},
	[VCAP_IS1_HK_IP4_TCP_UDP]		= {146,   1},
	[VCAP_IS1_HK_IP4_TCP]			= {147,   1},
	[VCAP_IS1_HK_IP4_L4_RNG]		= {148,   8},
	[VCAP_IS1_HK_IP4_IP_PAYLOAD_S1_5TUPLE]	= {156,  32},
};

static const struct vcap_field vsc7514_vcap_is1_actions[] = {
	[VCAP_IS1_ACT_DSCP_ENA]			= {  0,  1},
	[VCAP_IS1_ACT_DSCP_VAL]			= {  1,  6},
	[VCAP_IS1_ACT_QOS_ENA]			= {  7,  1},
	[VCAP_IS1_ACT_QOS_VAL]			= {  8,  3},
	[VCAP_IS1_ACT_DP_ENA]			= { 11,  1},
	[VCAP_IS1_ACT_DP_VAL]			= { 12,  1},
	[VCAP_IS1_ACT_PAG_OVERRIDE_MASK]	= { 13,  8},
	[VCAP_IS1_ACT_PAG_VAL]			= { 21,  8},
	[VCAP_IS1_ACT_RSV]			= { 29,  9},
	/* The fields below are incorrectly shifted by 2 in the manual */
	[VCAP_IS1_ACT_VID_REPLACE_ENA]		= { 38,  1},
	[VCAP_IS1_ACT_VID_ADD_VAL]		= { 39, 12},
	[VCAP_IS1_ACT_FID_SEL]			= { 51,  2},
	[VCAP_IS1_ACT_FID_VAL]			= { 53, 13},
	[VCAP_IS1_ACT_PCP_DEI_ENA]		= { 66,  1},
	[VCAP_IS1_ACT_PCP_VAL]			= { 67,  3},
	[VCAP_IS1_ACT_DEI_VAL]			= { 70,  1},
	[VCAP_IS1_ACT_VLAN_POP_CNT_ENA]		= { 71,  1},
	[VCAP_IS1_ACT_VLAN_POP_CNT]		= { 72,  2},
	[VCAP_IS1_ACT_CUSTOM_ACE_TYPE_ENA]	= { 74,  4},
	[VCAP_IS1_ACT_HIT_STICKY]		= { 78,  1},
};

static const struct vcap_field vsc7514_vcap_is2_keys[] = {
	/* Common: 46 bits */
	[VCAP_IS2_TYPE]				= {  0,   4},
	[VCAP_IS2_HK_FIRST]			= {  4,   1},
	[VCAP_IS2_HK_PAG]			= {  5,   8},
	[VCAP_IS2_HK_IGR_PORT_MASK]		= { 13,  12},
	[VCAP_IS2_HK_RSV2]			= { 25,   1},
	[VCAP_IS2_HK_HOST_MATCH]		= { 26,   1},
	[VCAP_IS2_HK_L2_MC]			= { 27,   1},
	[VCAP_IS2_HK_L2_BC]			= { 28,   1},
	[VCAP_IS2_HK_VLAN_TAGGED]		= { 29,   1},
	[VCAP_IS2_HK_VID]			= { 30,  12},
	[VCAP_IS2_HK_DEI]			= { 42,   1},
	[VCAP_IS2_HK_PCP]			= { 43,   3},
	/* MAC_ETYPE / MAC_LLC / MAC_SNAP / OAM common */
	[VCAP_IS2_HK_L2_DMAC]			= { 46,  48},
	[VCAP_IS2_HK_L2_SMAC]			= { 94,  48},
	/* MAC_ETYPE (TYPE=000) */
	[VCAP_IS2_HK_MAC_ETYPE_ETYPE]		= {142,  16},
	[VCAP_IS2_HK_MAC_ETYPE_L2_PAYLOAD0]	= {158,  16},
	[VCAP_IS2_HK_MAC_ETYPE_L2_PAYLOAD1]	= {174,   8},
	[VCAP_IS2_HK_MAC_ETYPE_L2_PAYLOAD2]	= {182,   3},
	/* MAC_LLC (TYPE=001) */
	[VCAP_IS2_HK_MAC_LLC_L2_LLC]		= {142,  40},
	/* MAC_SNAP (TYPE=010) */
	[VCAP_IS2_HK_MAC_SNAP_L2_SNAP]		= {142,  40},
	/* MAC_ARP (TYPE=011) */
	[VCAP_IS2_HK_MAC_ARP_SMAC]		= { 46,  48},
	[VCAP_IS2_HK_MAC_ARP_ADDR_SPACE_OK]	= { 94,   1},
	[VCAP_IS2_HK_MAC_ARP_PROTO_SPACE_OK]	= { 95,   1},
	[VCAP_IS2_HK_MAC_ARP_LEN_OK]		= { 96,   1},
	[VCAP_IS2_HK_MAC_ARP_TARGET_MATCH]	= { 97,   1},
	[VCAP_IS2_HK_MAC_ARP_SENDER_MATCH]	= { 98,   1},
	[VCAP_IS2_HK_MAC_ARP_OPCODE_UNKNOWN]	= { 99,   1},
	[VCAP_IS2_HK_MAC_ARP_OPCODE]		= {100,   2},
	[VCAP_IS2_HK_MAC_ARP_L3_IP4_DIP]	= {102,  32},
	[VCAP_IS2_HK_MAC_ARP_L3_IP4_SIP]	= {134,  32},
	[VCAP_IS2_HK_MAC_ARP_DIP_EQ_SIP]	= {166,   1},
	/* IP4_TCP_UDP / IP4_OTHER common */
	[VCAP_IS2_HK_IP4]			= { 46,   1},
	[VCAP_IS2_HK_L3_FRAGMENT]		= { 47,   1},
	[VCAP_IS2_HK_L3_FRAG_OFS_GT0]		= { 48,   1},
	[VCAP_IS2_HK_L3_OPTIONS]		= { 49,   1},
	[VCAP_IS2_HK_IP4_L3_TTL_GT0]		= { 50,   1},
	[VCAP_IS2_HK_L3_TOS]			= { 51,   8},
	[VCAP_IS2_HK_L3_IP4_DIP]		= { 59,  32},
	[VCAP_IS2_HK_L3_IP4_SIP]		= { 91,  32},
	[VCAP_IS2_HK_DIP_EQ_SIP]		= {123,   1},
	/* IP4_TCP_UDP (TYPE=100) */
	[VCAP_IS2_HK_TCP]			= {124,   1},
	[VCAP_IS2_HK_L4_DPORT]			= {125,  16},
	[VCAP_IS2_HK_L4_SPORT]			= {141,  16},
	[VCAP_IS2_HK_L4_RNG]			= {157,   8},
	[VCAP_IS2_HK_L4_SPORT_EQ_DPORT]		= {165,   1},
	[VCAP_IS2_HK_L4_SEQUENCE_EQ0]		= {166,   1},
	[VCAP_IS2_HK_L4_FIN]			= {167,   1},
	[VCAP_IS2_HK_L4_SYN]			= {168,   1},
	[VCAP_IS2_HK_L4_RST]			= {169,   1},
	[VCAP_IS2_HK_L4_PSH]			= {170,   1},
	[VCAP_IS2_HK_L4_ACK]			= {171,   1},
	[VCAP_IS2_HK_L4_URG]			= {172,   1},
	[VCAP_IS2_HK_L4_1588_DOM]		= {173,   8},
	[VCAP_IS2_HK_L4_1588_VER]		= {181,   4},
	/* IP4_OTHER (TYPE=101) */
	[VCAP_IS2_HK_IP4_L3_PROTO]		= {124,   8},
	[VCAP_IS2_HK_L3_PAYLOAD]		= {132,  56},
	/* IP6_STD (TYPE=110) */
	[VCAP_IS2_HK_IP6_L3_TTL_GT0]		= { 46,   1},
	[VCAP_IS2_HK_L3_IP6_SIP]		= { 47, 128},
	[VCAP_IS2_HK_IP6_L3_PROTO]		= {175,   8},
	/* OAM (TYPE=111) */
	[VCAP_IS2_HK_OAM_MEL_FLAGS]		= {142,   7},
	[VCAP_IS2_HK_OAM_VER]			= {149,   5},
	[VCAP_IS2_HK_OAM_OPCODE]		= {154,   8},
	[VCAP_IS2_HK_OAM_FLAGS]			= {162,   8},
	[VCAP_IS2_HK_OAM_MEPID]			= {170,  16},
	[VCAP_IS2_HK_OAM_CCM_CNTS_EQ0]		= {186,   1},
	[VCAP_IS2_HK_OAM_IS_Y1731]		= {187,   1},
};

static const struct vcap_field vsc7514_vcap_is2_actions[] = {
	[VCAP_IS2_ACT_HIT_ME_ONCE]		= {  0,  1},
	[VCAP_IS2_ACT_CPU_COPY_ENA]		= {  1,  1},
	[VCAP_IS2_ACT_CPU_QU_NUM]		= {  2,  3},
	[VCAP_IS2_ACT_MASK_MODE]		= {  5,  2},
	[VCAP_IS2_ACT_MIRROR_ENA]		= {  7,  1},
	[VCAP_IS2_ACT_LRN_DIS]			= {  8,  1},
	[VCAP_IS2_ACT_POLICE_ENA]		= {  9,  1},
	[VCAP_IS2_ACT_POLICE_IDX]		= { 10,  9},
	[VCAP_IS2_ACT_POLICE_VCAP_ONLY]		= { 19,  1},
	[VCAP_IS2_ACT_PORT_MASK]		= { 20, 11},
	[VCAP_IS2_ACT_REW_OP]			= { 31,  9},
	[VCAP_IS2_ACT_SMAC_REPLACE_ENA]		= { 40,  1},
	[VCAP_IS2_ACT_RSV]			= { 41,  2},
	[VCAP_IS2_ACT_ACL_ID]			= { 43,  6},
	[VCAP_IS2_ACT_HIT_CNT]			= { 49, 32},
};

static struct vcap_props vsc7514_vcap_props[] = {
	[VCAP_ES0] = {
		.action_type_width = 0,
		.action_table = {
			[ES0_ACTION_TYPE_NORMAL] = {
				.width = 73, /* HIT_STICKY not included */
				.count = 1,
			},
		},
		.target = S0,
		.keys = vsc7514_vcap_es0_keys,
		.actions = vsc7514_vcap_es0_actions,
	},
	[VCAP_IS1] = {
		.action_type_width = 0,
		.action_table = {
			[IS1_ACTION_TYPE_NORMAL] = {
				.width = 78, /* HIT_STICKY not included */
				.count = 4,
			},
		},
		.target = S1,
		.keys = vsc7514_vcap_is1_keys,
		.actions = vsc7514_vcap_is1_actions,
	},
	[VCAP_IS2] = {
		.action_type_width = 1,
		.action_table = {
			[IS2_ACTION_TYPE_NORMAL] = {
				.width = 49,
				.count = 2
			},
			[IS2_ACTION_TYPE_SMAC_SIP] = {
				.width = 6,
				.count = 4
			},
		},
		.target = S2,
		.keys = vsc7514_vcap_is2_keys,
		.actions = vsc7514_vcap_is2_actions,
	},
};

static struct ptp_clock_info ocelot_ptp_clock_info = {
	.owner		= THIS_MODULE,
	.name		= "ocelot ptp",
	.max_adj	= 0x7fffffff,
	.n_alarm	= 0,
	.n_ext_ts	= 0,
	.n_per_out	= OCELOT_PTP_PINS_NUM,
	.n_pins		= OCELOT_PTP_PINS_NUM,
	.pps		= 0,
	.gettime64	= ocelot_ptp_gettime64,
	.settime64	= ocelot_ptp_settime64,
	.adjtime	= ocelot_ptp_adjtime,
	.adjfine	= ocelot_ptp_adjfine,
	.verify		= ocelot_ptp_verify,
	.enable		= ocelot_ptp_enable,
};

static void mscc_ocelot_teardown_devlink_ports(struct ocelot *ocelot)
{
	int port;

	for (port = 0; port < ocelot->num_phys_ports; port++)
		ocelot_port_devlink_teardown(ocelot, port);
}

static void mscc_ocelot_release_ports(struct ocelot *ocelot)
{
	int port;

	for (port = 0; port < ocelot->num_phys_ports; port++) {
		struct ocelot_port *ocelot_port;

		ocelot_port = ocelot->ports[port];
		if (!ocelot_port)
			continue;

		ocelot_deinit_port(ocelot, port);
		ocelot_release_port(ocelot_port);
	}
}

static int mscc_ocelot_init_ports(struct platform_device *pdev,
				  struct device_node *ports)
{
	struct ocelot *ocelot = platform_get_drvdata(pdev);
	u32 devlink_ports_registered = 0;
	struct device_node *portnp;
	int port, err;
	u32 reg;

	ocelot->ports = devm_kcalloc(ocelot->dev, ocelot->num_phys_ports,
				     sizeof(struct ocelot_port *), GFP_KERNEL);
	if (!ocelot->ports)
		return -ENOMEM;

	ocelot->devlink_ports = devm_kcalloc(ocelot->dev,
					     ocelot->num_phys_ports,
					     sizeof(*ocelot->devlink_ports),
					     GFP_KERNEL);
	if (!ocelot->devlink_ports)
		return -ENOMEM;

	for_each_available_child_of_node(ports, portnp) {
		struct ocelot_port_private *priv;
		struct ocelot_port *ocelot_port;
		struct device_node *phy_node;
		struct devlink_port *dlp;
		phy_interface_t phy_mode;
		struct phy_device *phy;
		struct regmap *target;
		struct resource *res;
		struct phy *serdes;
		char res_name[8];

		if (of_property_read_u32(portnp, "reg", &reg))
			continue;

		port = reg;
		if (port < 0 || port >= ocelot->num_phys_ports) {
			dev_err(ocelot->dev,
				"invalid port number: %d >= %d\n", port,
				ocelot->num_phys_ports);
			continue;
		}

		snprintf(res_name, sizeof(res_name), "port%d", port);

		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						   res_name);
		target = ocelot_regmap_init(ocelot, res);
		if (IS_ERR(target)) {
			err = PTR_ERR(target);
			goto out_teardown;
		}

		phy_node = of_parse_phandle(portnp, "phy-handle", 0);
		if (!phy_node)
			continue;

		phy = of_phy_find_device(phy_node);
		of_node_put(phy_node);
		if (!phy)
			continue;

		err = ocelot_port_devlink_init(ocelot, port,
					       DEVLINK_PORT_FLAVOUR_PHYSICAL);
		if (err) {
			of_node_put(portnp);
			goto out_teardown;
		}
		devlink_ports_registered |= BIT(port);

		err = ocelot_probe_port(ocelot, port, target, phy);
		if (err) {
			of_node_put(portnp);
			goto out_teardown;
		}

		ocelot_port = ocelot->ports[port];
		priv = container_of(ocelot_port, struct ocelot_port_private,
				    port);
		dlp = &ocelot->devlink_ports[port];
		devlink_port_type_eth_set(dlp, priv->dev);

		of_get_phy_mode(portnp, &phy_mode);

		ocelot_port->phy_mode = phy_mode;

		switch (ocelot_port->phy_mode) {
		case PHY_INTERFACE_MODE_NA:
			continue;
		case PHY_INTERFACE_MODE_SGMII:
			break;
		case PHY_INTERFACE_MODE_QSGMII:
			/* Ensure clock signals and speed is set on all
			 * QSGMII links
			 */
			ocelot_port_writel(ocelot_port,
					   DEV_CLOCK_CFG_LINK_SPEED
					   (OCELOT_SPEED_1000),
					   DEV_CLOCK_CFG);
			break;
		default:
			dev_err(ocelot->dev,
				"invalid phy mode for port%d, (Q)SGMII only\n",
				port);
			of_node_put(portnp);
			err = -EINVAL;
			goto out_teardown;
		}

		serdes = devm_of_phy_get(ocelot->dev, portnp, NULL);
		if (IS_ERR(serdes)) {
			err = PTR_ERR(serdes);
			if (err == -EPROBE_DEFER)
				dev_dbg(ocelot->dev, "deferring probe\n");
			else
				dev_err(ocelot->dev,
					"missing SerDes phys for port%d\n",
					port);

			of_node_put(portnp);
			goto out_teardown;
		}

		priv->serdes = serdes;
	}

	/* Initialize unused devlink ports at the end */
	for (port = 0; port < ocelot->num_phys_ports; port++) {
		if (devlink_ports_registered & BIT(port))
			continue;

		err = ocelot_port_devlink_init(ocelot, port,
					       DEVLINK_PORT_FLAVOUR_UNUSED);
		if (err)
			goto out_teardown;

		devlink_ports_registered |= BIT(port);
	}

	return 0;

out_teardown:
	/* Unregister the network interfaces */
	mscc_ocelot_release_ports(ocelot);
	/* Tear down devlink ports for the registered network interfaces */
	for (port = 0; port < ocelot->num_phys_ports; port++) {
		if (devlink_ports_registered & BIT(port))
			ocelot_port_devlink_teardown(ocelot, port);
	}
	return err;
}

static int mscc_ocelot_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	int err, irq_xtr, irq_ptp_rdy;
	struct device_node *ports;
	struct devlink *devlink;
	struct ocelot *ocelot;
	struct regmap *hsio;
	unsigned int i;

	struct {
		enum ocelot_target id;
		char *name;
		u8 optional:1;
	} io_target[] = {
		{ SYS, "sys" },
		{ REW, "rew" },
		{ QSYS, "qsys" },
		{ ANA, "ana" },
		{ QS, "qs" },
		{ S0, "s0" },
		{ S1, "s1" },
		{ S2, "s2" },
		{ PTP, "ptp", 1 },
	};

	if (!np && !pdev->dev.platform_data)
		return -ENODEV;

	devlink = devlink_alloc(&ocelot_devlink_ops, sizeof(*ocelot));
	if (!devlink)
		return -ENOMEM;

	ocelot = devlink_priv(devlink);
	ocelot->devlink = priv_to_devlink(ocelot);
	platform_set_drvdata(pdev, ocelot);
	ocelot->dev = &pdev->dev;

	for (i = 0; i < ARRAY_SIZE(io_target); i++) {
		struct regmap *target;
		struct resource *res;

		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						   io_target[i].name);

		target = ocelot_regmap_init(ocelot, res);
		if (IS_ERR(target)) {
			if (io_target[i].optional) {
				ocelot->targets[io_target[i].id] = NULL;
				continue;
			}
			err = PTR_ERR(target);
			goto out_free_devlink;
		}

		ocelot->targets[io_target[i].id] = target;
	}

	hsio = syscon_regmap_lookup_by_compatible("mscc,ocelot-hsio");
	if (IS_ERR(hsio)) {
		dev_err(&pdev->dev, "missing hsio syscon\n");
		err = PTR_ERR(hsio);
		goto out_free_devlink;
	}

	ocelot->targets[HSIO] = hsio;

	err = ocelot_chip_init(ocelot, &ocelot_ops);
	if (err)
		goto out_free_devlink;

	irq_xtr = platform_get_irq_byname(pdev, "xtr");
	if (irq_xtr < 0) {
		err = irq_xtr;
		goto out_free_devlink;
	}

	err = devm_request_threaded_irq(&pdev->dev, irq_xtr, NULL,
					ocelot_xtr_irq_handler, IRQF_ONESHOT,
					"frame extraction", ocelot);
	if (err)
		goto out_free_devlink;

	irq_ptp_rdy = platform_get_irq_byname(pdev, "ptp_rdy");
	if (irq_ptp_rdy > 0 && ocelot->targets[PTP]) {
		err = devm_request_threaded_irq(&pdev->dev, irq_ptp_rdy, NULL,
						ocelot_ptp_rdy_irq_handler,
						IRQF_ONESHOT, "ptp ready",
						ocelot);
		if (err)
			goto out_free_devlink;

		/* Both the PTP interrupt and the PTP bank are available */
		ocelot->ptp = 1;
	}

	ports = of_get_child_by_name(np, "ethernet-ports");
	if (!ports) {
		dev_err(ocelot->dev, "no ethernet-ports child node found\n");
		err = -ENODEV;
		goto out_free_devlink;
	}

	ocelot->num_phys_ports = of_get_child_count(ports);
	ocelot->num_flooding_pgids = 1;

	ocelot->vcap = vsc7514_vcap_props;
	ocelot->npi = -1;

	err = ocelot_init(ocelot);
	if (err)
		goto out_put_ports;

	err = devlink_register(devlink, ocelot->dev);
	if (err)
		goto out_ocelot_deinit;

	err = mscc_ocelot_init_ports(pdev, ports);
	if (err)
		goto out_ocelot_devlink_unregister;

	err = ocelot_devlink_sb_register(ocelot);
	if (err)
		goto out_ocelot_release_ports;

	if (ocelot->ptp) {
		err = ocelot_init_timestamp(ocelot, &ocelot_ptp_clock_info);
		if (err) {
			dev_err(ocelot->dev,
				"Timestamp initialization failed\n");
			ocelot->ptp = 0;
		}
	}

	register_netdevice_notifier(&ocelot_netdevice_nb);
	register_switchdev_notifier(&ocelot_switchdev_nb);
	register_switchdev_blocking_notifier(&ocelot_switchdev_blocking_nb);

	of_node_put(ports);

	dev_info(&pdev->dev, "Ocelot switch probed\n");

	return 0;

out_ocelot_release_ports:
	mscc_ocelot_release_ports(ocelot);
	mscc_ocelot_teardown_devlink_ports(ocelot);
out_ocelot_devlink_unregister:
	devlink_unregister(devlink);
out_ocelot_deinit:
	ocelot_deinit(ocelot);
out_put_ports:
	of_node_put(ports);
out_free_devlink:
	devlink_free(devlink);
	return err;
}

static int mscc_ocelot_remove(struct platform_device *pdev)
{
	struct ocelot *ocelot = platform_get_drvdata(pdev);

	ocelot_deinit_timestamp(ocelot);
	ocelot_devlink_sb_unregister(ocelot);
	mscc_ocelot_release_ports(ocelot);
	mscc_ocelot_teardown_devlink_ports(ocelot);
	devlink_unregister(ocelot->devlink);
	ocelot_deinit(ocelot);
	unregister_switchdev_blocking_notifier(&ocelot_switchdev_blocking_nb);
	unregister_switchdev_notifier(&ocelot_switchdev_nb);
	unregister_netdevice_notifier(&ocelot_netdevice_nb);
	devlink_free(ocelot->devlink);

	return 0;
}

static struct platform_driver mscc_ocelot_driver = {
	.probe = mscc_ocelot_probe,
	.remove = mscc_ocelot_remove,
	.driver = {
		.name = "ocelot-switch",
		.of_match_table = mscc_ocelot_match,
	},
};

module_platform_driver(mscc_ocelot_driver);

MODULE_DESCRIPTION("Microsemi Ocelot switch driver");
MODULE_AUTHOR("Alexandre Belloni <alexandre.belloni@bootlin.com>");
MODULE_LICENSE("Dual MIT/GPL");
