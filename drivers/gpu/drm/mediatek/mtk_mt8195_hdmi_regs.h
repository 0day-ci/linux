/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#ifndef _MTK_HDMI_REGS_H
#define _MTK_HDMI_REGS_H

#define AIF_HEADER (0x00FFFFFF << 0)
#define AIF_PKT00 (0xFFFFFFFF << 0)
#define AIF_PKT01 (0x00FFFFFF << 0)
#define AIF_PKT02 (0xFFFFFFFF << 0)
#define AIF_PKT03 (0x00FFFFFF << 0)
#define AIP_CTRL 0x400
#define AIP_CTS_SVAL 0x408
#define AIP_DOWNSAMPLE_CTRL 0x41C
#define AIP_I2S_CHST0 0x414
#define AIP_I2S_CHST1 0x418
#define AIP_I2S_CTRL 0x410
#define AIP_N_VAL 0x404
#define AIP_SPDIF_CTRL 0x40C
#define AIP_TPI_CTRL 0x428
#define AIP_TXCTRL 0x424
#define AUD_DIS (0x0 << 2)
#define AUD_DIS_WR (0x0 << 18)
#define AUD_EN (0x1 << 2)
#define AUD_EN_WR (0x1 << 18)
#define AUD_ERR_THRESH (0x3F << 24)
#define AUD_ERR_THRESH_SHIFT 24
#define AUD_IN_EN (1 << 8)
#define AUD_IN_EN_SHIFT 8
#define AUD_MUTE_DIS (0x0 << 5)
#define AUD_MUTE_FIFO_EN (0x1 << 5)
#define AUD_PACKET_DROP (0x1 << 6)
#define AUD_RPT_DIS (0x0 << 2)
#define AUD_RPT_EN (0x1 << 2)
#define AUD_SEL_OWRT (1 << 9)
#define AVI_DIS (0x0 << 0)
#define AVI_DIS_WR (0x0 << 16)
#define AVI_EN (0x1 << 0)
#define AVI_EN_WR (0x1 << 16)
#define AVI_HEADER (0xFFFFFF << 0)
#define AVI_PKT00 (0xFFFFFFFF << 0)
#define AVI_PKT01 (0xFFFFFF << 0)
#define AVI_PKT02 (0xFFFFFFFF << 0)
#define AVI_PKT03 (0xFFFFFF << 0)
#define AVI_PKT04 (0xFFFFFFFF << 0)
#define AVI_PKT05 (0xFFFFFF << 0)
#define AVI_RPT_DIS (0x0 << 0)
#define AVI_RPT_EN (0x1 << 0)
#define C422_C420_CONFIG_BYPASS (0x1 << 5)
#define C422_C420_CONFIG_ENABLE (0x1 << 4)
#define C422_C420_CONFIG_OUT_CB_OR_CR (0x1 << 6)
#define C444_C422_CONFIG_ENABLE (0x1 << 0)
#define CBIT_ORDER_SAME (0x1 << 13)
#define CEA_AUD_EN (1 << 9)
#define CEA_AVI_EN (1 << 11)
#define CEA_CP_EN (1 << 6)
#define CEA_SPD_EN (1 << 10)
#define CLEAR_FIFO 0x9
#define CLOCK_SCL 0xA
#define CP_CLR_MUTE_EN (1 << 1)
#define CP_EN (0x1 << 5)
#define CP_EN_WR (0x1 << 21)
#define CP_RPT_EN (0x1 << 5)
#define CP_SET_MUTE_DIS (0 << 0)
#define CP_SET_MUTE_EN (1 << 0)
#define CTS_CAL_N4 (1 << 23)
#define CTS_REQ_EN (1 << 1)
#define CTS_SW_SEL (1 << 0)
#define C_SD0 (0x0 << 0)
#define C_SD1 (0x1 << 4)
#define C_SD2 (0x2 << 8)
#define C_SD3 (0x3 << 12)
#define C_SD4 (0x4 << 16)
#define C_SD5 (0x5 << 20)
#define C_SD6 (0x6 << 24)
#define C_SD7 (0x7 << 28)
#define DATA_DIR_LSB (0x1 << 9)
#define DATA_DIR_MSB (0x0 << 9)
#define DDC_CMD (0xF << 28)
#define DDC_CMD_SHIFT (28)
#define DDC_CTRL 0xC10
#define DDC_DATA_OUT (0xFF << 16)
#define DDC_DATA_OUT_CNT (0x1F << 8)
#define DDC_DATA_OUT_SHIFT (16)
#define DDC_DELAY_CNT (0xFFFF << 16)
#define DDC_DELAY_CNT_SHIFT (16)
#define DDC_DIN_CNT (0x3FF << 16)
#define DDC_DIN_CNT_SHIFT (16)
#define DDC_I2C_BUS_LOW (0x1 << 11)
#define DDC_I2C_IN_PROG (0x1 << 13)
#define DDC_I2C_IN_PROG_INT_CLR (1 << 29)
#define DDC_I2C_IN_PROG_INT_MASK (0 << 29)
#define DDC_I2C_IN_PROG_INT_STA (1 << 29)
#define DDC_I2C_IN_PROG_INT_UNCLR (0 << 29)
#define DDC_I2C_IN_PROG_INT_UNMASK (1 << 29)
#define DDC_I2C_NO_ACK (0x1 << 10)
#define DDC_OFFSET (0xFF << 8)
#define DDC_OFFSET_SHIFT (8)
#define DDC_SEGMENT (0xFF << 8)
#define DDC_SEGMENT_SHIFT (8)
#define DEEPCOLOR_MODE_10BIT (1 << 8)
#define DEEPCOLOR_MODE_12BIT (2 << 8)
#define DEEPCOLOR_MODE_16BIT (3 << 8)
#define DEEPCOLOR_MODE_8BIT (0 << 8)
#define DEEPCOLOR_MODE_MASKBIT (3 << 8)
#define DEEPCOLOR_PAT_EN (1 << 12)
#define DEEP_COLOR_ADD (0x1 << 4)
#define DOWNSAMPLE 0x2
#define DSD_EN (1 << 15)
#define DSD_MUTE_DATA (0x1 << 7)
#define ENH_READ_NO_ACK 0x4
#define FIFO0_MAP (0x3 << 0)
#define FIFO1_MAP (0x3 << 2)
#define FIFO2_MAP (0x3 << 4)
#define FIFO3_MAP (0x3 << 6)
#define FS_OVERRIDE_WRITE (1 << 1)
#define FS_UNOVERRIDE (0 << 1)
#define HBRA_ON (1 << 14)
#define HBR_FROM_SPDIF (1 << 20)
#define HDCP1X_CTRL 0xCD0
#define HDCP1X_ENC_EN (0x1 << 6)
#define HDCP1X_ENC_EN_SHIFT (6)
#define HDCP2X_CTRL_0 0xC20
#define HDCP2X_DDCM_STATUS 0xC68
#define HDCP2X_DIS_POLL_EN (0x1 << 16)
#define HDCP2X_EN (0x1 << 0)
#define HDCP2X_ENCRYPTING_ON (0x1 << 10)
#define HDCP2X_ENCRYPT_EN (0x1 << 7)
#define HDCP2X_ENCRYPT_EN_SHIFT (7)
#define HDCP2X_HPD_OVR (0x1 << 10)
#define HDCP2X_HPD_SW (0x1 << 11)
#define HDCP2X_POL_CTRL 0xC54
#define HDCP2X_RX_REAUTH_REQ_DDCM_INT_MASK (0 << 25)
#define HDCP2X_RX_REAUTH_REQ_DDCM_INT_UNMASK (1 << 25)
#define HDCP_ENABLE (0x0 << 0)
#define HDCP_ENCRYPTING_ON (0x1 << 26)
#define HDCP_TOP_CTRL 0xC00
#define HDMI2_OFF (0 << 2)
#define HDMI2_ON (1 << 2)
#define HDMITX_CONFIG 0x900
#define HDMITX_SW_HPD BIT(29)
#define HDMITX_SW_HPD_SHIFT (29)
#define HDMITX_SW_RSTB BIT(31)
#define HDMITX_SW_RSTB_SHIFT (31)
#define HDMI_MODE_DVI (0 << 3)
#define HDMI_MODE_HDMI (1 << 3)
#define HDMI_YUV420_MODE BIT(10)
#define HDMI_YUV420_MODE_SHIFT (10)
#define HPD_DDC_CTRL 0xC08
#define HPD_DDC_STATUS 0xC60
#define HPD_PIN_STA (0x1 << 4)
#define HPD_STATE (0x3 << 0)
#define HPD_STATE_CONNECTED (2)
#define HPD_STATE_DISCONNECTED (0)
#define HPD_STATE_SHIFT (0)
#define HTPLG_F_INT_STA (1 << 1)
#define HTPLG_R_INT_STA (1 << 0)
#define I2S2DSD_EN (1 << 30)
#define I2S_1ST_BIT_NOSHIFT (0x1 << 8)
#define I2S_EN (0xF << 16)
#define I2S_EN_SHIFT 16
#define JUSTIFY_RIGHT (0x1 << 10)
#define LAYOUT (0x1 << 18)
#define LAYOUT0 (0x0 << 4)
#define LAYOUT1 (0x1 << 4)
#define LFE_CC_SWAP (0x01 << 1)
#define MAP_SD0 0x0
#define MAP_SD1 0x1
#define MAP_SD2 0x2
#define MAP_SD3 0x3
#define MAX_1UI_WRITE (0xFF << 8)
#define MAX_1UI_WRITE_SHIFT 8
#define MAX_2UI_WRITE (0xFF << 16)
#define MAX_2UI_WRITE_SHIFT 16
#define MCLK_1152FS 0x6
#define MCLK_128FS 0x0
#define MCLK_192FS 0x7
#define MCLK_256FS 0x1
#define MCLK_384FS 0x2
#define MCLK_512FS 0x3
#define MCLK_768FS 0x4
#define MCLK_CTSGEN_SEL (0 << 3)
#define MCLK_EN (1 << 2)
#define NO_MCLK_CTSGEN_SEL (1 << 3)
#define NULL_PKT_EN (1 << 2)
#define NULL_PKT_VSYNC_HIGH_EN (1 << 3)
#define OUTPUT_FORMAT_DEMUX_420_ENABLE (0x1 << 10)
#define PORD_F_INT_STA (1 << 3)
#define PORD_PIN_STA (0x1 << 5)
#define PORD_R_INT_STA (1 << 2)
#define REG_VMUTE_EN (1 << 16)
#define RST4AUDIO (0x1 << 0)
#define RST4AUDIO_ACR (0x1 << 2)
#define RST4AUDIO_FIFO (0x1 << 1)
#define SCDC_CTRL 0xC18
#define SCK_EDGE_RISE (0x1 << 14)
#define SCR_OFF (0 << 4)
#define SCR_ON (1 << 4)
#define SEQ_READ_NO_ACK 0x2
#define SEQ_WRITE_REQ_ACK 0x7
#define SI2C_ADDR (0xFFFF << 16)
#define SI2C_ADDR_READ (0xF4)
#define SI2C_ADDR_SHIFT (16)
#define SI2C_CONFIRM_READ (0x1 << 2)
#define SI2C_CTRL 0xCAC
#define SI2C_RD (0x1 << 1)
#define SI2C_WDATA (0xFF << 8)
#define SI2C_WDATA_SHIFT (8)
#define SI2C_WR (0x1 << 0)
#define SPDIF_EN (1 << 13)
#define SPDIF_EN_SHIFT 13
#define SPDIF_HEADER (0x00FFFFFF << 0)
#define SPDIF_INTERNAL_MODULE (1 << 24)
#define SPDIF_PKT00 (0xFFFFFFFF << 0)
#define SPDIF_PKT01 (0x00FFFFFF << 0)
#define SPDIF_PKT02 (0xFFFFFFFF << 0)
#define SPDIF_PKT03 (0x00FFFFFF << 0)
#define SPDIF_PKT04 (0xFFFFFFFF << 0)
#define SPDIF_PKT05 (0x00FFFFFF << 0)
#define SPDIF_PKT06 (0xFFFFFFFF << 0)
#define SPDIF_PKT07 (0x00FFFFFF << 0)
#define SPD_DIS (0x0 << 1)
#define SPD_DIS_WR (0x0 << 17)
#define SPD_EN (0x1 << 1)
#define SPD_EN_WR (0x1 << 17)
#define SPD_RPT_DIS (0x0 << 1)
#define SPD_RPT_EN (0x1 << 1)
#define TOP_AIF_HEADER 0x040
#define TOP_AIF_PKT00 0x044
#define TOP_AIF_PKT01 0x048
#define TOP_AIF_PKT02 0x04C
#define TOP_AIF_PKT03 0x050
#define TOP_AUD_MAP 0x00C
#define TOP_AVI_HEADER 0x024
#define TOP_AVI_PKT00 0x028
#define TOP_AVI_PKT01 0x02C
#define TOP_AVI_PKT02 0x030
#define TOP_AVI_PKT03 0x034
#define TOP_AVI_PKT04 0x038
#define TOP_AVI_PKT05 0x03C
#define TOP_CFG00 0x000
#define TOP_CFG01 0x004
#define TOP_INFO_EN 0x01C
#define TOP_INFO_EN_EXPAND 0x368
#define TOP_INFO_RPT 0x020
#define TOP_INT_CLR00 0x1B8
#define TOP_INT_CLR01 0x1BC
#define TOP_INT_MASK00 0x1B0
#define TOP_INT_MASK01 0x1B4
#define TOP_INT_STA00 0x1A8
#define TOP_MISC_CTLR 0x1A4
#define TOP_SPDIF_HEADER 0x054
#define TOP_SPDIF_PKT00 0x058
#define TOP_SPDIF_PKT01 0x05C
#define TOP_SPDIF_PKT02 0x060
#define TOP_SPDIF_PKT03 0x064
#define TOP_SPDIF_PKT04 0x068
#define TOP_SPDIF_PKT05 0x06C
#define TOP_SPDIF_PKT06 0x070
#define TOP_SPDIF_PKT07 0x074
#define TOP_VMUTE_CFG1 0x1C8
#define TPI_AUDIO_LOOKUP_DIS (0x0 << 2)
#define TPI_AUDIO_LOOKUP_EN (0x1 << 2)
#define VBIT_COM (0x1 << 12)
#define VBIT_PCM (0x0 << 12)
#define VID_DOWNSAMPLE_CONFIG 0x8F0
#define VID_OUT_FORMAT 0x8FC
#define WR_1UI_LOCK (1 << 0)
#define WR_1UI_UNLOCK (0 << 0)
#define WR_2UI_LOCK (1 << 2)
#define WR_2UI_UNLOCK (0 << 2)
#define WS_HIGH (0x1 << 11)

#endif /* _MTK_HDMI_REGS_H */
