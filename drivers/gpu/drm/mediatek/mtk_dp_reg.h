/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 MediaTek Inc.
 * Copyright (c) 2021 BayLibre
 */
#ifndef _MTK_DP_REG_H_
#define _MTK_DP_REG_H_

#define MTK_DP_SIP_CONTROL_AARCH32 0x82000523
# define MTK_DP_SIP_ATF_VIDEO_UNMUTE 0x20
# define MTK_DP_SIP_ATF_EDP_VIDEO_UNMUTE 0x21
# define MTK_DP_SIP_ATF_REG_WRITE 0x22
# define MTK_DP_SIP_ATF_REG_READ 0x23
# define MTK_DP_SIP_ATF_CMD_COUNT 0x24

#define TOP_OFFSET		0x2000
#define ENC0_OFFSET		0x3000
#define ENC1_OFFSET		0x3200
#define TRANS_OFFSET		0x3400
#define AUX_OFFSET		0x3600
#define SEC_OFFSET		0x4000

#define MTK_DP_HPD_DISCONNECT	BIT(1)
#define MTK_DP_HPD_CONNECT	BIT(2)
#define MTK_DP_HPD_INTERRUPT	BIT(3)

#define MTK_DP_ENC0_P0_3000              (ENC0_OFFSET + 0x000)
# define LANE_NUM_DP_ENC0_P0_MASK                                      0x3
# define LANE_NUM_DP_ENC0_P0_SHIFT                                     0
# define VIDEO_MUTE_SW_DP_ENC0_P0_MASK                                 0x4
# define VIDEO_MUTE_SW_DP_ENC0_P0_SHIFT                                2
# define VIDEO_MUTE_SEL_DP_ENC0_P0_MASK                                0x8
# define VIDEO_MUTE_SEL_DP_ENC0_P0_SHIFT                               3
# define ENHANCED_FRAME_EN_DP_ENC0_P0_MASK                             0x10
# define ENHANCED_FRAME_EN_DP_ENC0_P0_SHIFT                            4
# define HDCP_FRAME_EN_DP_ENC0_P0_MASK                                 0x20
# define HDCP_FRAME_EN_DP_ENC0_P0_SHIFT                                5
# define IDP_EN_DP_ENC0_P0_MASK                                        0x40
# define IDP_EN_DP_ENC0_P0_SHIFT                                       6
# define BS_SYMBOL_CNT_RESET_DP_ENC0_P0_MASK                           0x80
# define BS_SYMBOL_CNT_RESET_DP_ENC0_P0_SHIFT                          7
# define MIXER_DUMMY_DATA_DP_ENC0_P0_MASK                              0xff00
# define MIXER_DUMMY_DATA_DP_ENC0_P0_SHIFT                             8

#define MTK_DP_ENC0_P0_3004              (ENC0_OFFSET + 0x004)
# define MIXER_STUFF_DUMMY_DATA_DP_ENC0_P0_MASK                        0xff
# define MIXER_STUFF_DUMMY_DATA_DP_ENC0_P0_SHIFT                       0
# define VIDEO_M_CODE_SEL_DP_ENC0_P0_MASK                              0x100
# define VIDEO_M_CODE_SEL_DP_ENC0_P0_SHIFT                             8
# define DP_TX_ENCODER_4P_RESET_SW_DP_ENC0_P0_MASK                     0x200
# define DP_TX_ENCODER_4P_RESET_SW_DP_ENC0_P0_SHIFT                    9
# define MIXER_RESET_SW_DP_ENC0_P0_MASK                                0x400
# define MIXER_RESET_SW_DP_ENC0_P0_SHIFT                               10
# define VIDEO_RESET_SW_DP_ENC0_P0_MASK                                0x800
# define VIDEO_RESET_SW_DP_ENC0_P0_SHIFT                               11
# define VIDEO_PATTERN_GEN_RESET_SW_DP_ENC0_P0_MASK                    0x1000
# define VIDEO_PATTERN_GEN_RESET_SW_DP_ENC0_P0_SHIFT                   12
# define SDP_RESET_SW_DP_ENC0_P0_MASK                                  0x2000
# define SDP_RESET_SW_DP_ENC0_P0_SHIFT                                 13
# define DP_TX_MUX_DP_ENC0_P0_MASK                                     0x4000
# define DP_TX_MUX_DP_ENC0_P0_SHIFT                                    14
# define MIXER_FSM_RESET_DP_ENC0_P0_MASK                               0x8000
# define MIXER_FSM_RESET_DP_ENC0_P0_SHIFT                              15

#define MTK_DP_ENC0_P0_3008              (ENC0_OFFSET + 0x008)
# define VIDEO_M_CODE_SW_0_DP_ENC0_P0_MASK                             0xffff
# define VIDEO_M_CODE_SW_0_DP_ENC0_P0_SHIFT                            0

#define MTK_DP_ENC0_P0_300C              (ENC0_OFFSET + 0x00C)
# define VIDEO_M_CODE_SW_1_DP_ENC0_P0_MASK                             0xff
# define VIDEO_M_CODE_SW_1_DP_ENC0_P0_SHIFT                            0
# define VIDEO_M_CODE_PULSE_DP_ENC0_P0_MASK                            0x100
# define VIDEO_M_CODE_PULSE_DP_ENC0_P0_SHIFT                           8
# define COMPRESSEDSTREAM_FLAG_DP_ENC0_P0_MASK                         0x200
# define COMPRESSEDSTREAM_FLAG_DP_ENC0_P0_SHIFT                        9
# define SDP_SPLIT_EN_DP_ENC0_P0_MASK                                  0x400
# define SDP_SPLIT_EN_DP_ENC0_P0_SHIFT                                 10
# define SDP_SPLIT_FIFO_RST_DP_ENC0_P0_MASK                            0x800
# define SDP_SPLIT_FIFO_RST_DP_ENC0_P0_SHIFT                           11
# define VIDEO_M_CODE_MULT_DIV_SEL_DP_ENC0_P0_MASK                     0x7000
# define VIDEO_M_CODE_MULT_DIV_SEL_DP_ENC0_P0_SHIFT                    12
# define SDP_AUDIO_ONE_SAMPLE_MODE_DP_ENC0_P0_MASK                     0x8000
# define SDP_AUDIO_ONE_SAMPLE_MODE_DP_ENC0_P0_SHIFT                    15

#define MTK_DP_ENC0_P0_3010              (ENC0_OFFSET + 0x010)
# define HTOTAL_SW_DP_ENC0_P0_MASK                                     0xffff
# define HTOTAL_SW_DP_ENC0_P0_SHIFT                                    0

#define MTK_DP_ENC0_P0_3014              (ENC0_OFFSET + 0x014)
# define VTOTAL_SW_DP_ENC0_P0_MASK                                     0xffff
# define VTOTAL_SW_DP_ENC0_P0_SHIFT                                    0

#define MTK_DP_ENC0_P0_3018              (ENC0_OFFSET + 0x018)
# define HSTART_SW_DP_ENC0_P0_MASK                                     0xffff
# define HSTART_SW_DP_ENC0_P0_SHIFT                                    0

#define MTK_DP_ENC0_P0_301C              (ENC0_OFFSET + 0x01C)
# define VSTART_SW_DP_ENC0_P0_MASK                                     0xffff
# define VSTART_SW_DP_ENC0_P0_SHIFT                                    0

#define MTK_DP_ENC0_P0_3020              (ENC0_OFFSET + 0x020)
# define HWIDTH_SW_DP_ENC0_P0_MASK                                     0xffff
# define HWIDTH_SW_DP_ENC0_P0_SHIFT                                    0

#define MTK_DP_ENC0_P0_3024              (ENC0_OFFSET + 0x024)
# define VHEIGHT_SW_DP_ENC0_P0_MASK                                    0xffff
# define VHEIGHT_SW_DP_ENC0_P0_SHIFT                                   0

#define MTK_DP_ENC0_P0_3028              (ENC0_OFFSET + 0x028)
# define HSW_SW_DP_ENC0_P0_MASK                                        0x7fff
# define HSW_SW_DP_ENC0_P0_SHIFT                                       0
# define HSP_SW_DP_ENC0_P0_MASK                                        0x8000
# define HSP_SW_DP_ENC0_P0_SHIFT                                       15

#define MTK_DP_ENC0_P0_302C              (ENC0_OFFSET + 0x02C)
# define VSW_SW_DP_ENC0_P0_MASK                                        0x7fff
# define VSW_SW_DP_ENC0_P0_SHIFT                                       0
# define VSP_SW_DP_ENC0_P0_MASK                                        0x8000
# define VSP_SW_DP_ENC0_P0_SHIFT                                       15

#define MTK_DP_ENC0_P0_3030              (ENC0_OFFSET + 0x030)
# define HTOTAL_SEL_DP_ENC0_P0_MASK                                    0x1
# define HTOTAL_SEL_DP_ENC0_P0_SHIFT                                   0
# define VTOTAL_SEL_DP_ENC0_P0_MASK                                    0x2
# define VTOTAL_SEL_DP_ENC0_P0_SHIFT                                   1
# define HSTART_SEL_DP_ENC0_P0_MASK                                    0x4
# define HSTART_SEL_DP_ENC0_P0_SHIFT                                   2
# define VSTART_SEL_DP_ENC0_P0_MASK                                    0x8
# define VSTART_SEL_DP_ENC0_P0_SHIFT                                   3
# define HWIDTH_SEL_DP_ENC0_P0_MASK                                    0x10
# define HWIDTH_SEL_DP_ENC0_P0_SHIFT                                   4
# define VHEIGHT_SEL_DP_ENC0_P0_MASK                                   0x20
# define VHEIGHT_SEL_DP_ENC0_P0_SHIFT                                  5
# define HSP_SEL_DP_ENC0_P0_MASK                                       0x40
# define HSP_SEL_DP_ENC0_P0_SHIFT                                      6
# define HSW_SEL_DP_ENC0_P0_MASK                                       0x80
# define HSW_SEL_DP_ENC0_P0_SHIFT                                      7
# define VSP_SEL_DP_ENC0_P0_MASK                                       0x100
# define VSP_SEL_DP_ENC0_P0_SHIFT                                      8
# define VSW_SEL_DP_ENC0_P0_MASK                                       0x200
# define VSW_SEL_DP_ENC0_P0_SHIFT                                      9
# define TX_VBID_SW_EN_DP_ENC0_P0_MASK                                 0x400
# define TX_VBID_SW_EN_DP_ENC0_P0_SHIFT                                10
# define VBID_AUDIO_MUTE_FLAG_SW_DP_ENC0_P0_MASK                       0x800
# define VBID_AUDIO_MUTE_SW_DP_ENC0_P0_SHIFT                           11
# define VBID_AUDIO_MUTE_FLAG_SEL_DP_ENC0_P0_MASK                      0x1000
# define VBID_AUDIO_MUTE_SEL_DP_ENC0_P0_SHIFT                          12
# define VBID_INTERLACE_FLAG_SW_DP_ENC0_P0_MASK                        0x2000
# define VBID_INTERLACE_FLAG_SW_DP_ENC0_P0_SHIFT                       13
# define VBID_INTERLACE_FLAG_SEL_DP_ENC0_P0_MASK                       0x4000
# define VBID_INTERLACE_FLAG_SEL_DP_ENC0_P0_SHIFT                      14
# define MIXER_SDP_EN_DP_ENC0_P0_MASK                                  0x8000
# define MIXER_SDP_EN_DP_ENC0_P0_SHIFT                                 15

#define MTK_DP_ENC0_P0_3034              (ENC0_OFFSET + 0x034)
# define MISC0_DATA_DP_ENC0_P0_MASK                                    0xff
# define MISC0_DATA_DP_ENC0_P0_SHIFT                                   0
# define MISC1_DATA_DP_ENC0_P0_MASK                                    0xff00
# define MISC1_DATA_DP_ENC0_P0_SHIFT                                   8

#define MTK_DP_ENC0_P0_3038              (ENC0_OFFSET + 0x038)
# define TX_VBID_SW_DP_ENC0_P0_MASK                                    0xff
# define TX_VBID_SW_DP_ENC0_P0_SHIFT                                   0
# define VIDEO_DATA_SWAP_DP_ENC0_P0_MASK                               0x700
# define VIDEO_DATA_SWAP_DP_ENC0_P0_SHIFT                              8
# define VIDEO_SOURCE_SEL_DP_ENC0_P0_MASK                              0x800
# define VIDEO_SOURCE_SEL_DP_ENC0_P0_SHIFT                             11
# define FIELD_VBID_SW_EN_DP_ENC0_P0_MASK                              0x1000
# define FIELD_VBID_SW_EN_DP_ENC0_P0_SHIFT                             12
# define FIELD_SW_DP_ENC0_P0_MASK                                      0x2000
# define FIELD_SW_DP_ENC0_P0_SHIFT                                     13
# define V3D_EN_SW_DP_ENC0_P0_MASK                                     0x4000
# define V3D_EN_SW_DP_ENC0_P0_SHIFT                                    14
# define V3D_LR_HW_SWAP_DP_ENC0_P0_MASK                                0x8000
# define V3D_LR_HW_SWAP_DP_ENC0_P0_SHIFT                               15

#define MTK_DP_ENC0_P0_303C              (ENC0_OFFSET + 0x03C)
# define SRAM_START_READ_THRD_DP_ENC0_P0_MASK                          0x3f
# define SRAM_START_READ_THRD_DP_ENC0_P0_SHIFT                         0
# define VIDEO_COLOR_DEPTH_DP_ENC0_P0_MASK                             0x700
# define VIDEO_COLOR_DEPTH_DP_ENC0_P0_SHIFT                            8
# define VIDEO_COLOR_DEPTH_DP_ENC0_P0_16BIT                            (0 << VIDEO_COLOR_DEPTH_DP_ENC0_P0_SHIFT)
# define VIDEO_COLOR_DEPTH_DP_ENC0_P0_12BIT                            (1 << VIDEO_COLOR_DEPTH_DP_ENC0_P0_SHIFT)
# define VIDEO_COLOR_DEPTH_DP_ENC0_P0_10BIT                            (2 << VIDEO_COLOR_DEPTH_DP_ENC0_P0_SHIFT)
# define VIDEO_COLOR_DEPTH_DP_ENC0_P0_8BIT                             (3 << VIDEO_COLOR_DEPTH_DP_ENC0_P0_SHIFT)
# define VIDEO_COLOR_DEPTH_DP_ENC0_P0_6BIT                             (4 << VIDEO_COLOR_DEPTH_DP_ENC0_P0_SHIFT)
# define VIDEO_COLOR_DEPTH_DP_ENC0_P0_DSC                              (7 << VIDEO_COLOR_DEPTH_DP_ENC0_P0_SHIFT)
# define PIXEL_ENCODE_FORMAT_DP_ENC0_P0_MASK                           0x7000
# define PIXEL_ENCODE_FORMAT_DP_ENC0_P0_SHIFT                          12
# define PIXEL_ENCODE_FORMAT_DP_ENC0_P0_RGB                            (0 << PIXEL_ENCODE_FORMAT_DP_ENC0_P0_SHIFT)
# define PIXEL_ENCODE_FORMAT_DP_ENC0_P0_YCBCR422                       (1 << PIXEL_ENCODE_FORMAT_DP_ENC0_P0_SHIFT)
# define PIXEL_ENCODE_FORMAT_DP_ENC0_P0_YCBCR420                       (2 << PIXEL_ENCODE_FORMAT_DP_ENC0_P0_SHIFT)
# define PIXEL_ENCODE_FORMAT_DP_ENC0_P0_YONLY                          (3 << PIXEL_ENCODE_FORMAT_DP_ENC0_P0_SHIFT)
# define PIXEL_ENCODE_FORMAT_DP_ENC0_P0_RAW                            (4 << PIXEL_ENCODE_FORMAT_DP_ENC0_P0_SHIFT)
# define PIXEL_ENCODE_FORMAT_DP_ENC0_P0_DSC                            (7 << PIXEL_ENCODE_FORMAT_DP_ENC0_P0_SHIFT)
# define VIDEO_MN_GEN_EN_DP_ENC0_P0_MASK                               0x8000
# define VIDEO_MN_GEN_EN_DP_ENC0_P0_SHIFT                              15

#define MTK_DP_ENC0_P0_3040              (ENC0_OFFSET + 0x040)
# define SDP_DOWN_CNT_INIT_DP_ENC0_P0_MASK                             0xfff
# define SDP_DOWN_CNT_INIT_DP_ENC0_P0_SHIFT                            0
# define AUDIO_32CH_EN_DP_ENC0_P0_MASK                                 0x1000
# define AUDIO_32CH_EN_DP_ENC0_P0_SHIFT                                12
# define AUDIO_32CH_SEL_DP_ENC0_P0_MASK                                0x2000
# define AUDIO_32CH_SEL_DP_ENC0_P0_SHIFT                               13
# define AUDIO_16CH_EN_DP_ENC0_P0_MASK                                 0x4000
# define AUDIO_16CH_EN_DP_ENC0_P0_SHIFT                                14
# define AUDIO_16CH_SEL_DP_ENC0_P0_MASK                                0x8000
# define AUDIO_16CH_SEL_DP_ENC0_P0_SHIFT                               15

#define MTK_DP_ENC0_P0_3044              (ENC0_OFFSET + 0x044)
# define VIDEO_N_CODE_0_DP_ENC0_P0_MASK                                0xffff
# define VIDEO_N_CODE_0_DP_ENC0_P0_SHIFT                               0

#define MTK_DP_ENC0_P0_3048              (ENC0_OFFSET + 0x048)
# define VIDEO_N_CODE_1_DP_ENC0_P0_MASK                                0xff
# define VIDEO_N_CODE_1_DP_ENC0_P0_SHIFT                               0

#define MTK_DP_ENC0_P0_304C              (ENC0_OFFSET + 0x04C)
# define VIDEO_SRAM_MODE_DP_ENC0_P0_MASK                                 0x3
# define VIDEO_SRAM_MODE_DP_ENC0_P0_SHIFT                                0
# define VBID_VIDEO_MUTE_DP_ENC0_P0_MASK                                 0x4
# define VBID_VIDEO_MUTE_DP_ENC0_P0_SHIFT                                2
# define VBID_VIDEO_MUTE_IDLE_PATTERN_SYNC_EN_DP_ENC0_P0_MASK            0x8
# define VBID_VIDEO_MUTE_IDLE_PATTERN_SYNC_EN_DP_ENC0_P0_SHIFT           3
# define HDCP_SYNC_SEL_DP_ENC0_P0_MASK                                   0x10
# define HDCP_SYNC_SEL_DP_ENC0_P0_SHIFT                                  4
# define HDCP_SYNC_SW_DP_ENC0_P0_MASK                                    0x20
# define HDCP_SYNC_SW_DP_ENC0_P0_SHIFT                                   5
# define SDP_VSYNC_RISING_MASK_DP_ENC0_P0_MASK                           0x100
# define SDP_VSYNC_RISING_MASK_DP_ENC0_P0_SHIFT                          8

#define MTK_DP_ENC0_P0_3050              (ENC0_OFFSET + 0x050)
# define VIDEO_N_CODE_MN_GEN_0_DP_ENC0_P0_MASK                           0xffff
# define VIDEO_N_CODE_MN_GEN_0_DP_ENC0_P0_SHIFT                          0

#define MTK_DP_ENC0_P0_3054              (ENC0_OFFSET + 0x054)
# define VIDEO_N_CODE_MN_GEN_1_DP_ENC0_P0_MASK                           0xff
# define VIDEO_N_CODE_MN_GEN_1_DP_ENC0_P0_SHIFT                          0

#define MTK_DP_ENC0_P0_3058              (ENC0_OFFSET + 0x058)
# define AUDIO_N_CODE_MN_GEN_0_DP_ENC0_P0_MASK                           0xffff
# define AUDIO_N_CODE_MN_GEN_0_DP_ENC0_P0_SHIFT                          0

#define MTK_DP_ENC0_P0_305C              (ENC0_OFFSET + 0x05C)
# define AUDIO_N_CODE_MN_GEN_1_DP_ENC0_P0_MASK                           0xff
# define AUDIO_N_CODE_MN_GEN_1_DP_ENC0_P0_SHIFT                          0

#define MTK_DP_ENC0_P0_3060              (ENC0_OFFSET + 0x060)
# define NUM_INTERLACE_FRAME_DP_ENC0_P0_MASK                             0x7
# define NUM_INTERLACE_FRAME_DP_ENC0_P0_SHIFT                            0
# define INTERLACE_DET_EVEN_EN_DP_ENC0_P0_MASK                           0x8
# define INTERLACE_DET_EVEN_EN_DP_ENC0_P0_SHIFT                          3
# define FIELD_DETECT_EN_DP_ENC0_P0_MASK                                 0x10
# define FIELD_DETECT_EN_DP_ENC0_P0_SHIFT                                4
# define FIELD_DETECT_UPDATE_THRD_DP_ENC0_P0_MASK                        0xff00
# define FIELD_DETECT_UPDATE_THRD_DP_ENC0_P0_SHIFT                       8

#define MTK_DP_ENC0_P0_3064              (ENC0_OFFSET + 0x064)
# define HDE_NUM_LAST_DP_ENC0_P0_MASK                                    0xffff
# define HDE_NUM_LAST_DP_ENC0_P0_SHIFT                                   0

#define MTK_DP_ENC0_P0_3088              (ENC0_OFFSET + 0x088)
# define AUDIO_DETECT_EN_DP_ENC0_P0_MASK                                 0x20
# define AUDIO_DETECT_EN_DP_ENC0_P0_SHIFT                                5
# define AU_EN_DP_ENC0_P0_MASK                                           0x40
# define AU_EN_DP_ENC0_P0_SHIFT                                          6
# define AUDIO_8CH_EN_DP_ENC0_P0_MASK                                    0x80
# define AUDIO_8CH_EN_DP_ENC0_P0_SHIFT                                   7
# define AUDIO_8CH_SEL_DP_ENC0_P0_MASK                                   0x100
# define AUDIO_8CH_SEL_DP_ENC0_P0_SHIFT                                  8
# define AU_GEN_EN_DP_ENC0_P0_MASK                                       0x200
# define AU_GEN_EN_DP_ENC0_P0_SHIFT                                      9
# define AUDIO_MN_GEN_EN_DP_ENC0_P0_MASK                                 0x1000
# define AUDIO_MN_GEN_EN_DP_ENC0_P0_SHIFT                                12
# define DIS_ASP_DP_ENC0_P0_MASK                                         0x2000
# define DIS_ASP_DP_ENC0_P0_SHIFT                                        13
# define AUDIO_2CH_EN_DP_ENC0_P0_MASK                                    0x4000
# define AUDIO_2CH_EN_DP_ENC0_P0_SHIFT                                   14
# define AUDIO_2CH_SEL_DP_ENC0_P0_MASK                                   0x8000
# define AUDIO_2CH_SEL_DP_ENC0_P0_SHIFT                                  15

#define MTK_DP_ENC0_P0_308C              (ENC0_OFFSET + 0x08C)
# define CH_STATUS_0_DP_ENC0_P0_MASK                                     0xffff
# define CH_STATUS_0_DP_ENC0_P0_SHIFT                                    0

#define MTK_DP_ENC0_P0_3090              (ENC0_OFFSET + 0x090)
# define CH_STATUS_1_DP_ENC0_P0_MASK                                     0xffff
# define CH_STATUS_1_DP_ENC0_P0_SHIFT                                    0

#define MTK_DP_ENC0_P0_3094              (ENC0_OFFSET + 0x094)
# define CH_STATUS_2_DP_ENC0_P0_MASK                                     0xff
# define CH_STATUS_2_DP_ENC0_P0_SHIFT                                    0

#define MTK_DP_ENC0_P0_3098              (ENC0_OFFSET + 0x098)
# define USER_DATA_0_DP_ENC0_P0_MASK                                     0xffff
# define USER_DATA_0_DP_ENC0_P0_SHIFT                                    0

#define MTK_DP_ENC0_P0_309C              (ENC0_OFFSET + 0x09C)
# define USER_DATA_1_DP_ENC0_P0_MASK                                     0xffff
# define USER_DATA_1_DP_ENC0_P0_SHIFT                                    0

#define MTK_DP_ENC0_P0_30A0              (ENC0_OFFSET + 0x0A0)
# define USER_DATA_2_DP_ENC0_P0_MASK                                     0xff
# define USER_DATA_2_DP_ENC0_P0_SHIFT                                    0
# define VSC_EXT_VESA_CFG_DP_ENC0_P0_MASK                                0xf00
# define VSC_EXT_VESA_CFG_DP_ENC0_P0_SHIFT                               8
# define VSC_EXT_CEA_CFG_DP_ENC0_P0_MASK                                 0xf000
# define VSC_EXT_CEA_CFG_DP_ENC0_P0_SHIFT                                12

#define MTK_DP_ENC0_P0_30A4              (ENC0_OFFSET + 0x0A4)
# define AU_TS_CFG_DP_ENC0_P0_MASK                                       0xff
# define AU_TS_CFG_DP_ENC0_P0_SHIFT                                      0
# define AVI_CFG_DP_ENC0_P0_MASK                                         0xff00
# define AVI_CFG_DP_ENC0_P0_SHIFT                                        8

#define MTK_DP_ENC0_P0_30A8              (ENC0_OFFSET + 0x0A8)
# define AUI_CFG_DP_ENC0_P0_MASK                                         0xff
# define AUI_CFG_DP_ENC0_P0_SHIFT                                        0
# define SPD_CFG_DP_ENC0_P0_MASK                                         0xff00
# define SPD_CFG_DP_ENC0_P0_SHIFT                                        8

#define MTK_DP_ENC0_P0_30AC              (ENC0_OFFSET + 0x0AC)
# define MPEG_CFG_DP_ENC0_P0_MASK                                        0xff
# define MPEG_CFG_DP_ENC0_P0_SHIFT                                       0
# define NTSC_CFG_DP_ENC0_P0_MASK                                        0xff00
# define NTSC_CFG_DP_ENC0_P0_SHIFT                                       8

#define MTK_DP_ENC0_P0_30B0              (ENC0_OFFSET + 0x0B0)
# define VSP_CFG_DP_ENC0_P0_MASK                                         0xff
# define VSP_CFG_DP_ENC0_P0_SHIFT                                        0
# define EXT_CFG_DP_ENC0_P0_MASK                                         0xff00
# define EXT_CFG_DP_ENC0_P0_SHIFT                                        8

#define MTK_DP_ENC0_P0_30B4              (ENC0_OFFSET + 0x0B4)
# define ACM_CFG_DP_ENC0_P0_MASK                                         0xff
# define ACM_CFG_DP_ENC0_P0_SHIFT                                        0
# define ISRC_CFG_DP_ENC0_P0_MASK                                        0xff00
# define ISRC_CFG_DP_ENC0_P0_SHIFT                                       8

#define MTK_DP_ENC0_P0_30B8              (ENC0_OFFSET + 0x0B8)
# define VSC_CFG_DP_ENC0_P0_MASK                                         0xff
# define VSC_CFG_DP_ENC0_P0_SHIFT                                        0
# define MSA_CFG_DP_ENC0_P0_MASK                                         0xff00
# define MSA_CFG_DP_ENC0_P0_SHIFT                                        8

#define MTK_DP_ENC0_P0_30BC              (ENC0_OFFSET + 0x0BC)
# define ISRC_CONT_DP_ENC0_P0_MASK                                       0x1
# define ISRC_CONT_DP_ENC0_P0_SHIFT                                      0
# define MSA_BY_SDP_DP_ENC0_P0_MASK                                      0x2
# define MSA_BY_SDP_DP_ENC0_P0_SHIFT                                     1
# define SDP_EN_DP_ENC0_P0_MASK                                          0x4
# define SDP_EN_DP_ENC0_P0_SHIFT                                         2
# define NIBBLE_INTERLEAVER_EN_DP_ENC0_P0_MASK                           0x8
# define NIBBLE_INTERLEAVER_EN_DP_ENC0_P0_SHIFT                          3
# define ECC_EN_DP_ENC0_P0_MASK                                          0x10
# define ECC_EN_DP_ENC0_P0_SHIFT                                         4
# define ASP_MIN_PL_SIZE_DP_ENC0_P0_MASK                                 0x60
# define ASP_MIN_PL_SIZE_DP_ENC0_P0_SHIFT                                5
# define AUDIO_M_CODE_MULT_DIV_SEL_DP_ENC0_P0_MASK                       0x700
# define AUDIO_M_CODE_MULT_DIV_SEL_DP_ENC0_P0_SHIFT                      8
# define AUDIO_M_CODE_SEL_DP_ENC0_P0_MASK                                0x4000
# define AUDIO_M_CODE_SEL_DP_ENC0_P0_SHIFT                               14
# define ASP_HB23_SEL_DP_ENC0_P0_MASK                                    0x8000
# define ASP_HB23_SEL_DP_ENC0_P0_SHIFT                                   15

#define MTK_DP_ENC0_P0_30C0              (ENC0_OFFSET + 0x0C0)
# define AU_TS_HB0_DP_ENC0_P0_MASK                                       0xff
# define AU_TS_HB0_DP_ENC0_P0_SHIFT                                      0
# define AU_TS_HB1_DP_ENC0_P0_MASK                                       0xff00
# define AU_TS_HB1_DP_ENC0_P0_SHIFT                                      8

#define MTK_DP_ENC0_P0_30C4              (ENC0_OFFSET + 0x0C4)
# define AU_TS_HB2_DP_ENC0_P0_MASK                                       0xff
# define AU_TS_HB2_DP_ENC0_P0_SHIFT                                      0
# define AU_TS_HB3_DP_ENC0_P0_MASK                                       0xff00
# define AU_TS_HB3_DP_ENC0_P0_SHIFT                                      8

#define MTK_DP_ENC0_P0_30C8              (ENC0_OFFSET + 0x0C8)
# define AUDIO_M_CODE_SW_0_DP_ENC0_P0_MASK                               0xffff
# define AUDIO_M_CODE_SW_0_DP_ENC0_P0_SHIFT                              0

#define MTK_DP_ENC0_P0_30CC              (ENC0_OFFSET + 0x0CC)
# define AUDIO_M_CODE_SW_1_DP_ENC0_P0_MASK                               0xff
# define AUDIO_M_CODE_SW_1_DP_ENC0_P0_SHIFT                              0

#define MTK_DP_ENC0_P0_30D0              (ENC0_OFFSET + 0x0D0)
# define AUDIO_N_CODE_0_DP_ENC0_P0_MASK                                  0xffff
# define AUDIO_N_CODE_0_DP_ENC0_P0_SHIFT                                 0

#define MTK_DP_ENC0_P0_30D4              (ENC0_OFFSET + 0x0D4)
# define AUDIO_N_CODE_1_DP_ENC0_P0_MASK                                  0xff
# define AUDIO_N_CODE_1_DP_ENC0_P0_SHIFT                                 0

#define MTK_DP_ENC0_P0_30D8              (ENC0_OFFSET + 0x0D8)
# define ACM_HB0_DP_ENC0_P0_MASK                                         0xff
# define ACM_HB0_DP_ENC0_P0_SHIFT                                        0
# define ACM_HB1_DP_ENC0_P0_MASK                                         0xff00
# define ACM_HB1_DP_ENC0_P0_SHIFT                                        8

#define MTK_DP_ENC0_P0_30DC              (ENC0_OFFSET + 0x0DC)
# define ACM_HB2_DP_ENC0_P0_MASK                                         0xff
# define ACM_HB2_DP_ENC0_P0_SHIFT                                        0
# define ACM_HB3_DP_ENC0_P0_MASK                                         0xff00
# define ACM_HB3_DP_ENC0_P0_SHIFT                                        8

#define MTK_DP_ENC0_P0_30E0              (ENC0_OFFSET + 0x0E0)
# define ISRC_HB0_DP_ENC0_P0_MASK                                        0xff
# define ISRC_HB0_DP_ENC0_P0_SHIFT                                       0
# define ISRC_HB1_DP_ENC0_P0_MASK                                        0xff00
# define ISRC_HB1_DP_ENC0_P0_SHIFT                                       8

#define MTK_DP_ENC0_P0_30E4              (ENC0_OFFSET + 0x0E4)
# define ISRC_HB2_DP_ENC0_P0_MASK                                        0xff
# define ISRC_HB2_DP_ENC0_P0_SHIFT                                       0
# define ISRC0_HB3_DP_ENC0_P0_MASK                                       0xff00
# define ISRC0_HB3_DP_ENC0_P0_SHIFT                                      8

#define MTK_DP_ENC0_P0_30E8              (ENC0_OFFSET + 0x0E8)
# define AVI_HB0_DP_ENC0_P0_MASK                                         0xff
# define AVI_HB0_DP_ENC0_P0_SHIFT                                        0
# define AVI_HB1_DP_ENC0_P0_MASK                                         0xff00
# define AVI_HB1_DP_ENC0_P0_SHIFT                                        8

#define MTK_DP_ENC0_P0_30EC              (ENC0_OFFSET + 0x0EC)
# define AVI_HB2_DP_ENC0_P0_MASK                                         0xff
# define AVI_HB2_DP_ENC0_P0_SHIFT                                        0
# define AVI_HB3_DP_ENC0_P0_MASK                                         0xff00
# define AVI_HB3_DP_ENC0_P0_SHIFT                                        8

#define MTK_DP_ENC0_P0_30F0              (ENC0_OFFSET + 0x0F0)
# define AUI_HB0_DP_ENC0_P0_MASK                                         0xff
# define AUI_HB0_DP_ENC0_P0_SHIFT                                        0
# define AUI_HB1_DP_ENC0_P0_MASK                                         0xff00
# define AUI_HB1_DP_ENC0_P0_SHIFT                                        8

#define MTK_DP_ENC0_P0_30F4              (ENC0_OFFSET + 0x0F4)
# define AUI_HB2_DP_ENC0_P0_MASK                                         0xff
# define AUI_HB2_DP_ENC0_P0_SHIFT                                        0
# define AUI_HB3_DP_ENC0_P0_MASK                                         0xff00
# define AUI_HB3_DP_ENC0_P0_SHIFT                                        8

#define MTK_DP_ENC0_P0_30F8              (ENC0_OFFSET + 0x0F8)
# define SPD_HB0_DP_ENC0_P0_MASK                                         0xff
# define SPD_HB0_DP_ENC0_P0_SHIFT                                        0
# define SPD_HB1_DP_ENC0_P0_MASK                                         0xff00
# define SPD_HB1_DP_ENC0_P0_SHIFT                                        8

#define MTK_DP_ENC0_P0_30FC              (ENC0_OFFSET + 0x0FC)
# define SPD_HB2_DP_ENC0_P0_MASK                                         0xff
# define SPD_HB2_DP_ENC0_P0_SHIFT                                        0
# define SPD_HB3_DP_ENC0_P0_MASK                                         0xff00
# define SPD_HB3_DP_ENC0_P0_SHIFT                                        8

#define MTK_DP_ENC0_P0_3100              (ENC0_OFFSET + 0x100)
# define MPEG_HB0_DP_ENC0_P0_MASK                                        0xff
# define MPEG_HB0_DP_ENC0_P0_SHIFT                                       0
# define MPEG_HB1_DP_ENC0_P0_MASK                                        0xff00
# define MPEG_HB1_DP_ENC0_P0_SHIFT                                       8

#define MTK_DP_ENC0_P0_3104              (ENC0_OFFSET + 0x104)
# define MPEG_HB2_DP_ENC0_P0_MASK                                        0xff
# define MPEG_HB2_DP_ENC0_P0_SHIFT                                       0
# define MPEG_HB3_DP_ENC0_P0_MASK                                        0xff00
# define MPEG_HB3_DP_ENC0_P0_SHIFT                                       8

#define MTK_DP_ENC0_P0_3108              (ENC0_OFFSET + 0x108)
# define NTSC_HB0_DP_ENC0_P0_MASK                                        0xff
# define NTSC_HB0_DP_ENC0_P0_SHIFT                                       0
# define NTSC_HB1_DP_ENC0_P0_MASK                                        0xff00
# define NTSC_HB1_DP_ENC0_P0_SHIFT                                       8

#define MTK_DP_ENC0_P0_310C              (ENC0_OFFSET + 0x10C)
# define NTSC_HB2_DP_ENC0_P0_MASK                                        0xff
# define NTSC_HB2_DP_ENC0_P0_SHIFT                                       0
# define NTSC_HB3_DP_ENC0_P0_MASK                                        0xff00
# define NTSC_HB3_DP_ENC0_P0_SHIFT                                       8

#define MTK_DP_ENC0_P0_3110              (ENC0_OFFSET + 0x110)
# define VSP_HB0_DP_ENC0_P0_MASK                                         0xff
# define VSP_HB0_DP_ENC0_P0_SHIFT                                        0
# define VSP_HB1_DP_ENC0_P0_MASK                                         0xff00
# define VSP_HB1_DP_ENC0_P0_SHIFT                                        8

#define MTK_DP_ENC0_P0_3114              (ENC0_OFFSET + 0x114)
# define VSP_HB2_DP_ENC0_P0_MASK                                         0xff
# define VSP_HB2_DP_ENC0_P0_SHIFT                                        0
# define VSP_HB3_DP_ENC0_P0_MASK                                         0xff00
# define VSP_HB3_DP_ENC0_P0_SHIFT                                        8

#define MTK_DP_ENC0_P0_3118              (ENC0_OFFSET + 0x118)
# define VSC_HB0_DP_ENC0_P0_MASK                                         0xff
# define VSC_HB0_DP_ENC0_P0_SHIFT                                        0
# define VSC_HB1_DP_ENC0_P0_MASK                                         0xff00
# define VSC_HB1_DP_ENC0_P0_SHIFT                                        8

#define MTK_DP_ENC0_P0_311C              (ENC0_OFFSET + 0x11C)
# define VSC_HB2_DP_ENC0_P0_MASK                                         0xff
# define VSC_HB2_DP_ENC0_P0_SHIFT                                        0
# define VSC_HB3_DP_ENC0_P0_MASK                                         0xff00
# define VSC_HB3_DP_ENC0_P0_SHIFT                                        8

#define MTK_DP_ENC0_P0_3120              (ENC0_OFFSET + 0x120)
# define EXT_HB0_DP_ENC0_P0_MASK                                         0xff
# define EXT_HB0_DP_ENC0_P0_SHIFT                                        0
# define EXT_HB1_DP_ENC0_P0_MASK                                         0xff00
# define EXT_HB1_DP_ENC0_P0_SHIFT                                        8

#define MTK_DP_ENC0_P0_3124              (ENC0_OFFSET + 0x124)
# define EXT_HB2_DP_ENC0_P0_MASK                                         0xff
# define EXT_HB2_DP_ENC0_P0_SHIFT                                        0
# define EXT_HB3_DP_ENC0_P0_MASK                                         0xff00
# define EXT_HB3_DP_ENC0_P0_SHIFT                                        8

#define MTK_DP_ENC0_P0_3128              (ENC0_OFFSET + 0x128)
# define ASP_HB0_DP_ENC0_P0_MASK                                         0xff
# define ASP_HB0_DP_ENC0_P0_SHIFT                                        0
# define ASP_HB1_DP_ENC0_P0_MASK                                         0xff00
# define ASP_HB1_DP_ENC0_P0_SHIFT                                        8

#define MTK_DP_ENC0_P0_312C              (ENC0_OFFSET + 0x12C)
# define ASP_HB2_DP_ENC0_P0_MASK                                         0xff
# define ASP_HB2_DP_ENC0_P0_SHIFT                                        0
# define ASP_HB3_DP_ENC0_P0_MASK                                         0xff00
# define ASP_HB3_DP_ENC0_P0_SHIFT                                        8

#define MTK_DP_ENC0_P0_3130              (ENC0_OFFSET + 0x130)
# define PPS_HB0_DP_ENC0_P0_MASK                                         0xff
# define PPS_HB0_DP_ENC0_P0_SHIFT                                        0
# define PPS_HB1_DP_ENC0_P0_MASK                                         0xff00
# define PPS_HB1_DP_ENC0_P0_SHIFT                                        8

#define MTK_DP_ENC0_P0_3134              (ENC0_OFFSET + 0x134)
# define PPS_HB2_DP_ENC0_P0_MASK                                         0xff
# define PPS_HB2_DP_ENC0_P0_SHIFT                                        0
# define PPS_HB3_DP_ENC0_P0_MASK                                         0xff00
# define PPS_HB3_DP_ENC0_P0_SHIFT                                        8

#define MTK_DP_ENC0_P0_3138              (ENC0_OFFSET + 0x138)
# define HDR0_HB0_DP_ENC0_P0_MASK                                        0xff
# define HDR0_HB0_DP_ENC0_P0_SHIFT                                       0
# define HDR0_HB1_DP_ENC0_P0_MASK                                        0xff00
# define HDR0_HB1_DP_ENC0_P0_SHIFT                                       8

#define MTK_DP_ENC0_P0_313C              (ENC0_OFFSET + 0x13C)
# define HDR0_HB2_DP_ENC0_P0_MASK                                        0xff
# define HDR0_HB2_DP_ENC0_P0_SHIFT                                       0
# define HDR0_HB3_DP_ENC0_P0_MASK                                        0xff00
# define HDR0_HB3_DP_ENC0_P0_SHIFT                                       8

#define MTK_DP_ENC0_P0_3140              (ENC0_OFFSET + 0x140)
# define PGEN_CURSOR_V_DP_ENC0_P0_MASK                                   0x1fff
# define PGEN_CURSOR_V_DP_ENC0_P0_SHIFT                                  0
# define PGEN_TG_SEL_DP_ENC0_P0_MASK                                     0x2000
# define PGEN_TG_SEL_DP_ENC0_P0_SHIFT                                    13
# define PGEN_PG_SEL_DP_ENC0_P0_MASK                                     0x4000
# define PGEN_PG_SEL_DP_ENC0_P0_SHIFT                                    14
# define PGEN_CURSOR_EN_DP_ENC0_P0_MASK                                  0x8000
# define PGEN_CURSOR_EN_DP_ENC0_P0_SHIFT                                 15

#define MTK_DP_ENC0_P0_3144              (ENC0_OFFSET + 0x144)
# define PGEN_CURSOR_H_DP_ENC0_P0_MASK                                   0x3fff
# define PGEN_CURSOR_H_DP_ENC0_P0_SHIFT                                  0

#define MTK_DP_ENC0_P0_3148              (ENC0_OFFSET + 0x148)
# define PGEN_CURSOR_RGB_COLOR_CODE_0_DP_ENC0_P0_MASK                    0xffff
# define PGEN_CURSOR_RGB_COLOR_CODE_0_DP_ENC0_P0_SHIFT                   0

#define MTK_DP_ENC0_P0_314C              (ENC0_OFFSET + 0x14C)
# define PGEN_CURSOR_RGB_COLOR_CODE_1_DP_ENC0_P0_MASK                    0xffff
# define PGEN_CURSOR_RGB_COLOR_CODE_1_DP_ENC0_P0_SHIFT                   0

#define MTK_DP_ENC0_P0_3150              (ENC0_OFFSET + 0x150)
# define PGEN_CURSOR_RGB_COLOR_CODE_2_DP_ENC0_P0_MASK                    0xf
# define PGEN_CURSOR_RGB_COLOR_CODE_2_DP_ENC0_P0_SHIFT                   0

#define MTK_DP_ENC0_P0_3154              (ENC0_OFFSET + 0x154)
# define PGEN_HTOTAL_DP_ENC0_P0_MASK                                     0x3fff
# define PGEN_HTOTAL_DP_ENC0_P0_SHIFT                                    0

#define MTK_DP_ENC0_P0_3158              (ENC0_OFFSET + 0x158)
# define PGEN_HSYNC_RISING_DP_ENC0_P0_MASK                               0x3fff
# define PGEN_HSYNC_RISING_DP_ENC0_P0_SHIFT                              0

#define MTK_DP_ENC0_P0_315C              (ENC0_OFFSET + 0x15C)
# define PGEN_HSYNC_PULSE_WIDTH_DP_ENC0_P0_MASK                          0x3fff
# define PGEN_HSYNC_PULSE_WIDTH_DP_ENC0_P0_SHIFT                         0

#define MTK_DP_ENC0_P0_3160              (ENC0_OFFSET + 0x160)
# define PGEN_HFDE_START_DP_ENC0_P0_MASK                                 0x3fff
# define PGEN_HFDE_START_DP_ENC0_P0_SHIFT                                0

#define MTK_DP_ENC0_P0_3164              (ENC0_OFFSET + 0x164)
# define PGEN_HFDE_ACTIVE_WIDTH_DP_ENC0_P0_MASK                          0x3fff
# define PGEN_HFDE_ACTIVE_WIDTH_DP_ENC0_P0_SHIFT                         0

#define MTK_DP_ENC0_P0_3168              (ENC0_OFFSET + 0x168)
# define PGEN_VTOTAL_DP_ENC0_P0_MASK                                     0x1fff
# define PGEN_VTOTAL_DP_ENC0_P0_SHIFT                                    0

#define MTK_DP_ENC0_P0_316C              (ENC0_OFFSET + 0x16C)
# define PGEN_VSYNC_RISING_DP_ENC0_P0_MASK                               0x1fff
# define PGEN_VSYNC_RISING_DP_ENC0_P0_SHIFT                              0

#define MTK_DP_ENC0_P0_3170              (ENC0_OFFSET + 0x170)
# define PGEN_VSYNC_PULSE_WIDTH_DP_ENC0_P0_MASK                          0x1fff
# define PGEN_VSYNC_PULSE_WIDTH_DP_ENC0_P0_SHIFT                         0

#define MTK_DP_ENC0_P0_3174              (ENC0_OFFSET + 0x174)
# define PGEN_VFDE_START_DP_ENC0_P0_MASK                                 0x1fff
# define PGEN_VFDE_START_DP_ENC0_P0_SHIFT                                0

#define MTK_DP_ENC0_P0_3178              (ENC0_OFFSET + 0x178)
# define PGEN_VFDE_ACTIVE_WIDTH_DP_ENC0_P0_MASK                          0x1fff
# define PGEN_VFDE_ACTIVE_WIDTH_DP_ENC0_P0_SHIFT                         0

#define MTK_DP_ENC0_P0_317C              (ENC0_OFFSET + 0x17C)
# define PGEN_PAT_BASE_PIXEL_0_DP_ENC0_P0_MASK                           0xfff
# define PGEN_PAT_BASE_PIXEL_0_DP_ENC0_P0_SHIFT                          0

#define MTK_DP_ENC0_P0_3180              (ENC0_OFFSET + 0x180)
# define PGEN_PAT_BASE_PIXEL_1_DP_ENC0_P0_MASK                           0xfff
# define PGEN_PAT_BASE_PIXEL_1_DP_ENC0_P0_SHIFT                          0

#define MTK_DP_ENC0_P0_3184              (ENC0_OFFSET + 0x184)
# define PGEN_PAT_BASE_PIXEL_2_DP_ENC0_P0_MASK                           0xfff
# define PGEN_PAT_BASE_PIXEL_2_DP_ENC0_P0_SHIFT                          0

#define MTK_DP_ENC0_P0_3188              (ENC0_OFFSET + 0x188)
# define PGEN_INITIAL_H_CNT_DP_ENC0_P0_MASK                              0x3fff
# define PGEN_INITIAL_H_CNT_DP_ENC0_P0_SHIFT                             0

#define MTK_DP_ENC0_P0_318C              (ENC0_OFFSET + 0x18C)
# define PGEN_INITIAL_V_CNT_DP_ENC0_P0_MASK                              0x1fff
# define PGEN_INITIAL_V_CNT_DP_ENC0_P0_SHIFT                             0

#define MTK_DP_ENC0_P0_3190              (ENC0_OFFSET + 0x190)
# define PGEN_INITIAL_CB_SEL_DP_ENC0_P0_MASK                             0x7
# define PGEN_INITIAL_CB_SEL_DP_ENC0_P0_SHIFT                            0
# define PGEN_FRAME_8K4K_MODE_EN_DP_ENC0_P0_MASK                         0x10
# define PGEN_FRAME_8K4K_MODE_EN_DP_ENC0_P0_SHIFT                        4
# define PGEN_FRAME_8K4K_MODE_SET_DP_ENC0_P0_MASK                        0x20
# define PGEN_FRAME_8K4K_MODE_SET_DP_ENC0_P0_SHIFT                       5
# define PGEN_INITIAL_H_GRAD_FLAG_DP_ENC0_P0_MASK                        0x40
# define PGEN_INITIAL_H_GRAD_FLAG_DP_ENC0_P0_SHIFT                       6
# define PGEN_INITIAL_V_GRAD_FLAG_DP_ENC0_P0_MASK                        0x80
# define PGEN_INITIAL_V_GRAD_FLAG_DP_ENC0_P0_SHIFT                       7
# define PGEN_FRAME_END_H_EN_DP_ENC0_P0_MASK                             0x100
# define PGEN_FRAME_END_H_EN_DP_ENC0_P0_SHIFT                            8
# define PGEN_FRAME_END_V_EN_DP_ENC0_P0_MASK                             0x200
# define PGEN_FRAME_END_V_EN_DP_ENC0_P0_SHIFT                            9

#define MTK_DP_ENC0_P0_3194              (ENC0_OFFSET + 0x194)
# define PGEN_PAT_EXTRA_PIXEL_0_DP_ENC0_P0_MASK                          0xfff
# define PGEN_PAT_EXTRA_PIXEL_0_DP_ENC0_P0_SHIFT                         0

#define MTK_DP_ENC0_P0_3198              (ENC0_OFFSET + 0x198)
# define PGEN_PAT_EXTRA_PIXEL_1_DP_ENC0_P0_MASK                          0xfff
# define PGEN_PAT_EXTRA_PIXEL_1_DP_ENC0_P0_SHIFT                         0

#define MTK_DP_ENC0_P0_319C              (ENC0_OFFSET + 0x19C)
# define PGEN_PAT_EXTRA_PIXEL_2_DP_ENC0_P0_MASK                          0xfff
# define PGEN_PAT_EXTRA_PIXEL_2_DP_ENC0_P0_SHIFT                         0

#define MTK_DP_ENC0_P0_31A0              (ENC0_OFFSET + 0x1A0)
# define PGEN_PAT_INCREMENT_0_DP_ENC0_P0_MASK                            0xffff
# define PGEN_PAT_INCREMENT_0_DP_ENC0_P0_SHIFT                           0

#define MTK_DP_ENC0_P0_31A4              (ENC0_OFFSET + 0x1A4)
# define PGEN_PAT_INCREMENT_1_DP_ENC0_P0_MASK                            0x1
# define PGEN_PAT_INCREMENT_1_DP_ENC0_P0_SHIFT                           0

#define MTK_DP_ENC0_P0_31A8              (ENC0_OFFSET + 0x1A8)
# define PGEN_PAT_HWIDTH_DP_ENC0_P0_MASK                                 0x3fff
# define PGEN_PAT_HWIDTH_DP_ENC0_P0_SHIFT                                0

#define MTK_DP_ENC0_P0_31AC              (ENC0_OFFSET + 0x1AC)
# define PGEN_PAT_VWIDTH_DP_ENC0_P0_MASK                                 0x1fff
# define PGEN_PAT_VWIDTH_DP_ENC0_P0_SHIFT                                0

#define MTK_DP_ENC0_P0_31B0              (ENC0_OFFSET + 0x1B0)
# define PGEN_PAT_RGB_ENABLE_DP_ENC0_P0_MASK                             0x7
# define PGEN_PAT_RGB_ENABLE_DP_ENC0_P0_SHIFT                            0
# define PGEN_PATTERN_SEL_DP_ENC0_P0_MASK                                0x70
# define PGEN_PATTERN_SEL_DP_ENC0_P0_SHIFT                               4
# define PGEN_PAT_DIRECTION_DP_ENC0_P0_MASK                              0x80
# define PGEN_PAT_DIRECTION_DP_ENC0_P0_SHIFT                             7
# define PGEN_PAT_GRADIENT_NORMAL_MODE_DP_ENC0_P0_MASK                   0x100
# define PGEN_PAT_GRADIENT_NORMAL_MODE_DP_ENC0_P0_SHIFT                  8
# define PGEN_PAT_COLOR_BAR_GRADIENT_EN_DP_ENC0_P0_MASK                  0x200
# define PGEN_PAT_COLOR_BAR_GRADIENT_EN_DP_ENC0_P0_SHIFT                 9
# define PGEN_PAT_CHESSBOARD_NORMAL_MODE_DP_ENC0_P0_MASK                 0x400
# define PGEN_PAT_CHESSBOARD_NORMAL_MODE_DP_ENC0_P0_SHIFT                10
# define PGEN_PAT_EXCHANGE_DP_ENC0_P0_MASK                               0x800
# define PGEN_PAT_EXCHANGE_DP_ENC0_P0_SHIFT                              11
# define PGEN_PAT_RGB_SUB_PIXEL_MASK_DP_ENC0_P0_MASK                     0x1000
# define PGEN_PAT_RGB_SUB_PIXEL_MASK_DP_ENC0_P0_SHIFT                    12

#define MTK_DP_ENC0_P0_31B4              (ENC0_OFFSET + 0x1B4)
# define PGEN_PAT_THICKNESS_DP_ENC0_P0_MASK                              0xf
# define PGEN_PAT_THICKNESS_DP_ENC0_P0_SHIFT                             0

#define MTK_DP_ENC0_P0_31C0              (ENC0_OFFSET + 0x1C0)
# define VIDEO_MUTE_CNT_THRD_DP_ENC0_P0_MASK                             0xfff
# define VIDEO_MUTE_CNT_THRD_DP_ENC0_P0_SHIFT                            0

#define MTK_DP_ENC0_P0_31C4              (ENC0_OFFSET + 0x1C4)
# define PPS_HW_BYPASS_MASK_DP_ENC0_P0_MASK                              0x800
# define PPS_HW_BYPASS_MASK_DP_ENC0_P0_SHIFT                             11
# define MST_EN_DP_ENC0_P0_MASK                                          0x1000
# define MST_EN_DP_ENC0_P0_SHIFT                                         12
# define DSC_BYPASS_EN_DP_ENC0_P0_MASK                                   0x2000
# define DSC_BYPASS_EN_DP_ENC0_P0_SHIFT                                  13
# define VSC_HW_BYPASS_MASK_DP_ENC0_P0_MASK                              0x4000
# define VSC_HW_BYPASS_MASK_DP_ENC0_P0_SHIFT                             14
# define HDR0_HW_BYPASS_MASK_DP_ENC0_P0_MASK                             0x8000
# define HDR0_HW_BYPASS_MASK_DP_ENC0_P0_SHIFT                            15

#define MTK_DP_ENC0_P0_31C8              (ENC0_OFFSET + 0x1C8)
# define VSC_EXT_VESA_HB0_DP_ENC0_P0_MASK                                0xff
# define VSC_EXT_VESA_HB0_DP_ENC0_P0_SHIFT                               0
# define VSC_EXT_VESA_HB1_DP_ENC0_P0_MASK                                0xff00
# define VSC_EXT_VESA_HB1_DP_ENC0_P0_SHIFT                               8

#define MTK_DP_ENC0_P0_31CC              (ENC0_OFFSET + 0x1CC)
# define VSC_EXT_VESA_HB2_DP_ENC0_P0_MASK                                0xff
# define VSC_EXT_VESA_HB2_DP_ENC0_P0_SHIFT                               0
# define VSC_EXT_VESA_HB3_DP_ENC0_P0_MASK                                0xff00
# define VSC_EXT_VESA_HB3_DP_ENC0_P0_SHIFT                               8

#define MTK_DP_ENC0_P0_31D0              (ENC0_OFFSET + 0x1D0)
# define VSC_EXT_CEA_HB0_DP_ENC0_P0_MASK                                 0xff
# define VSC_EXT_CEA_HB0_DP_ENC0_P0_SHIFT                                0
# define VSC_EXT_CEA_HB1_DP_ENC0_P0_MASK                                 0xff00
# define VSC_EXT_CEA_HB1_DP_ENC0_P0_SHIFT                                8

#define MTK_DP_ENC0_P0_31D4              (ENC0_OFFSET + 0x1D4)
# define VSC_EXT_CEA_HB2_DP_ENC0_P0_MASK                                 0xff
# define VSC_EXT_CEA_HB2_DP_ENC0_P0_SHIFT                                0
# define VSC_EXT_CEA_HB3_DP_ENC0_P0_MASK                                 0xff00
# define VSC_EXT_CEA_HB3_DP_ENC0_P0_SHIFT                                8

#define MTK_DP_ENC0_P0_31D8              (ENC0_OFFSET + 0x1D8)
# define VSC_EXT_VESA_NUM_DP_ENC0_P0_MASK                                0x3f
# define VSC_EXT_VESA_NUM_DP_ENC0_P0_SHIFT                               0
# define VSC_EXT_CEA_NUM_DP_ENC0_P0_MASK                                 0x3f00
# define VSC_EXT_CEA_NUM_DP_ENC0_P0_SHIFT                                8

#define MTK_DP_ENC0_P0_31DC              (ENC0_OFFSET + 0x1DC)
# define HDR0_CFG_DP_ENC0_P0_MASK                                        0xff
# define HDR0_CFG_DP_ENC0_P0_SHIFT                                       0
# define RESERVED_CFG_DP_ENC0_P0_MASK                                    0xff00
# define RESERVED_CFG_DP_ENC0_P0_SHIFT                                   8

#define MTK_DP_ENC0_P0_31E0              (ENC0_OFFSET + 0x1E0)
# define RESERVED_HB0_DP_ENC0_P0_MASK                                    0xff
# define RESERVED_HB0_DP_ENC0_P0_SHIFT                                   0
# define RESERVED_HB1_DP_ENC0_P0_MASK                                    0xff00
# define RESERVED_HB1_DP_ENC0_P0_SHIFT                                   8

#define MTK_DP_ENC0_P0_31E4              (ENC0_OFFSET + 0x1E4)
# define RESERVED_HB2_DP_ENC0_P0_MASK                                    0xff
# define RESERVED_HB2_DP_ENC0_P0_SHIFT                                   0
# define RESERVED_HB3_DP_ENC0_P0_MASK                                    0xff00
# define RESERVED_HB3_DP_ENC0_P0_SHIFT                                   8

#define MTK_DP_ENC0_P0_31E8              (ENC0_OFFSET + 0x1E8)
# define PPS_CFG_DP_ENC0_P0_MASK                                         0xff
# define PPS_CFG_DP_ENC0_P0_SHIFT                                        0
# define PPS_CFG_ONE_TIME_DP_ENC0_P0_MASK                                0x100
# define PPS_CFG_ONE_TIME_DP_ENC0_P0_SHIFT                               8
# define SDP_SPLIT_FIFO_READ_START_POINT_DP_ENC0_P0_MASK                 0xf000
# define SDP_SPLIT_FIFO_READ_START_POINT_DP_ENC0_P0_SHIFT                12

#define MTK_DP_ENC0_P0_31EC              (ENC0_OFFSET + 0x1EC)
# define VIDEO_M_CODE_FROM_DPRX_DP_ENC0_P0_MASK                          0x1
# define VIDEO_M_CODE_FROM_DPRX_DP_ENC0_P0_SHIFT                         0
# define MSA_MISC_FROM_DPRX_DP_ENC0_P0_MASK                              0x2
# define MSA_MISC_FROM_DPRX_DP_ENC0_P0_SHIFT                             1
# define ADS_CFG_DP_ENC0_P0_MASK                                         0x4
# define ADS_CFG_DP_ENC0_P0_SHIFT                                        2
# define ADS_MODE_DP_ENC0_P0_MASK                                        0x8
# define ADS_MODE_DP_ENC0_P0_SHIFT                                       3
# define AUDIO_CH_SRC_SEL_DP_ENC0_P0_MASK                                0x10
# define AUDIO_CH_SRC_SEL_DP_ENC0_P0_SHIFT                               4
# define ISRC1_HB3_DP_ENC0_P0_MASK                                       0xff00
# define ISRC1_HB3_DP_ENC0_P0_SHIFT                                      8

#define MTK_DP_ENC0_P0_31F0              (ENC0_OFFSET + 0x1F0)
# define ADS_HB0_DP_ENC0_P0_MASK                                         0xff
# define ADS_HB0_DP_ENC0_P0_SHIFT                                        0
# define ADS_HB1_DP_ENC0_P0_MASK                                         0xff00
# define ADS_HB1_DP_ENC0_P0_SHIFT                                        8

#define MTK_DP_ENC0_P0_31F8              (ENC0_OFFSET + 0x1F8)
# define ADS_HB2_DP_ENC0_P0_MASK                                         0xff
# define ADS_HB2_DP_ENC0_P0_SHIFT                                        0
# define ADS_HB3_DP_ENC0_P0_MASK                                         0xff00
# define ADS_HB3_DP_ENC0_P0_SHIFT                                        8

#define MTK_DP_ENC0_P0_31FC              (ENC0_OFFSET + 0x1FC)
# define VIDEO_ARBITER_DE_LAST_NUM0_SW_DP_ENC0_P0_MASK                   0x3
# define VIDEO_ARBITER_DE_LAST_NUM0_SW_DP_ENC0_P0_SHIFT                  0
# define VIDEO_ARBITER_DE_LAST_NUM1_SW_DP_ENC0_P0_MASK                   0xc
# define VIDEO_ARBITER_DE_LAST_NUM1_SW_DP_ENC0_P0_SHIFT                  2
# define VIDEO_ARBITER_DE_LAST_NUM2_SW_DP_ENC0_P0_MASK                   0x30
# define VIDEO_ARBITER_DE_LAST_NUM2_SW_DP_ENC0_P0_SHIFT                  4
# define VIDEO_ARBITER_DE_LAST_NUM3_SW_DP_ENC0_P0_MASK                   0xc0
# define VIDEO_ARBITER_DE_LAST_NUM3_SW_DP_ENC0_P0_SHIFT                  6
# define HDE_NUM_EVEN_EN_SW_LANE0_DP_ENC0_P0_MASK                        0x100
# define HDE_NUM_EVEN_EN_SW_LANE0_DP_ENC0_P0_SHIFT                       8
# define HDE_NUM_EVEN_EN_SW_LANE1_DP_ENC0_P0_MASK                        0x200
# define HDE_NUM_EVEN_EN_SW_LANE1_DP_ENC0_P0_SHIFT                       9
# define HDE_NUM_EVEN_EN_SW_LANE2_DP_ENC0_P0_MASK                        0x400
# define HDE_NUM_EVEN_EN_SW_LANE2_DP_ENC0_P0_SHIFT                       10
# define HDE_NUM_EVEN_EN_SW_LANE3_DP_ENC0_P0_MASK                        0x800
# define HDE_NUM_EVEN_EN_SW_LANE3_DP_ENC0_P0_SHIFT                       11
# define DE_LAST_NUM_SW_DP_ENC0_P0_MASK                                  0x1000
# define DE_LAST_NUM_SW_DP_ENC0_P0_SHIFT                                 12

#define MTK_DP_ENC1_P0_3200              (ENC1_OFFSET + 0x000)
# define SDP_DB0_DP_ENC1_P0_MASK                                         0xff
# define SDP_DB0_DP_ENC1_P0_SHIFT                                        0
# define SDP_DB1_DP_ENC1_P0_MASK                                         0xff00
# define SDP_DB1_DP_ENC1_P0_SHIFT                                        8

#define MTK_DP_ENC1_P0_3204              (ENC1_OFFSET + 0x004)
# define SDP_DB2_DP_ENC1_P0_MASK                                         0xff
# define SDP_DB2_DP_ENC1_P0_SHIFT                                        0
# define SDP_DB3_DP_ENC1_P0_MASK                                         0xff00
# define SDP_DB3_DP_ENC1_P0_SHIFT                                        8

#define MTK_DP_ENC1_P0_3208              (ENC1_OFFSET + 0x008)
# define SDP_DB4_DP_ENC1_P0_MASK                                         0xff
# define SDP_DB4_DP_ENC1_P0_SHIFT                                        0
# define SDP_DB5_DP_ENC1_P0_MASK                                         0xff00
# define SDP_DB5_DP_ENC1_P0_SHIFT                                        8

#define MTK_DP_ENC1_P0_320C              (ENC1_OFFSET + 0x00C)
# define SDP_DB6_DP_ENC1_P0_MASK                                         0xff
# define SDP_DB6_DP_ENC1_P0_SHIFT                                        0
# define SDP_DB7_DP_ENC1_P0_MASK                                         0xff00
# define SDP_DB7_DP_ENC1_P0_SHIFT                                        8

#define MTK_DP_ENC1_P0_3210              (ENC1_OFFSET + 0x010)
# define SDP_DB8_DP_ENC1_P0_MASK                                         0xff
# define SDP_DB8_DP_ENC1_P0_SHIFT                                        0
# define SDP_DB9_DP_ENC1_P0_MASK                                         0xff00
# define SDP_DB9_DP_ENC1_P0_SHIFT                                        8

#define MTK_DP_ENC1_P0_3214              (ENC1_OFFSET + 0x014)
# define SDP_DB10_DP_ENC1_P0_MASK                                        0xff
# define SDP_DB10_DP_ENC1_P0_SHIFT                                       0
# define SDP_DB11_DP_ENC1_P0_MASK                                        0xff00
# define SDP_DB11_DP_ENC1_P0_SHIFT                                       8

#define MTK_DP_ENC1_P0_3218              (ENC1_OFFSET + 0x018)
# define SDP_DB12_DP_ENC1_P0_MASK                                        0xff
# define SDP_DB12_DP_ENC1_P0_SHIFT                                       0
# define SDP_DB13_DP_ENC1_P0_MASK                                        0xff00
# define SDP_DB13_DP_ENC1_P0_SHIFT                                       8

#define MTK_DP_ENC1_P0_321C              (ENC1_OFFSET + 0x01C)
# define SDP_DB14_DP_ENC1_P0_MASK                                        0xff
# define SDP_DB14_DP_ENC1_P0_SHIFT                                       0
# define SDP_DB15_DP_ENC1_P0_MASK                                        0xff00
# define SDP_DB15_DP_ENC1_P0_SHIFT                                       8

#define MTK_DP_ENC1_P0_3220              (ENC1_OFFSET + 0x020)
# define SDP_DB16_DP_ENC1_P0_MASK                                        0xff
# define SDP_DB16_DP_ENC1_P0_SHIFT                                       0
# define SDP_DB17_DP_ENC1_P0_MASK                                        0xff00
# define SDP_DB17_DP_ENC1_P0_SHIFT                                       8

#define MTK_DP_ENC1_P0_3224              (ENC1_OFFSET + 0x024)
# define SDP_DB18_DP_ENC1_P0_MASK                                        0xff
# define SDP_DB18_DP_ENC1_P0_SHIFT                                       0
# define SDP_DB19_DP_ENC1_P0_MASK                                        0xff00
# define SDP_DB19_DP_ENC1_P0_SHIFT                                       8

#define MTK_DP_ENC1_P0_3228              (ENC1_OFFSET + 0x028)
# define SDP_DB20_DP_ENC1_P0_MASK                                        0xff
# define SDP_DB20_DP_ENC1_P0_SHIFT                                       0
# define SDP_DB21_DP_ENC1_P0_MASK                                        0xff00
# define SDP_DB21_DP_ENC1_P0_SHIFT                                       8

#define MTK_DP_ENC1_P0_322C              (ENC1_OFFSET + 0x02C)
# define SDP_DB22_DP_ENC1_P0_MASK                                        0xff
# define SDP_DB22_DP_ENC1_P0_SHIFT                                       0
# define SDP_DB23_DP_ENC1_P0_MASK                                        0xff00
# define SDP_DB23_DP_ENC1_P0_SHIFT                                       8

#define MTK_DP_ENC1_P0_3230              (ENC1_OFFSET + 0x030)
# define SDP_DB24_DP_ENC1_P0_MASK                                        0xff
# define SDP_DB24_DP_ENC1_P0_SHIFT                                       0
# define SDP_DB25_DP_ENC1_P0_MASK                                        0xff00
# define SDP_DB25_DP_ENC1_P0_SHIFT                                       8

#define MTK_DP_ENC1_P0_3234              (ENC1_OFFSET + 0x034)
# define SDP_DB26_DP_ENC1_P0_MASK                                        0xff
# define SDP_DB26_DP_ENC1_P0_SHIFT                                       0
# define SDP_DB27_DP_ENC1_P0_MASK                                        0xff00
# define SDP_DB27_DP_ENC1_P0_SHIFT                                       8

#define MTK_DP_ENC1_P0_3238              (ENC1_OFFSET + 0x038)
# define SDP_DB28_DP_ENC1_P0_MASK                                        0xff
# define SDP_DB28_DP_ENC1_P0_SHIFT                                       0
# define SDP_DB29_DP_ENC1_P0_MASK                                        0xff00
# define SDP_DB29_DP_ENC1_P0_SHIFT                                       8

#define MTK_DP_ENC1_P0_323C              (ENC1_OFFSET + 0x03C)
# define SDP_DB30_DP_ENC1_P0_MASK                                        0xff
# define SDP_DB30_DP_ENC1_P0_SHIFT                                       0
# define SDP_DB31_DP_ENC1_P0_MASK                                        0xff00
# define SDP_DB31_DP_ENC1_P0_SHIFT                                       8

#define MTK_DP_ENC1_P0_3240              (ENC1_OFFSET + 0x040)
# define SDP_DB0_R_DP_ENC1_P0_MASK                                       0xff
# define SDP_DB0_R_DP_ENC1_P0_SHIFT                                      0
# define SDP_DB1_R_DP_ENC1_P0_MASK                                       0xff00
# define SDP_DB1_R_DP_ENC1_P0_SHIFT                                      8

#define MTK_DP_ENC1_P0_3244              (ENC1_OFFSET + 0x044)
# define SDP_DB2_R_DP_ENC1_P0_MASK                                       0xff
# define SDP_DB2_R_DP_ENC1_P0_SHIFT                                      0
# define SDP_DB3_R_DP_ENC1_P0_MASK                                       0xff00
# define SDP_DB3_R_DP_ENC1_P0_SHIFT                                      8

#define MTK_DP_ENC1_P0_3248              (ENC1_OFFSET + 0x048)
# define SDP_DB4_R_DP_ENC1_P0_MASK                                       0xff
# define SDP_DB4_R_DP_ENC1_P0_SHIFT                                      0
# define SDP_DB5_R_DP_ENC1_P0_MASK                                       0xff00
# define SDP_DB5_R_DP_ENC1_P0_SHIFT                                      8

#define MTK_DP_ENC1_P0_324C              (ENC1_OFFSET + 0x04C)
# define SDP_DB6_R_DP_ENC1_P0_MASK                                       0xff
# define SDP_DB6_R_DP_ENC1_P0_SHIFT                                      0
# define SDP_DB7_R_DP_ENC1_P0_MASK                                       0xff00
# define SDP_DB7_R_DP_ENC1_P0_SHIFT                                      8

#define MTK_DP_ENC1_P0_3250              (ENC1_OFFSET + 0x050)
# define SDP_DB8_R_DP_ENC1_P0_MASK                                       0xff
# define SDP_DB8_R_DP_ENC1_P0_SHIFT                                      0
# define SDP_DB9_R_DP_ENC1_P0_MASK                                       0xff00
# define SDP_DB9_R_DP_ENC1_P0_SHIFT                                      8

#define MTK_DP_ENC1_P0_3254              (ENC1_OFFSET + 0x054)
# define SDP_DB10_R_DP_ENC1_P0_MASK                                      0xff
# define SDP_DB10_R_DP_ENC1_P0_SHIFT                                     0
# define SDP_DB11_R_DP_ENC1_P0_MASK                                      0xff00
# define SDP_DB11_R_DP_ENC1_P0_SHIFT                                     8

#define MTK_DP_ENC1_P0_3258              (ENC1_OFFSET + 0x058)
# define SDP_DB12_R_DP_ENC1_P0_MASK                                      0xff
# define SDP_DB12_R_DP_ENC1_P0_SHIFT                                     0
# define SDP_DB13_R_DP_ENC1_P0_MASK                                      0xff00
# define SDP_DB13_R_DP_ENC1_P0_SHIFT                                     8

#define MTK_DP_ENC1_P0_325C              (ENC1_OFFSET + 0x05C)
# define SDP_DB14_R_DP_ENC1_P0_MASK                                      0xff
# define SDP_DB14_R_DP_ENC1_P0_SHIFT                                     0
# define SDP_DB15_R_DP_ENC1_P0_MASK                                      0xff00
# define SDP_DB15_R_DP_ENC1_P0_SHIFT                                     8

#define MTK_DP_ENC1_P0_3260              (ENC1_OFFSET + 0x060)
# define SDP_DB16_R_DP_ENC1_P0_MASK                                      0xff
# define SDP_DB16_R_DP_ENC1_P0_SHIFT                                     0
# define SDP_DB17_R_DP_ENC1_P0_MASK                                      0xff00
# define SDP_DB17_R_DP_ENC1_P0_SHIFT                                     8

#define MTK_DP_ENC1_P0_3264              (ENC1_OFFSET + 0x064)
# define SDP_DB18_R_DP_ENC1_P0_MASK                                      0xff
# define SDP_DB18_R_DP_ENC1_P0_SHIFT                                     0
# define SDP_DB19_R_DP_ENC1_P0_MASK                                      0xff00
# define SDP_DB19_R_DP_ENC1_P0_SHIFT                                     8

#define MTK_DP_ENC1_P0_3268              (ENC1_OFFSET + 0x068)
# define SDP_DB20_R_DP_ENC1_P0_MASK                                      0xff
# define SDP_DB20_R_DP_ENC1_P0_SHIFT                                     0
# define SDP_DB21_R_DP_ENC1_P0_MASK                                      0xff00
# define SDP_DB21_R_DP_ENC1_P0_SHIFT                                     8

#define MTK_DP_ENC1_P0_326C              (ENC1_OFFSET + 0x06C)
# define SDP_DB22_R_DP_ENC1_P0_MASK                                      0xff
# define SDP_DB22_R_DP_ENC1_P0_SHIFT                                     0
# define SDP_DB23_R_DP_ENC1_P0_MASK                                      0xff00
# define SDP_DB23_R_DP_ENC1_P0_SHIFT                                     8

#define MTK_DP_ENC1_P0_3270              (ENC1_OFFSET + 0x070)
# define SDP_DB24_R_DP_ENC1_P0_MASK                                      0xff
# define SDP_DB24_R_DP_ENC1_P0_SHIFT                                     0
# define SDP_DB25_R_DP_ENC1_P0_MASK                                      0xff00
# define SDP_DB25_R_DP_ENC1_P0_SHIFT                                     8

#define MTK_DP_ENC1_P0_3274              (ENC1_OFFSET + 0x074)
# define SDP_DB26_R_DP_ENC1_P0_MASK                                      0xff
# define SDP_DB26_R_DP_ENC1_P0_SHIFT                                     0
# define SDP_DB27_R_DP_ENC1_P0_MASK                                      0xff00
# define SDP_DB27_R_DP_ENC1_P0_SHIFT                                     8

#define MTK_DP_ENC1_P0_3278              (ENC1_OFFSET + 0x078)
# define SDP_DB28_R_DP_ENC1_P0_MASK                                      0xff
# define SDP_DB28_R_DP_ENC1_P0_SHIFT                                     0
# define SDP_DB29_R_DP_ENC1_P0_MASK                                      0xff00
# define SDP_DB29_R_DP_ENC1_P0_SHIFT                                     8

#define MTK_DP_ENC1_P0_327C              (ENC1_OFFSET + 0x07C)
# define SDP_DB30_R_DP_ENC1_P0_MASK                                      0xff
# define SDP_DB30_R_DP_ENC1_P0_SHIFT                                     0
# define SDP_DB31_R_DP_ENC1_P0_MASK                                      0xff00
# define SDP_DB31_R_DP_ENC1_P0_SHIFT                                     8

#define MTK_DP_ENC1_P0_3280              (ENC1_OFFSET + 0x080)
# define SDP_PACKET_TYPE_DP_ENC1_P0_MASK                                 0x1f
# define SDP_PACKET_TYPE_DP_ENC1_P0_SHIFT                                0
# define SDP_PACKET_W_DP_ENC1_P0                                         0x20
# define SDP_PACKET_W_DP_ENC1_P0_MASK                                    0x20
# define SDP_PACKET_W_DP_ENC1_P0_SHIFT                                   5
# define SDP_PACKET_R_DP_ENC1_P0_MASK                                    0x40
# define SDP_PACKET_R_DP_ENC1_P0_SHIFT                                   6

#define MTK_DP_ENC1_P0_328C              (ENC1_OFFSET + 0x08C)
# define VSC_SW_HW_SEL_VESA_DP_ENC1_P0_MASK                              0x1
# define VSC_SW_HW_SEL_VESA_DP_ENC1_P0_SHIFT                             0
# define VSC_SRAM_HW_RST_VESA_DP_ENC1_P0_MASK                            0x2
# define VSC_SRAM_HW_RST_VESA_DP_ENC1_P0_SHIFT                           1
# define VSC_SRAM_SW_RST_VESA_DP_ENC1_P0_MASK                            0x4
# define VSC_SRAM_SW_RST_VESA_DP_ENC1_P0_SHIFT                           2
# define VSC_SRAM_HW_EMPTY_VESA_DP_ENC1_P0_MASK                          0x8
# define VSC_SRAM_HW_EMPTY_VESA_DP_ENC1_P0_SHIFT                         3
# define VSC_SRAM_HW_FULL_VESA_DP_ENC1_P0_MASK                           0x10
# define VSC_SRAM_HW_FULL_VESA_DP_ENC1_P0_SHIFT                          4
# define VSC_SRAM_HW_FULL_CLR_VESA_DP_ENC1_P0_MASK                       0x20
# define VSC_SRAM_HW_FULL_CLR_VESA_DP_ENC1_P0_SHIFT                      5
# define VSC_DATA_TOGGLE_VESA_DP_ENC1_P0_MASK                            0x40
# define VSC_DATA_TOGGLE_VESA_DP_ENC1_P0_SHIFT                           6
# define VSC_DATA_RDY_VESA_DP_ENC1_P0_MASK                               0x80
# define VSC_DATA_RDY_VESA_DP_ENC1_P0_SHIFT                              7
# define VSC_SRAM_SW_EMPTY_VESA_DP_ENC1_P0_MASK                          0x100
# define VSC_SRAM_SW_EMPTY_VESA_DP_ENC1_P0_SHIFT                         8

#define MTK_DP_ENC1_P0_3290              (ENC1_OFFSET + 0x090)
# define VSC_DATA_BYTE0_VESA_DP_ENC1_P0_MASK                             0xff
# define VSC_DATA_BYTE0_VESA_DP_ENC1_P0_SHIFT                            0
# define VSC_DATA_BYTE1_VESA_DP_ENC1_P0_MASK                             0xff00
# define VSC_DATA_BYTE1_VESA_DP_ENC1_P0_SHIFT                            8

#define MTK_DP_ENC1_P0_3294              (ENC1_OFFSET + 0x094)
# define VSC_DATA_BYTE2_VESA_DP_ENC1_P0_MASK                             0xff
# define VSC_DATA_BYTE2_VESA_DP_ENC1_P0_SHIFT                            0
# define VSC_DATA_BYTE3_VESA_DP_ENC1_P0_MASK                             0xff00
# define VSC_DATA_BYTE3_VESA_DP_ENC1_P0_SHIFT                            8

#define MTK_DP_ENC1_P0_3298              (ENC1_OFFSET + 0x098)
# define VSC_DATA_BYTE4_VESA_DP_ENC1_P0_MASK                             0xff
# define VSC_DATA_BYTE4_VESA_DP_ENC1_P0_SHIFT                            0
# define VSC_DATA_BYTE5_VESA_DP_ENC1_P0_MASK                             0xff00
# define VSC_DATA_BYTE5_VESA_DP_ENC1_P0_SHIFT                            8

#define MTK_DP_ENC1_P0_329C              (ENC1_OFFSET + 0x09C)
# define VSC_DATA_BYTE6_VESA_DP_ENC1_P0_MASK                             0xff
# define VSC_DATA_BYTE6_VESA_DP_ENC1_P0_SHIFT                            0
# define VSC_DATA_BYTE7_VESA_DP_ENC1_P0_MASK                             0xff00
# define VSC_DATA_BYTE7_VESA_DP_ENC1_P0_SHIFT                            8

#define MTK_DP_ENC1_P0_32A0              (ENC1_OFFSET + 0x0A0)
# define VSC_SW_HW_SEL_CEA_DP_ENC1_P0_MASK                               0x1
# define VSC_SW_HW_SEL_CEA_DP_ENC1_P0_SHIFT                              0
# define VSC_SRAM_HW_RST_CEA_DP_ENC1_P0_MASK                             0x2
# define VSC_SRAM_HW_RST_CEA_DP_ENC1_P0_SHIFT                            1
# define VSC_SRAM_SW_RST_CEA_DP_ENC1_P0_MASK                             0x4
# define VSC_SRAM_SW_RST_CEA_DP_ENC1_P0_SHIFT                            2
# define VSC_SRAM_HW_EMPTY_CEA_DP_ENC1_P0_MASK                           0x8
# define VSC_SRAM_HW_EMPTY_CEA_DP_ENC1_P0_SHIFT                          3
# define VSC_SRAM_HW_FULL_CEA_DP_ENC1_P0_MASK                            0x10
# define VSC_SRAM_HW_FULL_CEA_DP_ENC1_P0_SHIFT                           4
# define VSC_SRAM_HW_FULL_CLR_CEA_DP_ENC1_P0_MASK                        0x20
# define VSC_SRAM_HW_FULL_CLR_CEA_DP_ENC1_P0_SHIFT                       5
# define VSC_DATA_TOGGLE_CEA_DP_ENC1_P0_MASK                             0x40
# define VSC_DATA_TOGGLE_CEA_DP_ENC1_P0_SHIFT                            6
# define VSC_DATA_RDY_CEA_DP_ENC1_P0_MASK                                0x80
# define VSC_DATA_RDY_CEA_DP_ENC1_P0_SHIFT                               7
# define VSC_SRAM_SW_EMPTY_CEA_DP_ENC1_P0_MASK                           0x100
# define VSC_SRAM_SW_EMPTY_CEA_DP_ENC1_P0_SHIFT                          8

#define MTK_DP_ENC1_P0_32A4              (ENC1_OFFSET + 0x0A4)
# define VSC_DATA_BYTE0_CEA_DP_ENC1_P0_MASK                              0xff
# define VSC_DATA_BYTE0_CEA_DP_ENC1_P0_SHIFT                             0
# define VSC_DATA_BYTE1_CEA_DP_ENC1_P0_MASK                              0xff00
# define VSC_DATA_BYTE1_CEA_DP_ENC1_P0_SHIFT                             8

#define MTK_DP_ENC1_P0_32A8              (ENC1_OFFSET + 0x0A8)
# define VSC_DATA_BYTE2_CEA_DP_ENC1_P0_MASK                              0xff
# define VSC_DATA_BYTE2_CEA_DP_ENC1_P0_SHIFT                             0
# define VSC_DATA_BYTE3_CEA_DP_ENC1_P0_MASK                              0xff00
# define VSC_DATA_BYTE3_CEA_DP_ENC1_P0_SHIFT                             8

#define MTK_DP_ENC1_P0_32AC              (ENC1_OFFSET + 0x0AC)
# define VSC_DATA_BYTE4_CEA_DP_ENC1_P0_MASK                              0xff
# define VSC_DATA_BYTE4_CEA_DP_ENC1_P0_SHIFT                             0
# define VSC_DATA_BYTE5_CEA_DP_ENC1_P0_MASK                              0xff00
# define VSC_DATA_BYTE5_CEA_DP_ENC1_P0_SHIFT                             8

#define MTK_DP_ENC1_P0_32B0              (ENC1_OFFSET + 0x0B0)
# define VSC_DATA_BYTE6_CEA_DP_ENC1_P0_MASK                              0xff
# define VSC_DATA_BYTE6_CEA_DP_ENC1_P0_SHIFT                             0
# define VSC_DATA_BYTE7_CEA_DP_ENC1_P0_MASK                              0xff00
# define VSC_DATA_BYTE7_CEA_DP_ENC1_P0_SHIFT                             8

#define MTK_DP_ENC1_P0_32B4              (ENC1_OFFSET + 0x0B4)
# define VSC_DATA_SW_CAN_WRITE_VESA_DP_ENC1_P0_MASK                      0x1
# define VSC_DATA_SW_CAN_WRITE_VESA_DP_ENC1_P0_SHIFT                     0
# define VSC_DATA_SW_CAN_WRITE_CEA_DP_ENC1_P0_MASK                       0x2
# define VSC_DATA_SW_CAN_WRITE_CEA_DP_ENC1_P0_SHIFT                      1
# define VSC_DATA_TRANSMIT_SEL_VESA_DP_ENC1_P0_MASK                      0x4
# define VSC_DATA_TRANSMIT_SEL_VESA_DP_ENC1_P0_SHIFT                     2
# define VSC_DATA_TRANSMIT_SEL_CEA_DP_ENC1_P0_MASK                       0x8
# define VSC_DATA_TRANSMIT_SEL_CEA_DP_ENC1_P0_SHIFT                      3

#define MTK_DP_ENC1_P0_32C0              (ENC1_OFFSET + 0x0C0)
# define IRQ_MASK_DP_ENC1_P0_MASK                                        0xffff
# define IRQ_MASK_DP_ENC1_P0_SHIFT                                       0

#define MTK_DP_ENC1_P0_32C4              (ENC1_OFFSET + 0x0C4)
# define IRQ_CLR_DP_ENC1_P0_MASK                                         0xffff
# define IRQ_CLR_DP_ENC1_P0_SHIFT                                        0

#define MTK_DP_ENC1_P0_32C8              (ENC1_OFFSET + 0x0C8)
# define IRQ_FORCE_DP_ENC1_P0_MASK                                       0xffff
# define IRQ_FORCE_DP_ENC1_P0_SHIFT                                      0

#define MTK_DP_ENC1_P0_32CC              (ENC1_OFFSET + 0x0CC)
# define IRQ_STATUS_DP_ENC1_P0_MASK                                      0xffff
# define IRQ_STATUS_DP_ENC1_P0_SHIFT                                     0

#define MTK_DP_ENC1_P0_32D0              (ENC1_OFFSET + 0x0D0)
# define IRQ_FINAL_STATUS_DP_ENC1_P0_MASK                                0xffff
# define IRQ_FINAL_STATUS_DP_ENC1_P0_SHIFT                               0

#define MTK_DP_ENC1_P0_32D4              (ENC1_OFFSET + 0x0D4)
# define IRQ_MASK_51_DP_ENC1_P0_MASK                                     0xffff
# define IRQ_MASK_51_DP_ENC1_P0_SHIFT                                    0

#define MTK_DP_ENC1_P0_32D8              (ENC1_OFFSET + 0x0D8)
# define IRQ_CLR_51_DP_ENC1_P0_MASK                                      0xffff
# define IRQ_CLR_51_DP_ENC1_P0_SHIFT                                     0

#define MTK_DP_ENC1_P0_32DC              (ENC1_OFFSET + 0x0DC)
# define IRQ_FORCE_51_DP_ENC1_P0_MASK                                    0xffff
# define IRQ_FORCE_51_DP_ENC1_P0_SHIFT                                   0

#define MTK_DP_ENC1_P0_32E0              (ENC1_OFFSET + 0x0E0)
# define IRQ_STATUS_51_DP_ENC1_P0_MASK                                   0xffff
# define IRQ_STATUS_51_DP_ENC1_P0_SHIFT                                  0

#define MTK_DP_ENC1_P0_32E4              (ENC1_OFFSET + 0x0E4)
# define IRQ_FINAL_STATUS_51_DP_ENC1_P0_MASK                             0xffff
# define IRQ_FINAL_STATUS_51_DP_ENC1_P0_SHIFT                            0

#define MTK_DP_ENC1_P0_32E8              (ENC1_OFFSET + 0x0E8)
# define AUDIO_SRAM_WRITE_ADDR_0_DP_ENC1_P0_MASK                         0x7f
# define AUDIO_SRAM_WRITE_ADDR_0_DP_ENC1_P0_SHIFT                        0
# define AUDIO_SRAM_WRITE_ADDR_1_DP_ENC1_P0_MASK                         0x7f00
# define AUDIO_SRAM_WRITE_ADDR_1_DP_ENC1_P0_SHIFT                        8

#define MTK_DP_ENC1_P0_32EC              (ENC1_OFFSET + 0x0EC)
# define AUDIO_SRAM_WRITE_ADDR_2_DP_ENC1_P0_MASK                         0x7f
# define AUDIO_SRAM_WRITE_ADDR_2_DP_ENC1_P0_SHIFT                        0
# define AUDIO_SRAM_WRITE_ADDR_3_DP_ENC1_P0_MASK                         0x7f00
# define AUDIO_SRAM_WRITE_ADDR_3_DP_ENC1_P0_SHIFT                        8

#define MTK_DP_ENC1_P0_32F0              (ENC1_OFFSET + 0x0F0)
# define M_CODE_FEC_MERGE_0_DP_ENC1_P0_MASK                              0xffff
# define M_CODE_FEC_MERGE_0_DP_ENC1_P0_SHIFT                             0

#define MTK_DP_ENC1_P0_32F4              (ENC1_OFFSET + 0x0F4)
# define M_CODE_FEC_MERGE_1_DP_ENC1_P0_MASK                              0xff
# define M_CODE_FEC_MERGE_1_DP_ENC1_P0_SHIFT                             0

#define MTK_DP_ENC1_P0_32F8              (ENC1_OFFSET + 0x0F8)
# define MSA_UPDATE_LINE_CNT_THRD_DP_ENC1_P0_MASK                        0xff
# define MSA_UPDATE_LINE_CNT_THRD_DP_ENC1_P0_SHIFT                       0
# define SDP_SPLIT_BUG_FIX_DP_ENC1_P0_MASK                               0x200
# define SDP_SPLIT_BUG_FIX_DP_ENC1_P0_SHIFT                              9
# define MSA_MUTE_MASK_DP_ENC1_P0_MASK                                   0x400
# define MSA_MUTE_MASK_DP_ENC1_P0_SHIFT                                  10
# define MSA_UPDATE_SEL_DP_ENC1_P0_MASK                                  0x3000
# define MSA_UPDATE_SEL_DP_ENC1_P0_SHIFT                                 12
# define VIDEO_MUTE_TOGGLE_SEL_DP_ENC1_P0_MASK                           0xc000
# define VIDEO_MUTE_TOGGLE_SEL_DP_ENC1_P0_SHIFT                          14

#define MTK_DP_ENC1_P0_3300              (ENC1_OFFSET + 0x100)
# define AUDIO_AFIFO_CNT_SEL_DP_ENC1_P0_MASK                             0x1
# define AUDIO_AFIFO_CNT_SEL_DP_ENC1_P0_SHIFT                            0
# define AUDIO_SRAM_CNT_SEL_DP_ENC1_P0_MASK                              0x2
# define AUDIO_SRAM_CNT_SEL_DP_ENC1_P0_SHIFT                             1
# define AUDIO_AFIFO_CNT_DP_ENC1_P0_MASK                                 0xf0
# define AUDIO_AFIFO_CNT_DP_ENC1_P0_SHIFT                                4
# define VIDEO_AFIFO_RDY_SEL_DP_ENC1_P0_MASK                             0x300
# define VIDEO_AFIFO_RDY_SEL_DP_ENC1_P0_SHIFT                            8

#define MTK_DP_ENC1_P0_3304              (ENC1_OFFSET + 0x104)
# define AUDIO_SRAM_CNT_DP_ENC1_P0_MASK                                  0x7f
# define AUDIO_SRAM_CNT_DP_ENC1_P0_SHIFT                                 0
# define AU_PRTY_REGEN_DP_ENC1_P0_MASK                                   0x100
# define AU_PRTY_REGEN_DP_ENC1_P0_SHIFT                                  8
# define AU_CH_STS_REGEN_DP_ENC1_P0_MASK                                 0x200
# define AU_CH_STS_REGEN_DP_ENC1_P0_SHIFT                                9
# define AUDIO_VALIDITY_REGEN_DP_ENC1_P0_MASK                            0x400
# define AUDIO_VALIDITY_REGEN_DP_ENC1_P0_SHIFT                           10
# define AUDIO_RESERVED_REGEN_DP_ENC1_P0_MASK                            0x800
# define AUDIO_RESERVED_REGEN_DP_ENC1_P0_SHIFT                           11
# define AUDIO_SAMPLE_PRSENT_REGEN_DP_ENC1_P0_MASK                       0x1000
# define AUDIO_SAMPLE_PRSENT_REGEN_DP_ENC1_P0_SHIFT                      12

#define MTK_DP_ENC1_P0_3320              (ENC1_OFFSET + 0x120)
# define AUDIO_PATTERN_GEN_DSTB_CNT_THRD_DP_ENC1_P0_MASK                 0x1ff
# define AUDIO_PATTERN_GEN_DSTB_CNT_THRD_DP_ENC1_P0_SHIFT                0

#define MTK_DP_ENC1_P0_3324              (ENC1_OFFSET + 0x124)
# define AUDIO_SOURCE_MUX_DP_ENC1_P0_MASK                                0x300
# define AUDIO_SOURCE_MUX_DP_ENC1_P0_SHIFT                               8
# define AUDIO_SOURCE_MUX_DP_ENC1_P0_DPRX                                (0 << AUDIO_SOURCE_MUX_DP_ENC1_P0_SHIFT)
# define AUDIO_SOURCE_MUX_DP_ENC1_P0_HDMIRX                              (1 << AUDIO_SOURCE_MUX_DP_ENC1_P0_SHIFT)
# define AUDIO_SOURCE_MUX_DP_ENC1_P0_HAYDYN                              (2 << AUDIO_SOURCE_MUX_DP_ENC1_P0_SHIFT)
# define AUDIO_PATTERN_GEN_CH_NUM_DP_ENC1_P0_MASK                        0x3000
# define AUDIO_PATGEN_CH_NUM_DP_ENC1_P0_SHIFT                            12
# define AUDIO_PATTERN_GEN_FS_SEL_DP_ENC1_P0_MASK                        0xc000
# define AUDIO_PATGEN_FS_SEL_DP_ENC1_P0_SHIFT                            14

#define MTK_DP_ENC1_P0_3328              (ENC1_OFFSET + 0x128)
# define VSYNC_DETECT_POL_DP_ENC1_P0_MASK                                0x1
# define VSYNC_DETECT_POL_DP_ENC1_P0_SHIFT                               0
# define HSYNC_DETECT_POL_DP_ENC1_P0_MASK                                0x2
# define HSYNC_DETECT_POL_DP_ENC1_P0_SHIFT                               1
# define HTOTAL_DETECT_STABLE_DP_ENC1_P0_MASK                            0x4
# define HTOTAL_DETECT_STABLE_DP_ENC1_P0_SHIFT                           2
# define HDE_DETECT_STABLE_DP_ENC1_P0_MASK                               0x8
# define HDE_DETECT_STABLE_DP_ENC1_P0_SHIFT                              3

#define MTK_DP_ENC1_P0_332C              (ENC1_OFFSET + 0x12C)
# define VTOTAL_DETECT_DP_ENC1_P0_MASK                                   0xffff
# define VTOTAL_DETECT_DP_ENC1_P0_SHIFT                                  0

#define MTK_DP_ENC1_P0_3330              (ENC1_OFFSET + 0x130)
# define VDE_DETECT_DP_ENC1_P0_MASK                                      0xffff
# define VDE_DETECT_DP_ENC1_P0_SHIFT                                     0

#define MTK_DP_ENC1_P0_3334              (ENC1_OFFSET + 0x134)
# define HTOTAL_DETECT_DP_ENC1_P0_MASK                                   0xffff
# define HTOTAL_DETECT_DP_ENC1_P0_SHIFT                                  0

#define MTK_DP_ENC1_P0_3338              (ENC1_OFFSET + 0x138)
# define HDE_DETECT_DP_ENC1_P0_MASK                                      0xffff
# define HDE_DETECT_DP_ENC1_P0_SHIFT                                     0

#define MTK_DP_ENC1_P0_3340              (ENC1_OFFSET + 0x140)
# define BIST_FAIL_VIDEO_L0_DP_ENC1_P0_MASK                              0x1
# define BIST_FAIL_VIDEO_L0_DP_ENC1_P0_SHIFT                             0
# define BIST_FAIL_VIDEO_L1_DP_ENC1_P0_MASK                              0x2
# define BIST_FAIL_VIDEO_L1_DP_ENC1_P0_SHIFT                             1
# define BIST_FAIL_VIDEO_L2_DP_ENC1_P0_MASK                              0x4
# define BIST_FAIL_VIDEO_L2_DP_ENC1_P0_SHIFT                             2
# define BIST_FAIL_VIDEO_L3_DP_ENC1_P0_MASK                              0x8
# define BIST_FAIL_VIDEO_L3_DP_ENC1_P0_SHIFT                             3
# define BIST_FAIL_AUDIO_L0_DP_ENC1_P0_MASK                              0x10
# define BIST_FAIL_AUDIO_L0_DP_ENC1_P0_SHIFT                             4
# define BIST_FAIL_AUDIO_L1_DP_ENC1_P0_MASK                              0x20
# define BIST_FAIL_AUDIO_L1_DP_ENC1_P0_SHIFT                             5
# define BIST_FAIL_AUDIO_L2_DP_ENC1_P0_MASK                              0x40
# define BIST_FAIL_AUDIO_L2_DP_ENC1_P0_SHIFT                             6
# define BIST_FAIL_AUDIO_L3_DP_ENC1_P0_MASK                              0x80
# define BIST_FAIL_AUDIO_L3_DP_ENC1_P0_SHIFT                             7
# define BIST_FAIL_VSC_VESA_HW_DP_ENC1_P0_MASK                           0x100
# define BIST_FAIL_VSC_VESA_HW_DP_ENC1_P0_SHIFT                          8
# define BIST_FAIL_VSC_CEA_HW_DP_ENC1_P0_MASK                            0x200
# define BIST_FAIL_VSC_CEA_HW_DP_ENC1_P0_SHIFT                           9
# define BIST_FAIL_VSC_VESA_SW_DP_ENC1_P0_MASK                           0x400
# define BIST_FAIL_VSC_VESA_SW_DP_ENC1_P0_SHIFT                          10
# define BIST_FAIL_VSC_CEA_SW_DP_ENC1_P0_MASK                            0x800
# define BIST_FAIL_VSC_CEA_SW_DP_ENC1_P0_SHIFT                           11
# define LR_FIELD_SYNC_SEL_DP_ENC1_P0_MASK                               0x7000
# define LR_FIELD_SYNC_SEL_DP_ENC1_P0_SHIFT                              12

#define MTK_DP_ENC1_P0_3344              (ENC1_OFFSET + 0x144)
# define DP_CH1_MATRIX_DP_ENC1_P0_MASK                                   0x1f
# define DP_CH1_MATRIX_DP_ENC1_P0_SHIFT                                  0
# define DP_CH2_MATRIX_DP_ENC1_P0_MASK                                   0x1f00
# define DP_CH2_MATRIX_DP_ENC1_P0_SHIFT                                  8

#define MTK_DP_ENC1_P0_3348              (ENC1_OFFSET + 0x148)
# define DP_CH3_MATRIX_DP_ENC1_P0_MASK                                   0x1f
# define DP_CH3_MATRIX_DP_ENC1_P0_SHIFT                                  0
# define DP_CH4_MATRIX_DP_ENC1_P0_MASK                                   0x1f00
# define DP_CH4_MATRIX_DP_ENC1_P0_SHIFT                                  8

#define MTK_DP_ENC1_P0_334C              (ENC1_OFFSET + 0x14C)
# define DP_CH5_MATRIX_DP_ENC1_P0_MASK                                   0x1f
# define DP_CH5_MATRIX_DP_ENC1_P0_SHIFT                                  0
# define DP_CH6_MATRIX_DP_ENC1_P0_MASK                                   0x1f00
# define DP_CH6_MATRIX_DP_ENC1_P0_SHIFT                                  8

#define MTK_DP_ENC1_P0_3350              (ENC1_OFFSET + 0x150)
# define DP_CH7_MATRIX_DP_ENC1_P0_MASK                                   0x1f
# define DP_CH7_MATRIX_DP_ENC1_P0_SHIFT                                  0
# define DP_CH8_MATRIX_DP_ENC1_P0_MASK                                   0x1f00
# define DP_CH8_MATRIX_DP_ENC1_P0_SHIFT                                  8

#define MTK_DP_ENC1_P0_3354              (ENC1_OFFSET + 0x154)
# define DP_S2P_LAUNCH_CFG_DP_ENC1_P0_MASK                               0x7f
# define DP_S2P_LAUNCH_CFG_DP_ENC1_P0_SHIFT                              0
# define AUDIO_HAYDN_EN_FORCE_DP_ENC1_P0_MASK                            0x1000
# define AUDIO_HAYDN_EN_FORCE_DP_ENC1_P0_SHIFT                           12
# define AUDIO_HAYDN_FORMAT_DP_ENC1_P0_MASK                              0xf00
# define AUDIO_HAYDN_FORMAT_DP_ENC1_P0_SHIFT                             8

#define MTK_DP_ENC1_P0_3358              (ENC1_OFFSET + 0x158)
# define TU_SIZE_DP_ENC1_P0_MASK                                         0x7f
# define TU_SIZE_DP_ENC1_P0_SHIFT                                        0
# define TU_CALC_SW_DP_ENC1_P0_MASK                                      0x80
# define TU_CALC_SW_DP_ENC1_P0_SHIFT                                     7

#define MTK_DP_ENC1_P0_335C              (ENC1_OFFSET + 0x15C)
# define SYMBOL_DATA_PER_TU_SW_0_DP_ENC1_P0_MASK                         0xffff
# define SYMBOL_DATA_PER_TU_SW_0_DP_ENC1_P0_SHIFT                        0

#define MTK_DP_ENC1_P0_3360              (ENC1_OFFSET + 0x160)
# define SYMBOL_DATA_PER_TU_SW_1_DP_ENC1_P0_MASK                         0x7fff
# define SYMBOL_DATA_PER_TU_SW_1_DP_ENC1_P0_SHIFT                        0

#define MTK_DP_ENC1_P0_3364              (ENC1_OFFSET + 0x164)
# define SDP_DOWN_CNT_INIT_IN_HBLANK_DP_ENC1_P0_MASK                     0xfff
# define SDP_DOWN_CNT_INIT_IN_HBLANK_DP_ENC1_P0_SHIFT                    0
# define FIFO_READ_START_POINT_DP_ENC1_P0_MASK                           0xf000
# define FIFO_READ_START_POINT_DP_ENC1_P0_SHIFT                          12

#define MTK_DP_ENC1_P0_3368              (ENC1_OFFSET + 0x168)
# define VIDEO_SRAM_FIFO_CNT_RESET_SEL_DP_ENC1_P0_MASK                   0x3
# define VIDEO_SRAM_FIFO_CNT_RESET_SEL_DP_ENC1_P0_SHIFT                  0
# define VIDEO_STABLE_EN_DP_ENC1_P0_MASK                                 0x4
# define VIDEO_STABLE_EN_DP_ENC1_P0_SHIFT                                2
# define VIDEO_STABLE_CNT_THRD_DP_ENC1_P0_MASK                           0xf0
# define VIDEO_STABLE_CNT_THRD_DP_ENC1_P0_SHIFT                          4
# define SDP_DP13_EN_DP_ENC1_P0_MASK                                     0x100
# define SDP_DP13_EN_DP_ENC1_P0_SHIFT                                    8
# define VIDEO_PIXEL_SWAP_DP_ENC1_P0_MASK                                0x600
# define VIDEO_PIXEL_SWAP_DP_ENC1_P0_SHIFT                               9
# define BS2BS_MODE_DP_ENC1_P0_MASK                                      0x3000
# define BS2BS_MODE_DP_ENC1_P0_SHIFT                                     12

#define MTK_DP_ENC1_P0_336C              (ENC1_OFFSET + 0x16C)
# define DSC_EN_DP_ENC1_P0_MASK                                          0x1
# define DSC_EN_DP_ENC1_P0_SHIFT                                         0
# define DSC_BYTE_SWAP_DP_ENC1_P0_MASK                                   0x2
# define DSC_BYTE_SWAP_DP_ENC1_P0_SHIFT                                  1
# define DSC_SLICE_NUM_DP_ENC1_P0_MASK                                   0xf0
# define DSC_SLICE_NUM_DP_ENC1_P0_SHIFT                                  4
# define DSC_CHUNK_REMAINDER_DP_ENC1_P0_MASK                             0xf00
# define DSC_CHUNK_REMAINDER_DP_ENC1_P0_SHIFT                            8

#define MTK_DP_ENC1_P0_3370              (ENC1_OFFSET + 0x170)
# define DSC_CHUNK_NUM_DP_ENC1_P0_MASK                                   0xffff
# define DSC_CHUNK_NUM_DP_ENC1_P0_SHIFT                                  0

#define MTK_DP_ENC1_P0_33AC              (ENC1_OFFSET + 0x1AC)
# define TEST_CRC_R_CR_DP_ENC1_P0_MASK                                   0xffff
# define TEST_CRC_R_CR_DP_ENC1_P0_SHIFT                                  0

#define MTK_DP_ENC1_P0_33B0              (ENC1_OFFSET + 0x1B0)
# define TEST_CRC_G_Y_DP_ENC1_P0_MASK                                    0xffff
# define TEST_CRC_G_Y_DP_ENC1_P0_SHIFT                                   0

#define MTK_DP_ENC1_P0_33B4              (ENC1_OFFSET + 0x1B4)
# define TEST_CRC_B_CB_DP_ENC1_P0_MASK                                   0xffff
# define TEST_CRC_B_CB_DP_ENC1_P0_SHIFT                                  0

#define MTK_DP_ENC1_P0_33B8              (ENC1_OFFSET + 0x1B8)
# define TEST_CRC_WRAP_CNT_DP_ENC1_P0_MASK                               0xf
# define TEST_CRC_WRAP_CNT_DP_ENC1_P0_SHIFT                              0
# define CRC_COLOR_FORMAT_DP_ENC1_P0_MASK                                0x1f0
# define CRC_COLOR_FORMAT_DP_ENC1_P0_SHIFT                               4
# define CRC_TEST_SINK_START_DP_ENC1_P0_MASK                             0x200
# define CRC_TEST_SINK_START_DP_ENC1_P0_SHIFT                            9

#define MTK_DP_ENC1_P0_33BC              (ENC1_OFFSET + 0x1BC)
# define CRC_TEST_CONFIG_DP_ENC1_P0_MASK                                 0x1fff
# define CRC_TEST_CONFIG_DP_ENC1_P0_SHIFT                                0

#define MTK_DP_ENC1_P0_33C0              (ENC1_OFFSET + 0x1C0)
# define VIDEO_TU_VALUE_DP_ENC1_P0_MASK                                  0x7f
# define VIDEO_TU_VALUE_DP_ENC1_P0_SHIFT                                 0
# define DP_TX_MIXER_TESTBUS_SEL_DP_ENC1_P0_MASK                         0xf00
# define DP_TX_MIXER_TESTBUS_SEL_DP_ENC1_P0_SHIFT                        8
# define DP_TX_SDP_TESTBUS_SEL_DP_ENC1_P0_MASK                           0xf000
# define DP_TX_SDP_TESTBUS_SEL_DP_ENC1_P0_SHIFT                          12

#define MTK_DP_ENC1_P0_33C4              (ENC1_OFFSET + 0x1C4)
# define DP_TX_VIDEO_TESTBUS_SEL_DP_ENC1_P0_MASK                         0x1f
# define DP_TX_VIDEO_TESTBUS_SEL_DP_ENC1_P0_SHIFT                        0
# define DP_TX_ENCODER_TESTBUS_SEL_DP_ENC1_P0_MASK                       0x60
# define DP_TX_ENCODER_TESTBUS_SEL_DP_ENC1_P0_SHIFT                      5

#define MTK_DP_ENC1_P0_33C8              (ENC1_OFFSET + 0x1C8)
# define VIDEO_M_CODE_READ_0_DP_ENC1_P0_MASK                             0xffff
# define VIDEO_M_CODE_READ_0_DP_ENC1_P0_SHIFT                            0

#define MTK_DP_ENC1_P0_33CC              (ENC1_OFFSET + 0x1CC)
# define VIDEO_M_CODE_READ_1_DP_ENC1_P0_MASK                             0xff
# define VIDEO_M_CODE_READ_1_DP_ENC1_P0_SHIFT                            0

#define MTK_DP_ENC1_P0_33D0              (ENC1_OFFSET + 0x1D0)
# define AUDIO_M_CODE_READ_0_DP_ENC1_P0_MASK                             0xffff
# define AUDIO_M_CODE_READ_0_DP_ENC1_P0_SHIFT                            0

#define MTK_DP_ENC1_P0_33D4              (ENC1_OFFSET + 0x1D4)
# define AUDIO_M_CODE_READ_1_DP_ENC1_P0_MASK                             0xff
# define AUDIO_M_CODE_READ_1_DP_ENC1_P0_SHIFT                            0

#define MTK_DP_ENC1_P0_33D8              (ENC1_OFFSET + 0x1D8)
# define VSC_EXT_CFG_DP_ENC1_P0_MASK                                     0xff
# define VSC_EXT_CFG_DP_ENC1_P0_SHIFT                                    0
# define SDP_SPLIT_FIFO_EMPTY_DP_ENC1_P0_MASK                            0x100
# define SDP_SPLIT_FIFO_EMPTY_DP_ENC1_P0_SHIFT                           8
# define SDP_SPLIT_FIFO_FULL_DP_ENC1_P0_MASK                             0x200
# define SDP_SPLIT_FIFO_FULL_DP_ENC1_P0_SHIFT                            9
# define SDP_SPLIT_FIFO_FULL_CLR_DP_ENC1_P0_MASK                         0x400
# define SDP_SPLIT_FIFO_FULL_CLR_DP_ENC1_P0_SHIFT                        10
# define SDP_SPLIT_INSERT_INVALID_CNT_THRD_DP_ENC1_P0_MASK               0xf000
# define SDP_SPLIT_INSERT_INVALID_CNT_THRD_DP_ENC1_P0_SHIFT              12

#define MTK_DP_ENC1_P0_33DC              (ENC1_OFFSET + 0x1DC)
# define VIDEO_SRAM0_FULL_DP_ENC1_P0_MASK                                0x1
# define VIDEO_SRAM0_FULL_DP_ENC1_P0_SHIFT                               0
# define VIDEO_SRAM0_FULL_CLR_DP_ENC1_P0_MASK                            0x2
# define VIDEO_SRAM0_FULL_CLR_DP_ENC1_P0_SHIFT                           1
# define VIDEO_SRAM1_FULL_DP_ENC1_P0_MASK                                0x4
# define VIDEO_SRAM1_FULL_DP_ENC1_P0_SHIFT                               2
# define VIDEO_SRAM1_FULL_CLR_DP_ENC1_P0_MASK                            0x8
# define VIDEO_SRAM1_FULL_CLR_DP_ENC1_P0_SHIFT                           3
# define VIDEO_SRAM2_FULL_DP_ENC1_P0_MASK                                0x10
# define VIDEO_SRAM2_FULL_DP_ENC1_P0_SHIFT                               4
# define VIDEO_SRAM2_FULL_CLR_DP_ENC1_P0_MASK                            0x20
# define VIDEO_SRAM2_FULL_CLR_DP_ENC1_P0_SHIFT                           5
# define VIDEO_SRAM3_FULL_DP_ENC1_P0_MASK                                0x40
# define VIDEO_SRAM3_FULL_DP_ENC1_P0_SHIFT                               6
# define VIDEO_SRAM3_FULL_CLR_DP_ENC1_P0_MASK                            0x80
# define VIDEO_SRAM3_FULL_CLR_DP_ENC1_P0_SHIFT                           7
# define VIDEO_SRAM0_EMPTY_DP_ENC1_P0_MASK                               0x100
# define VIDEO_SRAM0_EMPTY_DP_ENC1_P0_SHIFT                              8
# define VIDEO_SRAM0_EMPTY_CLR_DP_ENC1_P0_MASK                           0x200
# define VIDEO_SRAM0_EMPTY_CLR_DP_ENC1_P0_SHIFT                          9
# define VIDEO_SRAM1_EMPTY_DP_ENC1_P0_MASK                               0x400
# define VIDEO_SRAM1_EMPTY_DP_ENC1_P0_SHIFT                              10
# define VIDEO_SRAM1_EMPTY_CLR_DP_ENC1_P0_MASK                           0x800
# define VIDEO_SRAM1_EMPTY_CLR_DP_ENC1_P0_SHIFT                          11
# define VIDEO_SRAM2_EMPTY_DP_ENC1_P0_MASK                               0x1000
# define VIDEO_SRAM2_EMPTY_DP_ENC1_P0_SHIFT                              12
# define VIDEO_SRAM2_EMPTY_CLR_DP_ENC1_P0_MASK                           0x2000
# define VIDEO_SRAM2_EMPTY_CLR_DP_ENC1_P0_SHIFT                          13
# define VIDEO_SRAM3_EMPTY_DP_ENC1_P0_MASK                               0x4000
# define VIDEO_SRAM3_EMPTY_DP_ENC1_P0_SHIFT                              14
# define VIDEO_SRAM3_EMPTY_CLR_DP_ENC1_P0_MASK                           0x8000
# define VIDEO_SRAM3_EMPTY_CLR_DP_ENC1_P0_SHIFT                          15

#define MTK_DP_ENC1_P0_33E0              (ENC1_OFFSET + 0x1E0)
# define BS2BS_CNT_SW_DP_ENC1_P0_MASK                                    0xffff
# define BS2BS_CNT_SW_DP_ENC1_P0_SHIFT                                   0

#define MTK_DP_ENC1_P0_33E4              (ENC1_OFFSET + 0x1E4)
# define MIXER_STATE_0_DP_ENC1_P0_MASK                                   0xffff
# define MIXER_STATE_0_DP_ENC1_P0_SHIFT                                  0

#define MTK_DP_ENC1_P0_33E8              (ENC1_OFFSET + 0x1E8)
# define MIXER_STATE_1_DP_ENC1_P0_MASK                                   0xffff
# define MIXER_STATE_1_DP_ENC1_P0_SHIFT                                  0

#define MTK_DP_ENC1_P0_33EC              (ENC1_OFFSET + 0x1EC)
# define MIXER_STATE_2_DP_ENC1_P0_MASK                                   0xff
# define MIXER_STATE_2_DP_ENC1_P0_SHIFT                                  0
# define VIDEO_PERIOD_ENABLE_DP_ENC1_P0_MASK                             0x200
# define VIDEO_PERIOD_ENABLE_DP_ENC1_P0_SHIFT                            9
# define BS2BS_CNT_SW_SEL_DP_ENC1_P0_MASK                                0x400
# define BS2BS_CNT_SW_SEL_DP_ENC1_P0_SHIFT                               10
# define AUDIO_SRAM_FULL_DP_ENC1_P0_MASK                                 0x800
# define AUDIO_SRAM_FULL_DP_ENC1_P0_SHIFT                                11
# define AUDIO_SRAM_FULL_CLR_DP_ENC1_P0_MASK                             0x1000
# define AUDIO_SRAM_FULL_CLR_DP_ENC1_P0_SHIFT                            12
# define AUDIO_SRAM_EMPTY_DP_ENC1_P0_MASK                                0x2000
# define AUDIO_SRAM_EMPTY_DP_ENC1_P0_SHIFT                               13

#define MTK_DP_ENC1_P0_33F0              (ENC1_OFFSET + 0x1F0)
# define DP_ENCODER_DUMMY_RW_0_DP_ENC1_P0_MASK                           0xffff
# define DP_ENCODER_DUMMY_RW_0_DP_ENC1_P0_SHIFT                          0

#define MTK_DP_ENC1_P0_33F4              (ENC1_OFFSET + 0x1F4)
# define DP_ENCODER_DUMMY_RW_1_DP_ENC1_P0_MASK                           0xffff
# define DP_ENCODER_DUMMY_RW_1_DP_ENC1_P0_SHIFT                          0

#define MTK_DP_ENC1_P0_33F8              (ENC1_OFFSET + 0x1F8)
# define DP_ENCODER_DUMMY_R_0_DP_ENC1_P0_MASK                            0xffff
# define DP_ENCODER_DUMMY_R_0_DP_ENC1_P0_SHIFT                           0

#define MTK_DP_ENC1_P0_33FC              (ENC1_OFFSET + 0x1FC)
# define DP_ENCODER_DUMMY_R_1_DP_ENC1_P0_MASK                            0xffff
# define DP_ENCODER_DUMMY_R_1_DP_ENC1_P0_SHIFT                           0

#define MTK_DP_TRANS_P0_3400              (TRANS_OFFSET + 0x000)
# define PRE_MISC_LANE0_MUX_DP_TRANS_P0_MASK                                 0x3
# define PRE_MISC_LANE0_MUX_DP_TRANS_P0_SHIFT                                0
# define PRE_MISC_LANE1_MUX_DP_TRANS_P0_MASK                                 0xc
# define PRE_MISC_LANE1_MUX_DP_TRANS_P0_SHIFT                                2
# define PRE_MISC_LANE2_MUX_DP_TRANS_P0_MASK                                 0x30
# define PRE_MISC_LANE2_MUX_DP_TRANS_P0_SHIFT                                4
# define PRE_MISC_LANE3_MUX_DP_TRANS_P0_MASK                                 0xc0
# define PRE_MISC_LANE3_MUX_DP_TRANS_P0_SHIFT                                6
# define PRE_MISC_PORT_MUX_DP_TRANS_P0_MASK                                  0x700
# define PRE_MISC_PORT_MUX_DP_TRANS_P0_SHIFT                                 8
# define HDCP_SEL_DP_TRANS_P0_MASK                                           0x800
# define HDCP_SEL_DP_TRANS_P0_SHIFT                                          11
# define PATTERN1_EN_DP_TRANS_P0_MASK                                        0x1000
# define PATTERN1_EN_DP_TRANS_P0_SHIFT                                       12
# define PATTERN2_EN_DP_TRANS_P0_MASK                                        0x2000
# define PATTERN2_EN_DP_TRANS_P0_SHIFT                                       13
# define PATTERN3_EN_DP_TRANS_P0_MASK                                        0x4000
# define PATTERN3_EN_DP_TRANS_P0_SHIFT                                       14
# define PATTERN4_EN_DP_TRANS_P0_MASK                                        0x8000
# define PATTERN4_EN_DP_TRANS_P0_SHIFT                                       15

#define MTK_DP_TRANS_P0_3404              (TRANS_OFFSET + 0x004)
# define DP_SCR_EN_DP_TRANS_P0_MASK                                          0x1
# define DP_SCR_EN_DP_TRANS_P0_SHIFT                                         0
# define ALTER_SCRAMBLER_RESET_EN_DP_TRANS_P0_MASK                           0x2
# define ALTER_SCRAMBLER_RESET_EN_DP_TRANS_P0_SHIFT                          1
# define SCRAMB_BYPASS_IN_EN_DP_TRANS_P0_MASK                                0x4
# define SCRAMB_BYPASS_IN_EN_DP_TRANS_P0_SHIFT                               2
# define SCRAMB_BYPASS_MASK_DP_TRANS_P0_MASK                                 0x8
# define SCRAMB_BYPASS_MASK_DP_TRANS_P0_SHIFT                                3
# define INDEX_SCR_MODE_DP_TRANS_P0_MASK                                     0x30
# define INDEX_SCR_MODE_DP_TRANS_P0_SHIFT                                    4
# define PAT_INIT_DISPARITY_DP_TRANS_P0_MASK                                 0x40
# define PAT_INIT_DISPARITY_DP_TRANS_P0_SHIFT                                6
# define TPS_DISPARITY_RESET_DP_TRANS_P0_MASK                                0x80
# define TPS_DISPARITY_RESET_DP_TRANS_P0_SHIFT                               7

#define MTK_DP_TRANS_P0_3408              (TRANS_OFFSET + 0x008)
# define LANE_SKEW_SEL_LANE0_DP_TRANS_P0_MASK                                0x3
# define LANE_SKEW_SEL_LANE0_DP_TRANS_P0_SHIFT                               0
# define LANE_SKEW_SEL_LANE1_DP_TRANS_P0_MASK                                0xc
# define LANE_SKEW_SEL_LANE1_DP_TRANS_P0_SHIFT                               2
# define LANE_SKEW_SEL_LANE2_DP_TRANS_P0_MASK                                0x30
# define LANE_SKEW_SEL_LANE2_DP_TRANS_P0_SHIFT                               4
# define LANE_SKEW_SEL_LANE3_DP_TRANS_P0_MASK                                0xc0
# define LANE_SKEW_SEL_LANE3_DP_TRANS_P0_SHIFT                               6
# define POST_MISC_LANE0_MUX_DP_TRANS_P0_MASK                                0x300
# define POST_MISC_LANE0_MUX_DP_TRANS_P0_SHIFT                               8
# define POST_MISC_LANE1_MUX_DP_TRANS_P0_MASK                                0xc00
# define POST_MISC_LANE1_MUX_DP_TRANS_P0_SHIFT                               10
# define POST_MISC_LANE2_MUX_DP_TRANS_P0_MASK                                0x3000
# define POST_MISC_LANE2_MUX_DP_TRANS_P0_SHIFT                               12
# define POST_MISC_LANE3_MUX_DP_TRANS_P0_MASK                                0xc000
# define POST_MISC_LANE3_MUX_DP_TRANS_P0_SHIFT                               14

#define MTK_DP_TRANS_P0_340C              (TRANS_OFFSET + 0x00C)
# define TOP_RESET_SW_DP_TRANS_P0_MASK                                       0x100
# define TOP_RESET_SW_DP_TRANS_P0_SHIFT                                      8
# define LANE0_RESET_SW_DP_TRANS_P0_MASK                                     0x200
# define LANE0_RESET_SW_DP_TRANS_P0_SHIFT                                    9
# define LANE1_RESET_SW_DP_TRANS_P0_MASK                                     0x400
# define LANE1_RESET_SW_DP_TRANS_P0_SHIFT                                    10
# define LANE2_RESET_SW_DP_TRANS_P0_MASK                                     0x800
# define LANE2_RESET_SW_DP_TRANS_P0_SHIFT                                    11
# define LANE3_RESET_SW_DP_TRANS_P0_MASK                                     0x1000
# define LANE3_RESET_SW_DP_TRANS_P0_SHIFT                                    12
# define DP_TX_TRANSMITTER_4P_RESET_SW_DP_TRANS_P0_MASK                      0x2000
# define DP_TX_TRANSMITTER_4P_RESET_SW_DP_TRANS_P0_SHIFT                     13
# define HDCP13_RST_SW_DP_TRANS_P0_MASK                                      0x4000
# define HDCP13_RST_SW_DP_TRANS_P0_SHIFT                                     14
# define HDCP22_RST_SW_DP_TRANS_P0_MASK                                      0x8000
# define HDCP22_RST_SW_DP_TRANS_P0_SHIFT                                     15

#define MTK_DP_TRANS_P0_3410              (TRANS_OFFSET + 0x010)
# define HPD_DEB_THD_DP_TRANS_P0_MASK                                        0xf
# define HPD_DEB_THD_DP_TRANS_P0_SHIFT                                       0
# define HPD_INT_THD_DP_TRANS_P0_MASK                                        0xf0
# define HPD_INT_THD_DP_TRANS_P0_SHIFT                                       4
# define HPD_INT_THD_DP_TRANS_P0_LOWER_100US                                 (0 << HPD_INT_THD_DP_TRANS_P0_SHIFT)
# define HPD_INT_THD_DP_TRANS_P0_LOWER_300US                                 (1 << HPD_INT_THD_DP_TRANS_P0_SHIFT)
# define HPD_INT_THD_DP_TRANS_P0_LOWER_500US                                 (2 << HPD_INT_THD_DP_TRANS_P0_SHIFT)
# define HPD_INT_THD_DP_TRANS_P0_LOWER_700US                                 (3 << HPD_INT_THD_DP_TRANS_P0_SHIFT)
# define HPD_INT_THD_DP_TRANS_P0_UPPER_700US                                 (0 << (HPD_INT_THD_DP_TRANS_P0_SHIFT + 2))
# define HPD_INT_THD_DP_TRANS_P0_UPPER_900US                                 (1 << (HPD_INT_THD_DP_TRANS_P0_SHIFT + 2))
# define HPD_INT_THD_DP_TRANS_P0_UPPER_1100US                                (2 << (HPD_INT_THD_DP_TRANS_P0_SHIFT + 2))
# define HPD_INT_THD_DP_TRANS_P0_UPPER_1300US                                (3 << (HPD_INT_THD_DP_TRANS_P0_SHIFT + 2))
# define HPD_DISC_THD_DP_TRANS_P0_MASK                                       0xf00
# define HPD_DISC_THD_DP_TRANS_P0_SHIFT                                      8
# define HPD_CONN_THD_DP_TRANS_P0_MASK                                       0xf000
# define HPD_CONN_THD_DP_TRANS_P0_SHIFT                                      12

#define MTK_DP_TRANS_P0_3414              (TRANS_OFFSET + 0x014)
# define HPD_OVR_EN_DP_TRANS_P0_MASK                                         0x1
# define HPD_OVR_EN_DP_TRANS_P0_SHIFT                                        0
# define HPD_SET_DP_TRANS_P0_MASK                                            0x2
# define HPD_SET_DP_TRANS_P0_SHIFT                                           1
# define HPD_DB_DP_TRANS_P0_MASK                                             0x4
# define HPD_DB_DP_TRANS_P0_SHIFT                                            2

#define MTK_DP_TRANS_P0_3418              (TRANS_OFFSET + 0x018)
# define IRQ_CLR_DP_TRANS_P0_MASK                                            0xf
# define IRQ_CLR_DP_TRANS_P0_SHIFT                                           0
# define IRQ_MASK_DP_TRANS_P0_MASK                                           0xf0
# define IRQ_MASK_DP_TRANS_P0_SHIFT                                          4
# define IRQ_MASK_DP_TRANS_P0_DISC_IRQ                                       (BIT(1) << IRQ_MASK_DP_TRANS_P0_SHIFT)
# define IRQ_MASK_DP_TRANS_P0_CONN_IRQ                                       (BIT(2) << IRQ_MASK_DP_TRANS_P0_SHIFT)
# define IRQ_MASK_DP_TRANS_P0_INT_IRQ                                        (BIT(3) << IRQ_MASK_DP_TRANS_P0_SHIFT)
# define IRQ_FORCE_DP_TRANS_P0_MASK                                          0xf00
# define IRQ_FORCE_DP_TRANS_P0_SHIFT                                         8
# define IRQ_STATUS_DP_TRANS_P0_MASK                                         0xf000
# define IRQ_STATUS_DP_TRANS_P0_SHIFT                                        12
# define IRQ_STATUS_DP_TRANS_P0_DISC_IRQ                                     (BIT(1) << IRQ_STATUS_DP_TRANS_P0_SHIFT)
# define IRQ_STATUS_DP_TRANS_P0_CONN_IRQ                                     (BIT(2) << IRQ_STATUS_DP_TRANS_P0_SHIFT)
# define IRQ_STATUS_DP_TRANS_P0_INT_IRQ                                      (BIT(3) << IRQ_STATUS_DP_TRANS_P0_SHIFT)
#define MTK_DP_TRANS_P0_341C              (TRANS_OFFSET + 0x01C)
# define IRQ_CLR_51_DP_TRANS_P0_MASK                                         0xf
# define IRQ_CLR_51_DP_TRANS_P0_SHIFT                                        0
# define IRQ_MASK_51_DP_TRANS_P0_MASK                                        0xf0
# define IRQ_MASK_51_DP_TRANS_P0_SHIFT                                       4
# define IRQ_FORCE_51_DP_TRANS_P0_MASK                                       0xf00
# define IRQ_FORCE_51_DP_TRANS_P0_SHIFT                                      8
# define IRQ_STATUS_51_DP_TRANS_P0_MASK                                      0xf000
# define IRQ_STATUS_51_DP_TRANS_P0_SHIFT                                     12

#define MTK_DP_TRANS_P0_3420              (TRANS_OFFSET + 0x020)
# define HPD_STATUS_DP_TRANS_P0_MASK                                         0x1
# define HPD_STATUS_DP_TRANS_P0_SHIFT                                        0

#define MTK_DP_TRANS_P0_3428              (TRANS_OFFSET + 0x028)
# define POST_MISC_BIT_REVERSE_EN_LANE0_DP_TRANS_P0_MASK                     0x1
# define POST_MISC_BIT_REVERSE_EN_LANE0_DP_TRANS_P0_SHIFT                    0
# define POST_MISC_BIT_REVERSE_EN_LANE1_DP_TRANS_P0_MASK                     0x2
# define POST_MISC_BIT_REVERSE_EN_LANE1_DP_TRANS_P0_SHIFT                    1
# define POST_MISC_BIT_REVERSE_EN_LANE2_DP_TRANS_P0_MASK                     0x4
# define POST_MISC_BIT_REVERSE_EN_LANE2_DP_TRANS_P0_SHIFT                    2
# define POST_MISC_BIT_REVERSE_EN_LANE3_DP_TRANS_P0_MASK                     0x8
# define POST_MISC_BIT_REVERSE_EN_LANE3_DP_TRANS_P0_SHIFT                    3
# define POST_MISC_PN_SWAP_EN_LANE0_DP_TRANS_P0_MASK                         0x10
# define POST_MISC_PN_SWAP_EN_LANE0_DP_TRANS_P0_SHIFT                        4
# define POST_MISC_PN_SWAP_EN_LANE1_DP_TRANS_P0_MASK                         0x20
# define POST_MISC_PN_SWAP_EN_LANE1_DP_TRANS_P0_SHIFT                        5
# define POST_MISC_PN_SWAP_EN_LANE2_DP_TRANS_P0_MASK                         0x40
# define POST_MISC_PN_SWAP_EN_LANE2_DP_TRANS_P0_SHIFT                        6
# define POST_MISC_PN_SWAP_EN_LANE3_DP_TRANS_P0_MASK                         0x80
# define POST_MISC_PN_SWAP_EN_LANE3_DP_TRANS_P0_SHIFT                        7
# define POST_MISC_DATA_SWAP_EN_LANE0_DP_TRANS_P0_MASK                       0x100
# define POST_MISC_DATA_SWAP_EN_LANE0_DP_TRANS_P0_SHIFT                      8
# define POST_MISC_DATA_SWAP_EN_LANE1_DP_TRANS_P0_MASK                       0x200
# define POST_MISC_DATA_SWAP_EN_LANE1_DP_TRANS_P0_SHIFT                      9
# define POST_MISC_DATA_SWAP_EN_LANE2_DP_TRANS_P0_MASK                       0x400
# define POST_MISC_DATA_SWAP_EN_LANE2_DP_TRANS_P0_SHIFT                      10
# define POST_MISC_DATA_SWAP_EN_LANE3_DP_TRANS_P0_MASK                       0x800
# define POST_MISC_DATA_SWAP_EN_LANE3_DP_TRANS_P0_SHIFT                      11

#define MTK_DP_TRANS_P0_342C              (TRANS_OFFSET + 0x02C)
# define XTAL_FREQ_DP_TRANS_P0_DEFAULT                                       0x69
# define XTAL_FREQ_DP_TRANS_P0_MASK                                          0xff
# define XTAL_FREQ_DP_TRANS_P0_SHIFT                                         0

#define MTK_DP_TRANS_P0_3430              (TRANS_OFFSET + 0x030)
# define HPD_INT_THD_ECO_DP_TRANS_P0_MASK                                    0x3
# define HPD_INT_THD_ECO_DP_TRANS_P0_SHIFT                                   0
# define HPD_INT_THD_ECO_DP_TRANS_P0_LOW_BOUND_EXT                           BIT(0)
# define HPD_INT_THD_ECO_DP_TRANS_P0_HIGH_BOUND_EXT                          BIT(1)

#define MTK_DP_TRANS_P0_3440              (TRANS_OFFSET + 0x040)
# define PGM_PAT_EN_DP_TRANS_P0_MASK                                         0xf
# define PGM_PAT_EN_DP_TRANS_P0_SHIFT                                        0
# define PGM_PAT_SEL_L0_DP_TRANS_P0_MASK                                     0x70
# define PGM_PAT_SEL_L0_DP_TRANS_P0_SHIFT                                    4
# define PGM_PAT_SEL_L1_DP_TRANS_P0_MASK                                     0x700
# define PGM_PAT_SEL_L1_DP_TRANS_P0_SHIFT                                    8
# define PGM_PAT_SEL_L2_DP_TRANS_P0_MASK                                     0x7000
# define PGM_PAT_SEL_L2_DP_TRANS_P0_SHIFT                                    12

#define MTK_DP_TRANS_P0_3444              (TRANS_OFFSET + 0x044)
# define PGM_PAT_SEL_L3_DP_TRANS_P0_MASK                                     0x7
# define PGM_PAT_SEL_L3_DP_TRANS_P0_SHIFT                                    0
# define PRBS_EN_DP_TRANS_P0_MASK                                            0x8
# define PRBS_EN_DP_TRANS_P0_SHIFT                                           3

#define MTK_DP_TRANS_P0_3448              (TRANS_OFFSET + 0x048)
# define PGM_PAT_L0_0_DP_TRANS_P0_MASK                                       0xffff
# define PGM_PAT_L0_0_DP_TRANS_P0_SHIFT                                      0

#define MTK_DP_TRANS_P0_344C              (TRANS_OFFSET + 0x04C)
# define PGM_PAT_L0_1_DP_TRANS_P0_MASK                                       0xffff
# define PGM_PAT_L0_1_DP_TRANS_P0_SHIFT                                      0

#define MTK_DP_TRANS_P0_3450              (TRANS_OFFSET + 0x050)
# define PGM_PAT_L0_2_DP_TRANS_P0_MASK                                       0xff
# define PGM_PAT_L0_2_DP_TRANS_P0_SHIFT                                      0

#define MTK_DP_TRANS_P0_3454              (TRANS_OFFSET + 0x054)
# define PGM_PAT_L1_0_DP_TRANS_P0_MASK                                       0xffff
# define PGM_PAT_L1_0_DP_TRANS_P0_SHIFT                                      0

#define MTK_DP_TRANS_P0_3458              (TRANS_OFFSET + 0x058)
# define PGM_PAT_L1_1_DP_TRANS_P0_MASK                                       0xffff
# define PGM_PAT_L1_1_DP_TRANS_P0_SHIFT                                      0

#define MTK_DP_TRANS_P0_345C              (TRANS_OFFSET + 0x05C)
# define PGM_PAT_L1_2_DP_TRANS_P0_MASK                                       0xff
# define PGM_PAT_L1_2_DP_TRANS_P0_SHIFT                                      0

#define MTK_DP_TRANS_P0_3460              (TRANS_OFFSET + 0x060)
# define PGM_PAT_L2_0_DP_TRANS_P0_MASK                                       0xffff
# define PGM_PAT_L2_0_DP_TRANS_P0_SHIFT                                      0

#define MTK_DP_TRANS_P0_3464              (TRANS_OFFSET + 0x064)
# define PGM_PAT_L2_1_DP_TRANS_P0_MASK                                       0xffff
# define PGM_PAT_L2_1_DP_TRANS_P0_SHIFT                                      0

#define MTK_DP_TRANS_P0_3468              (TRANS_OFFSET + 0x068)
# define PGM_PAT_L2_2_DP_TRANS_P0_MASK                                       0xff
# define PGM_PAT_L2_2_DP_TRANS_P0_SHIFT                                      0

#define MTK_DP_TRANS_P0_346C              (TRANS_OFFSET + 0x06C)
# define PGM_PAT_L3_0_DP_TRANS_P0_MASK                                       0xffff
# define PGM_PAT_L3_0_DP_TRANS_P0_SHIFT                                      0

#define MTK_DP_TRANS_P0_3470              (TRANS_OFFSET + 0x070)
# define PGM_PAT_L3_1_DP_TRANS_P0_MASK                                       0xffff
# define PGM_PAT_L3_1_DP_TRANS_P0_SHIFT                                      0

#define MTK_DP_TRANS_P0_3474              (TRANS_OFFSET + 0x074)
# define PGM_PAT_L3_2_DP_TRANS_P0_MASK                                       0xff
# define PGM_PAT_L3_2_DP_TRANS_P0_SHIFT                                      0

#define MTK_DP_TRANS_P0_3478              (TRANS_OFFSET + 0x078)
# define CP2520_PATTERN1_DP_TRANS_P0_MASK                                    0x1
# define CP2520_PATTERN1_DP_TRANS_P0_SHIFT                                   0
# define CP2520_PATTERN2_DP_TRANS_P0_MASK                                    0x2
# define CP2520_PATTERN2_DP_TRANS_P0_SHIFT                                   1
# define CP2520_PATTERN1_KCODE_ERROR_LANE0_DP_TRANS_P0_MASK                  0x10
# define CP2520_PATTERN1_KCODE_ERROR_LANE0_DP_TRANS_P0_SHIFT                 4
# define CP2520_PATTERN1_KCODE_ERROR_LANE1_DP_TRANS_P0_MASK                  0x20
# define CP2520_PATTERN1_KCODE_ERROR_LANE1_DP_TRANS_P0_SHIFT                 5
# define CP2520_PATTERN1_KCODE_ERROR_LANE2_DP_TRANS_P0_MASK                  0x40
# define CP2520_PATTERN1_KCODE_ERROR_LANE2_DP_TRANS_P0_SHIFT                 6
# define CP2520_PATTERN1_KCODE_ERROR_LANE3_DP_TRANS_P0_MASK                  0x80
# define CP2520_PATTERN1_KCODE_ERROR_LANE3_DP_TRANS_P0_SHIFT                 7
# define CP2520_PATTERN1_DCODE_ERROR_LANE0_DP_TRANS_P0_MASK                  0x100
# define CP2520_PATTERN1_DCODE_ERROR_LANE0_DP_TRANS_P0_SHIFT                 8
# define CP2520_PATTERN1_DCODE_ERROR_LANE1_DP_TRANS_P0_MASK                  0x200
# define CP2520_PATTERN1_DCODE_ERROR_LANE1_DP_TRANS_P0_SHIFT                 9
# define CP2520_PATTERN1_DCODE_ERROR_LANE2_DP_TRANS_P0_MASK                  0x400
# define CP2520_PATTERN1_DCODE_ERROR_LANE2_DP_TRANS_P0_SHIFT                 10
# define CP2520_PATTERN1_DCODE_ERROR_LANE3_DP_TRANS_P0_MASK                  0x800
# define CP2520_PATTERN1_DCODE_ERROR_LANE3_DP_TRANS_P0_SHIFT                 11

#define MTK_DP_TRANS_P0_347C              (TRANS_OFFSET + 0x07C)
# define CP2520_PATTERN2_KCODE_ERROR_LANE0_DP_TRANS_P0_MASK                  0x1
# define CP2520_PATTERN2_KCODE_ERROR_LANE0_DP_TRANS_P0_SHIFT                 0
# define CP2520_PATTERN2_KCODE_ERROR_LANE1_DP_TRANS_P0_MASK                  0x2
# define CP2520_PATTERN2_KCODE_ERROR_LANE1_DP_TRANS_P0_SHIFT                 1
# define CP2520_PATTERN2_KCODE_ERROR_LANE2_DP_TRANS_P0_MASK                  0x4
# define CP2520_PATTERN2_KCODE_ERROR_LANE2_DP_TRANS_P0_SHIFT                 2
# define CP2520_PATTERN2_KCODE_ERROR_LANE3_DP_TRANS_P0_MASK                  0x8
# define CP2520_PATTERN2_KCODE_ERROR_LANE3_DP_TRANS_P0_SHIFT                 3
# define CP2520_PATTERN2_DCODE_ERROR_LANE0_DP_TRANS_P0_MASK                  0x10
# define CP2520_PATTERN2_DCODE_ERROR_LANE0_DP_TRANS_P0_SHIFT                 4
# define CP2520_PATTERN2_DCODE_ERROR_LANE1_DP_TRANS_P0_MASK                  0x20
# define CP2520_PATTERN2_DCODE_ERROR_LANE1_DP_TRANS_P0_SHIFT                 5
# define CP2520_PATTERN2_DCODE_ERROR_LANE2_DP_TRANS_P0_MASK                  0x40
# define CP2520_PATTERN2_DCODE_ERROR_LANE2_DP_TRANS_P0_SHIFT                 6
# define CP2520_PATTERN2_DCODE_ERROR_LANE3_DP_TRANS_P0_MASK                  0x80
# define CP2520_PATTERN2_DCODE_ERROR_LANE3_DP_TRANS_P0_SHIFT                 7
# define CP2520_PATTERN3_KCODE_ERROR_LANE0_DP_TRANS_P0_MASK                  0x100
# define CP2520_PATTERN3_KCODE_ERROR_LANE0_DP_TRANS_P0_SHIFT                 8
# define CP2520_PATTERN3_KCODE_ERROR_LANE1_DP_TRANS_P0_MASK                  0x200
# define CP2520_PATTERN3_KCODE_ERROR_LANE1_DP_TRANS_P0_SHIFT                 9
# define CP2520_PATTERN3_KCODE_ERROR_LANE2_DP_TRANS_P0_MASK                  0x400
# define CP2520_PATTERN3_KCODE_ERROR_LANE2_DP_TRANS_P0_SHIFT                 10
# define CP2520_PATTERN3_KCODE_ERROR_LANE3_DP_TRANS_P0_MASK                  0x800
# define CP2520_PATTERN3_KCODE_ERROR_LANE3_DP_TRANS_P0_SHIFT                 11
# define CP2520_PATTERN3_DCODE_ERROR_LANE0_DP_TRANS_P0_MASK                  0x1000
# define CP2520_PATTERN3_DCODE_ERROR_LANE0_DP_TRANS_P0_SHIFT                 12
# define CP2520_PATTERN3_DCODE_ERROR_LANE1_DP_TRANS_P0_MASK                  0x2000
# define CP2520_PATTERN3_DCODE_ERROR_LANE1_DP_TRANS_P0_SHIFT                 13
# define CP2520_PATTERN3_DCODE_ERROR_LANE2_DP_TRANS_P0_MASK                  0x4000
# define CP2520_PATTERN3_DCODE_ERROR_LANE2_DP_TRANS_P0_SHIFT                 14
# define CP2520_PATTERN3_DCODE_ERROR_LANE3_DP_TRANS_P0_MASK                  0x8000
# define CP2520_PATTERN3_DCODE_ERROR_LANE3_DP_TRANS_P0_SHIFT                 15

#define MTK_DP_TRANS_P0_3480              (TRANS_OFFSET + 0x080)
# define DP_EN_DP_TRANS_P0_MASK                                              0x1
# define DP_EN_DP_TRANS_P0_SHIFT                                             0
# define HDCP_CAPABLE_DP_TRANS_P0_MASK                                       0x2
# define HDCP_CAPABLE_DP_TRANS_P0_SHIFT                                      1
# define SELECT_INTERNAL_AN_DP_TRANS_P0_MASK                                 0x4
# define SELECT_INTERNAL_AN_DP_TRANS_P0_SHIFT                                2
# define AN_FREERUN_DP_TRANS_P0_MASK                                         0x8
# define AN_FREERUN_DP_TRANS_P0_SHIFT                                        3
# define KM_GENERATED_DP_TRANS_P0_MASK                                       0x10
# define KM_GENERATED_DP_TRANS_P0_SHIFT                                      4
# define REQ_BLOCK_CIPHER_AUTH_DP_TRANS_P0_MASK                              0x1000
# define REQ_BLOCK_CIPHER_AUTH_DP_TRANS_P0_SHIFT                             12
# define HDCP_1LANE_SEL_DP_TRANS_P0_MASK                                     0x2000
# define HDCP_1LANE_SEL_DP_TRANS_P0_SHIFT                                    13
# define HDCP_24LANE_SEL_DP_TRANS_P0_MASK                                    0x4000
# define HDCP_24LANE_SEL_DP_TRANS_P0_SHIFT                                   14
# define MST_EN_DP_TRANS_P0_MASK                                             0x8000
# define MST_EN_DP_TRANS_P0_SHIFT                                            15

#define MTK_DP_TRANS_P0_34A4              (TRANS_OFFSET + 0x0A4)
# define EN_COPY_2LANE_MSA_DP_TRANS_P0_MASK                                  0x1
# define EN_COPY_2LANE_MSA_DP_TRANS_P0_SHIFT                                 0
# define EN_COPY_4LANE_MSA_DP_TRANS_P0_MASK                                  0x2
# define EN_COPY_4LANE_MSA_DP_TRANS_P0_SHIFT                                 1
# define LANE_NUM_DP_TRANS_P0_MASK                                           0xc
# define LANE_NUM_DP_TRANS_P0_SHIFT                                          2
# define HDCP22_AUTH_DONE_DP_TRANS_P0_MASK                                   0x10
# define HDCP22_AUTH_DONE_DP_TRANS_P0_SHIFT                                  4
# define DISCARD_UNUSED_CIPHER_DP_TRANS_P0_MASK                              0x20
# define DISCARD_UNUSED_CIPHER_DP_TRANS_P0_SHIFT                             5
# define HDCP22_CIPHER_REVERSE_DP_TRANS_P0_MASK                              0x40
# define HDCP22_CIPHER_REVERSE_DP_TRANS_P0_SHIFT                             6
# define MST_DELAY_CYCLE_FLAG_SEL_DP_TRANS_P0_MASK                           0x80
# define MST_DELAY_CYCLE_FLAG_SEL_DP_TRANS_P0_SHIFT                          7
# define TEST_CONFIG_HDCP22_DP_TRANS_P0_MASK                                 0xf00
# define TEST_CONFIG_HDCP22_DP_TRANS_P0_SHIFT                                8
# define R0_AVAILABLE_DP_TRANS_P0_MASK                                       0x1000
# define R0_AVAILABLE_DP_TRANS_P0_SHIFT                                      12
# define DPES_TX_HDCP22_DP_TRANS_P0_MASK                                     0x2000
# define DPES_TX_HDCP22_DP_TRANS_P0_SHIFT                                    13
# define DP_AES_OUT_RDY_L_DP_TRANS_P0_MASK                                   0x4000
# define DP_AES_OUT_RDY_L_DP_TRANS_P0_SHIFT                                  14
# define REPEATER_I_DP_TRANS_P0_MASK                                         0x8000
# define REPEATER_I_DP_TRANS_P0_SHIFT                                        15

#define MTK_DP_TRANS_P0_34A8              (TRANS_OFFSET + 0x0A8)
# define TEST_CONFIG_HDCP13_DP_TRANS_P0_MASK                                 0xff00
# define TEST_CONFIG_HDCP13_DP_TRANS_P0_SHIFT                                8

#define MTK_DP_TRANS_P0_34D0              (TRANS_OFFSET + 0x0D0)
# define TX_HDCP22_TYPE_DP_TRANS_P0_MASK                                     0xff
# define TX_HDCP22_TYPE_DP_TRANS_P0_SHIFT                                    0
# define PIPE_DELAY_EN_CNT_DP_TRANS_P0_MASK                                  0xf00
# define PIPE_DELAY_EN_CNT_DP_TRANS_P0_SHIFT                                 8
# define PIPE_DELAY_DP_TRANS_P0_MASK                                         0xf000
# define PIPE_DELAY_DP_TRANS_P0_SHIFT                                        12

#define MTK_DP_TRANS_P0_34D4              (TRANS_OFFSET + 0x0D4)
# define DP_AES_INCTR_L_0_DP_TRANS_P0_MASK                                   0xffff
# define DP_AES_INCTR_L_0_DP_TRANS_P0_SHIFT                                  0

#define MTK_DP_TRANS_P0_34D8              (TRANS_OFFSET + 0x0D8)
# define DP_AES_INCTR_L_1_DP_TRANS_P0_MASK                                   0xffff
# define DP_AES_INCTR_L_1_DP_TRANS_P0_SHIFT                                  0

#define MTK_DP_TRANS_P0_34DC              (TRANS_OFFSET + 0x0DC)
# define DP_AES_INCTR_L_2_DP_TRANS_P0_MASK                                   0xffff
# define DP_AES_INCTR_L_2_DP_TRANS_P0_SHIFT                                  0

#define MTK_DP_TRANS_P0_34E0              (TRANS_OFFSET + 0x0E0)
# define DP_AES_INCTR_L_3_DP_TRANS_P0_MASK                                   0xffff
# define DP_AES_INCTR_L_3_DP_TRANS_P0_SHIFT                                  0

#define MTK_DP_TRANS_P0_34E4              (TRANS_OFFSET + 0x0E4)
# define HDCP_TYPE_TX_0_DP_TRANS_P0_MASK                                     0xffff
# define HDCP_TYPE_TX_0_DP_TRANS_P0_SHIFT                                    0

#define MTK_DP_TRANS_P0_34E8              (TRANS_OFFSET + 0x0E8)
# define HDCP_TYPE_TX_1_DP_TRANS_P0_MASK                                     0xffff
# define HDCP_TYPE_TX_1_DP_TRANS_P0_SHIFT                                    0

#define MTK_DP_TRANS_P0_34EC              (TRANS_OFFSET + 0x0EC)
# define HDCP_TYPE_TX_2_DP_TRANS_P0_MASK                                     0xffff
# define HDCP_TYPE_TX_2_DP_TRANS_P0_SHIFT                                    0

#define MTK_DP_TRANS_P0_34F0              (TRANS_OFFSET + 0x0F0)
# define HDCP_TYPE_TX_3_DP_TRANS_P0_MASK                                     0xffff
# define HDCP_TYPE_TX_3_DP_TRANS_P0_SHIFT                                    0

#define MTK_DP_TRANS_P0_34F4              (TRANS_OFFSET + 0x0F4)
# define SST_HDCP_TYPE_TX_DP_TRANS_P0_MASK                                   0xff
# define SST_HDCP_TYPE_TX_DP_TRANS_P0_SHIFT                                  0
# define PIPE_OV_VALUE_DP_TRANS_P0_MASK                                      0xf00
# define PIPE_OV_VALUE_DP_TRANS_P0_SHIFT                                     8
# define PIPE_OV_ENABLE_DP_TRANS_P0_MASK                                     0x1000
# define PIPE_OV_ENABLE_DP_TRANS_P0_SHIFT                                    12

#define MTK_DP_TRANS_P0_34F8              (TRANS_OFFSET + 0x0F8)
# define DP_AES_OUT_RDY_H_DP_TRANS_P0_MASK                                   0x4000
# define DP_AES_OUT_RDY_H_DP_TRANS_P0_SHIFT                                  14

#define MTK_DP_TRANS_P0_34FC              (TRANS_OFFSET + 0x0FC)
# define HDCP_4P_TO_2P_FIFO_RST_CHK_DP_TRANS_P0_MASK                         0xff
# define HDCP_4P_TO_2P_FIFO_RST_CHK_DP_TRANS_P0_SHIFT                        0
# define HDCP_2P_TO_4P_FIFO_RST_CHK_DP_TRANS_P0_MASK                         0xff00
# define HDCP_2P_TO_4P_FIFO_RST_CHK_DP_TRANS_P0_SHIFT                        8

#define MTK_DP_TRANS_P0_3500              (TRANS_OFFSET + 0x100)
# define DP_AES_INCTR_H_0_DP_TRANS_P0_MASK                                   0xffff
# define DP_AES_INCTR_H_0_DP_TRANS_P0_SHIFT                                  0

#define MTK_DP_TRANS_P0_3504              (TRANS_OFFSET + 0x104)
# define DP_AES_INCTR_H_1_DP_TRANS_P0_MASK                                   0xffff
# define DP_AES_INCTR_H_1_DP_TRANS_P0_SHIFT                                  0

#define MTK_DP_TRANS_P0_3508              (TRANS_OFFSET + 0x108)
# define DP_AES_INCTR_H_2_DP_TRANS_P0_MASK                                   0xffff
# define DP_AES_INCTR_H_2_DP_TRANS_P0_SHIFT                                  0

#define MTK_DP_TRANS_P0_350C              (TRANS_OFFSET + 0x10C)
# define DP_AES_INCTR_H_3_DP_TRANS_P0_MASK                                   0xffff
# define DP_AES_INCTR_H_3_DP_TRANS_P0_SHIFT                                  0

#define MTK_DP_TRANS_P0_3510              (TRANS_OFFSET + 0x110)
# define HDCP22_TYPE_DP_TRANS_P0_MASK                                        0xff
# define HDCP22_TYPE_DP_TRANS_P0_SHIFT                                       0

#define MTK_DP_TRANS_P0_3540              (TRANS_OFFSET + 0x140)
# define FEC_EN_DP_TRANS_P0_MASK                                             0x1
# define FEC_EN_DP_TRANS_P0_SHIFT                                            0
# define FEC_END_MODE_DP_TRANS_P0_MASK                                       0x6
# define FEC_END_MODE_DP_TRANS_P0_SHIFT                                      1
# define FEC_CLOCK_EN_MODE_DP_TRANS_P0_MASK                                  0x8
# define FEC_CLOCK_EN_MODE_DP_TRANS_P0_SHIFT                                 3
# define FEC_FIFO_READ_START_DP_TRANS_P0_MASK                                0xf0
# define FEC_FIFO_READ_START_DP_TRANS_P0_SHIFT                               4
# define FEC_FIFO_UNDER_POINT_DP_TRANS_P0_MASK                               0xf00
# define FEC_FIFO_UNDER_POINT_DP_TRANS_P0_SHIFT                              8
# define FEC_FIFO_OVER_POINT_DP_TRANS_P0_MASK                                0xf000
# define FEC_FIFO_OVER_POINT_DP_TRANS_P0_SHIFT                               12

#define MTK_DP_TRANS_P0_3544              (TRANS_OFFSET + 0x144)
# define FEC_FIFO_RST_DP_TRANS_P0_MASK                                       0x1
# define FEC_FIFO_RST_DP_TRANS_P0_SHIFT                                      0
# define FEC_SUPPORT_DP_TRANS_P0_MASK                                        0x2
# define FEC_SUPPORT_DP_TRANS_P0_SHIFT                                       1
# define FEC_PATTERN_NEW_DP_TRANS_P0_MASK                                    0x4
# define FEC_PATTERN_NEW_DP_TRANS_P0_SHIFT                                   2
# define FEC_INSERT_FIFO_EMPTY_DP_TRANS_P0_MASK                              0x10
# define FEC_INSERT_FIFO_EMPTY_DP_TRANS_P0_SHIFT                             4
# define FEC_INSERT_FIFO_EMPTY_CLR_DP_TRANS_P0_MASK                          0x20
# define FEC_INSERT_FIFO_EMPTY_CLR_DP_TRANS_P0_SHIFT                         5
# define FEC_INSERT_FIFO_FULL_DP_TRANS_P0_MASK                               0x40
# define FEC_INSERT_FIFO_FULL_DP_TRANS_P0_SHIFT                              6
# define FEC_INSERT_FIFO_FULL_CLR_DP_TRANS_P0_MASK                           0x80
# define FEC_INSERT_FIFO_FULL_CLR_DP_TRANS_P0_SHIFT                          7
# define PARITY_INTERLEAVER_DATA_INVERT_PIPE_SEL_DP_TRANS_P0_MASK            0x700
# define PARITY_INTERLEAVER_DATA_INVERT_PIPE_SEL_DP_TRANS_P0_SHIFT           8
# define PAT_INIT_DISPARITY_FEC_DP_TRANS_P0_MASK                             0x800
# define PAT_INIT_DISPARITY_FEC_DP_TRANS_P0_SHIFT                            11
# define FEC_PARITY_DATA_LANE_SWAP_DP_TRANS_P0_MASK                          0x1000
# define FEC_PARITY_DATA_LANE_SWAP_DP_TRANS_P0_SHIFT                         12

#define MTK_DP_TRANS_P0_3548              (TRANS_OFFSET + 0x148)
# define FEC_INSERT_SYMBOL_ERROR_CNT_LANE0_DP_TRANS_P0_MASK                  0x7
# define FEC_INSERT_SYMBOL_ERROR_CNT_LANE0_DP_TRANS_P0_SHIFT                 0
# define FEC_INSERT_SYMBOL_ERROR_LANE0_DP_TRANS_P0_MASK                      0x8
# define FEC_INSERT_SYMBOL_ERROR_LANE0_DP_TRANS_P0_SHIFT                     3
# define FEC_INSERT_SYMBOL_ERROR_CNT_LANE1_DP_TRANS_P0_MASK                  0x70
# define FEC_INSERT_SYMBOL_ERROR_CNT_LANE1_DP_TRANS_P0_SHIFT                 4
# define FEC_INSERT_SYMBOL_ERROR_LANE1_DP_TRANS_P0_MASK                      0x80
# define FEC_INSERT_SYMBOL_ERROR_LANE1_DP_TRANS_P0_SHIFT                     7
# define FEC_INSERT_SYMBOL_ERROR_CNT_LANE2_DP_TRANS_P0_MASK                  0x700
# define FEC_INSERT_SYMBOL_ERROR_CNT_LANE2_DP_TRANS_P0_SHIFT                 8
# define FEC_INSERT_SYMBOL_ERROR_LANE2_DP_TRANS_P0_MASK                      0x800
# define FEC_INSERT_SYMBOL_ERROR_LANE2_DP_TRANS_P0_SHIFT                     11
# define FEC_INSERT_SYMBOL_ERROR_CNT_LANE3_DP_TRANS_P0_MASK                  0x7000
# define FEC_INSERT_SYMBOL_ERROR_CNT_LANE3_DP_TRANS_P0_SHIFT                 12
# define FEC_INSERT_SYMBOL_ERROR_LANE3_DP_TRANS_P0_MASK                      0x8000
# define FEC_INSERT_SYMBOL_ERROR_LANE3_DP_TRANS_P0_SHIFT                     15

#define MTK_DP_TRANS_P0_354C              (TRANS_OFFSET + 0x14C)
# define FEC_CP_HIT_LANE0_DP_TRANS_P0_MASK                                   0x1
# define FEC_CP_HIT_LANE0_DP_TRANS_P0_SHIFT                                  0
# define FEC_CP_HIT_LANE1_DP_TRANS_P0_MASK                                   0x2
# define FEC_CP_HIT_LANE1_DP_TRANS_P0_SHIFT                                  1
# define FEC_CP_HIT_LANE2_DP_TRANS_P0_MASK                                   0x4
# define FEC_CP_HIT_LANE2_DP_TRANS_P0_SHIFT                                  2
# define FEC_CP_HIT_LANE3_DP_TRANS_P0_MASK                                   0x8
# define FEC_CP_HIT_LANE3_DP_TRANS_P0_SHIFT                                  3
# define FEC_CP_HIT_CLR_DP_TRANS_P0_MASK                                     0x10
# define FEC_CP_HIT_CLR_DP_TRANS_P0_SHIFT                                    4
# define FEC_ENCODE_TOP_TESTBUS_SEL_DP_TRANS_P0_MASK                         0x300
# define FEC_ENCODE_TOP_TESTBUS_SEL_DP_TRANS_P0_SHIFT                        8
# define FEC_INSERT_TOP_TESTBUS_SEL_DP_TRANS_P0_MASK                         0xc00
# define FEC_INSERT_TOP_TESTBUS_SEL_DP_TRANS_P0_SHIFT                        10

#define MTK_DP_TRANS_P0_3550              (TRANS_OFFSET + 0x150)
# define FEC_INSERT_FIFO_WCNT_DP_TRANS_P0_MASK                               0x1f
# define FEC_INSERT_FIFO_WCNT_DP_TRANS_P0_SHIFT                              0
# define FEC_INSERT_FIFO_RCNT_DP_TRANS_P0_MASK                               0x1f00
# define FEC_INSERT_FIFO_RCNT_DP_TRANS_P0_SHIFT                              8

#define MTK_DP_TRANS_P0_3554              (TRANS_OFFSET + 0x154)
# define FEC_CLK_GATE_DATA_CNT_0_DP_TRANS_P0_MASK                            0x7f
# define FEC_CLK_GATE_DATA_CNT_0_DP_TRANS_P0_SHIFT                           0

#define MTK_DP_TRANS_P0_3558              (TRANS_OFFSET + 0x158)
# define FEC_CLK_GATE_DATA_CNT_1_0_DP_TRANS_P0_MASK                          0xffff
# define FEC_CLK_GATE_DATA_CNT_1_0_DP_TRANS_P0_SHIFT                         0

#define MTK_DP_TRANS_P0_355C              (TRANS_OFFSET + 0x15C)
# define FEC_CLK_GATE_DATA_CNT_1_1_DP_TRANS_P0_MASK                          0x3
# define FEC_CLK_GATE_DATA_CNT_1_1_DP_TRANS_P0_SHIFT                         0

#define MTK_DP_TRANS_P0_3580              (TRANS_OFFSET + 0x180)
# define DP_TX_TRANS_TESTBUS_SEL_DP_TRANS_P0_MASK                            0x1f
# define DP_TX_TRANS_TESTBUS_SEL_DP_TRANS_P0_SHIFT                           0
# define POST_MISC_DATA_LANE0_OV_DP_TRANS_P0_MASK                            0x100
# define POST_MISC_DATA_LANE0_OV_DP_TRANS_P0_SHIFT                           8
# define POST_MISC_DATA_LANE1_OV_DP_TRANS_P0_MASK                            0x200
# define POST_MISC_DATA_LANE1_OV_DP_TRANS_P0_SHIFT                           9
# define POST_MISC_DATA_LANE2_OV_DP_TRANS_P0_MASK                            0x400
# define POST_MISC_DATA_LANE2_OV_DP_TRANS_P0_SHIFT                           10
# define POST_MISC_DATA_LANE3_OV_DP_TRANS_P0_MASK                            0x800
# define POST_MISC_DATA_LANE3_OV_DP_TRANS_P0_SHIFT                           11

#define MTK_DP_TRANS_P0_3584              (TRANS_OFFSET + 0x184)
# define POST_MISC_DATA_LANE0_0_DP_TRANS_P0_MASK                             0xffff
# define POST_MISC_DATA_LANE0_0_DP_TRANS_P0_SHIFT                            0

#define MTK_DP_TRANS_P0_3588              (TRANS_OFFSET + 0x188)
# define POST_MISC_DATA_LANE0_1_DP_TRANS_P0_MASK                             0xffff
# define POST_MISC_DATA_LANE0_1_DP_TRANS_P0_SHIFT                            0

#define MTK_DP_TRANS_P0_358C              (TRANS_OFFSET + 0x18C)
# define POST_MISC_DATA_LANE0_2_DP_TRANS_P0_MASK                             0xff
# define POST_MISC_DATA_LANE0_2_DP_TRANS_P0_SHIFT                            0

#define MTK_DP_TRANS_P0_3590              (TRANS_OFFSET + 0x190)
# define POST_MISC_DATA_LANE1_0_DP_TRANS_P0_MASK                             0xffff
# define POST_MISC_DATA_LANE1_0_DP_TRANS_P0_SHIFT                            0

#define MTK_DP_TRANS_P0_3594              (TRANS_OFFSET + 0x194)
# define POST_MISC_DATA_LANE1_1_DP_TRANS_P0_MASK                             0xffff
# define POST_MISC_DATA_LANE1_1_DP_TRANS_P0_SHIFT                            0

#define MTK_DP_TRANS_P0_3598              (TRANS_OFFSET + 0x198)
# define POST_MISC_DATA_LANE1_2_DP_TRANS_P0_MASK                             0xff
# define POST_MISC_DATA_LANE1_2_DP_TRANS_P0_SHIFT                            0

#define MTK_DP_TRANS_P0_359C              (TRANS_OFFSET + 0x19C)
# define POST_MISC_DATA_LANE2_0_DP_TRANS_P0_MASK                             0xffff
# define POST_MISC_DATA_LANE2_0_DP_TRANS_P0_SHIFT                            0

#define MTK_DP_TRANS_P0_35A0              (TRANS_OFFSET + 0x1A0)
# define POST_MISC_DATA_LANE2_1_DP_TRANS_P0_MASK                             0xffff
# define POST_MISC_DATA_LANE2_1_DP_TRANS_P0_SHIFT                            0

#define MTK_DP_TRANS_P0_35A4              (TRANS_OFFSET + 0x1A4)
# define POST_MISC_DATA_LANE2_2_DP_TRANS_P0_MASK                             0xff
# define POST_MISC_DATA_LANE2_2_DP_TRANS_P0_SHIFT                            0

#define MTK_DP_TRANS_P0_35A8              (TRANS_OFFSET + 0x1A8)
# define POST_MISC_DATA_LANE3_0_DP_TRANS_P0_MASK                             0xffff
# define POST_MISC_DATA_LANE3_0_DP_TRANS_P0_SHIFT                            0

#define MTK_DP_TRANS_P0_35AC              (TRANS_OFFSET + 0x1AC)
# define POST_MISC_DATA_LANE3_1_DP_TRANS_P0_MASK                             0xffff
# define POST_MISC_DATA_LANE3_1_DP_TRANS_P0_SHIFT                            0

#define MTK_DP_TRANS_P0_35B0              (TRANS_OFFSET + 0x1B0)
# define POST_MISC_DATA_LANE3_2_DP_TRANS_P0_MASK                             0xff
# define POST_MISC_DATA_LANE3_2_DP_TRANS_P0_SHIFT                            0

#define MTK_DP_TRANS_P0_35C0              (TRANS_OFFSET + 0x1C0)
# define SW_IRQ_SRC_DP_TRANS_P0_MASK                                         0xffff
# define SW_IRQ_SRC_DP_TRANS_P0_SHIFT                                        0

#define MTK_DP_TRANS_P0_35C4              (TRANS_OFFSET + 0x1C4)
# define SW_IRQ_MASK_DP_TRANS_P0_MASK                                        0xffff
# define SW_IRQ_MASK_DP_TRANS_P0_SHIFT                                       0

#define MTK_DP_TRANS_P0_35C8              (TRANS_OFFSET + 0x1C8)
# define SW_IRQ_CLR_DP_TRANS_P0_MASK                                         0xffff
# define SW_IRQ_CLR_DP_TRANS_P0_SHIFT                                        0

#define MTK_DP_TRANS_P0_35CC              (TRANS_OFFSET + 0x1CC)
# define SW_IRQ_STATUS_DP_TRANS_P0_MASK                                      0xffff
# define SW_IRQ_STATUS_DP_TRANS_P0_SHIFT                                     0

#define MTK_DP_TRANS_P0_35D0              (TRANS_OFFSET + 0x1D0)
# define SW_IRQ_FINAL_STATUS_DP_TRANS_P0_MASK                                0xffff
# define SW_IRQ_FINAL_STATUS_DP_TRANS_P0_SHIFT                               0

#define MTK_DP_TRANS_P0_35D4              (TRANS_OFFSET + 0x1D4)
# define SW_IRQ_RAW_STATUS_DP_TRANS_P0_MASK                                  0xffff
# define SW_IRQ_RAW_STATUS_DP_TRANS_P0_SHIFT                                 0

#define MTK_DP_TRANS_P0_35D8              (TRANS_OFFSET + 0x1D8)
# define SW_IRQ_FORCE_DP_TRANS_P0_MASK                                       0xffff
# define SW_IRQ_FORCE_DP_TRANS_P0_SHIFT                                      0

#define MTK_DP_TRANS_P0_35F0              (TRANS_OFFSET + 0x1F0)
# define DP_TRANSMITTER_DUMMY_RW_0_DP_TRANS_P0_MASK                          0xffff
# define DP_TRANSMITTER_DUMMY_RW_0_DP_TRANS_P0_SHIFT                         0

#define MTK_DP_TRANS_P0_35F4              (TRANS_OFFSET + 0x1F4)
# define DP_TRANSMITTER_DUMMY_RW_1_DP_TRANS_P0_MASK                          0xffff
# define DP_TRANSMITTER_DUMMY_RW_1_DP_TRANS_P0_SHIFT                         0

#define MTK_DP_TRANS_P0_35F8              (TRANS_OFFSET + 0x1F8)
# define DP_TRANSMITTER_DUMMY_R_0_DP_TRANS_P0_MASK                           0xffff
# define DP_TRANSMITTER_DUMMY_R_0_DP_TRANS_P0_SHIFT                          0

#define MTK_DP_TRANS_P0_35FC              (TRANS_OFFSET + 0x1FC)
# define DP_TRANSMITTER_DUMMY_R_1_DP_TRANS_P0_MASK                           0xffff
# define DP_TRANSMITTER_DUMMY_R_1_DP_TRANS_P0_SHIFT                          0

#define MTK_DP_AUX_P0_3600              (AUX_OFFSET + 0x000)
# define DP_TX_SW_RESET_AUX_TX_P0_MASK                                       0x1
# define DP_TX_SW_RESET_AUX_TX_P0_SHIFT                                      0
# define AUX_TOP_RESET_AUX_TX_P0_MASK                                        0x2
# define AUX_TOP_RESET_AUX_TX_P0_SHIFT                                       1
# define SOFTWARE_RESET_RESERVED_AUX_TX_P0_MASK                              0x1c
# define SOFTWARE_RESET_RESERVED_AUX_TX_P0_SHIFT                             2
# define AUX_CLK_EN_AUX_TX_P0_MASK                                           0x100
# define AUX_CLK_EN_AUX_TX_P0_SHIFT                                          8
# define AUX_CLK_INV_AUX_TX_P0_MASK                                          0x200
# define AUX_CLK_INV_AUX_TX_P0_SHIFT                                         9
# define AUX_CLK_SEL_AUX_TX_P0_MASK                                          0xc00
# define AUX_CLK_SEL_AUX_TX_P0_SHIFT                                         10

#define MTK_DP_AUX_P0_3604              (AUX_OFFSET + 0x004)
# define AUX_TX_FSM_SOFTWARE_RESET_AUX_TX_P0_MASK                            0x8000
# define AUX_TX_FSM_SOFTWARE_RESET_AUX_TX_P0_SHIFT                           15
# define AUX_TX_PHY_SOFTWARE_RESET_AUX_TX_P0_MASK                            0x4000
# define AUX_TX_PHY_SOFTWARE_RESET_AUX_TX_P0_SHIFT                           14
# define AUX_RX_FSM_SOFTWARE_RESET_AUX_TX_P0_MASK                            0x2000
# define AUX_RX_FSM_SOFTWARE_RESET_AUX_TX_P0_SHIFT                           13
# define AUX_RX_PHY_SOFTWARE_RESET_AUX_TX_P0_MASK                            0x1000
# define AUX_RX_PHY_SOFTWARE_RESET_AUX_TX_P0_SHIFT                           12
# define DP_TX_TESTBUS_SEL_AUX_TX_P0_MASK                                    0xff
# define DP_TX_TESTBUS_SEL_AUX_TX_P0_SHIFT                                   0

#define MTK_DP_AUX_P0_3608              (AUX_OFFSET + 0x008)
# define DP_TX_INT_STATUS_AUX_TX_P0_MASK                                     0xffff
# define DP_TX_INT_STATUS_AUX_TX_P0_SHIFT                                    0

#define MTK_DP_AUX_P0_360C              (AUX_OFFSET + 0x00C)
# define AUX_SWAP_AUX_TX_P0_MASK                                             0x8000
# define AUX_SWAP_AUX_TX_P0_SHIFT                                            15
# define AUX_AUX_REPLY_MCU_AUX_TX_P0_MASK                                    0x4000
# define AUX_AUX_REPLY_MCU_AUX_TX_P0_SHIFT                                   14
# define AUX_TIMEOUT_CMP_MASK_AUX_TX_P0_MASK                                 0x2000
# define AUX_TIMEOUT_CMP_MASK_AUX_TX_P0_SHIFT                                13
# define AUX_TIMEOUT_THR_AUX_TX_P0_MASK                                      0x1fff
# define AUX_TIMEOUT_THR_AUX_TX_P0_SHIFT                                     0

#define MTK_DP_AUX_P0_3610              (AUX_OFFSET + 0x010)
# define AUX_EDID_REPLY_MCU_AUX_TX_P0_MASK                                   0x8000
# define AUX_EDID_REPLY_MCU_AUX_TX_P0_SHIFT                                  15
# define AUX_EDID_ADDR_AUX_TX_P0_MASK                                        0x7f00
# define AUX_EDID_ADDR_AUX_TX_P0_SHIFT                                       8
# define AUX_MCCS_REPLY_MCU_AUX_TX_P0_MASK                                   0x80
# define AUX_MCCS_REPLY_MCU_AUX_TX_P0_SHIFT                                  7
# define AUX_MCCS_ADDR_AUX_TX_P0_MASK                                        0x7f
# define AUX_MCCS_ADDR_AUX_TX_P0_SHIFT                                       0

#define MTK_DP_AUX_P0_3614              (AUX_OFFSET + 0x014)
# define AUX_TIMEOUT_THR_EXTEN_AUX_TX_P0_MASK                                0x4000
# define AUX_TIMEOUT_THR_EXTEN_AUX_TX_P0_SHIFT                               14
# define AUX_RX_AVERAGE_SEL_AUX_TX_P0_MASK                                   0x3000
# define AUX_RX_AVERAGE_SEL_AUX_TX_P0_SHIFT                                  12
# define AUX_RX_SYNC_PATTERN_THR_AUX_TX_P0_MASK                              0xf00
# define AUX_RX_SYNC_PATTERN_THR_AUX_TX_P0_SHIFT                             8
# define AUX_RX_DECODE_SEL_AUX_TX_P0_MASK                                    0x80
# define AUX_RX_DECODE_SEL_AUX_TX_P0_SHIFT                                   7
# define AUX_RX_UI_CNT_THR_AUX_TX_P0_MASK                                    0x7f
# define AUX_RX_UI_CNT_THR_AUX_TX_P0_SHIFT                                   0

#define MTK_DP_AUX_P0_3618              (AUX_OFFSET + 0x018)
# define AUX_RX_DP_REV_AUX_TX_P0_MASK                                        0x400
# define AUX_RX_DP_REV_AUX_TX_P0_SHIFT                                       10
# define AUX_RX_FIFO_FULL_AUX_TX_P0_MASK                                     0x200
# define AUX_RX_FIFO_FULL_AUX_TX_P0_SHIFT                                    9
# define AUX_RX_FIFO_EMPTY_AUX_TX_P0_MASK                                    0x100
# define AUX_RX_FIFO_EMPTY_AUX_TX_P0_SHIFT                                   8
# define AUX_RX_FIFO_READ_POINTER_AUX_TX_P0_MASK                             0xf0
# define AUX_RX_FIFO_READ_POINTER_AUX_TX_P0_SHIFT                            4
# define AUX_RX_FIFO_WRITE_POINTER_AUX_TX_P0_MASK                            0xf
# define AUX_RX_FIFO_WRITE_POINTER_AUX_TX_P0_SHIFT                           0

#define MTK_DP_AUX_P0_361C              (AUX_OFFSET + 0x01C)
# define AUX_RX_DATA_BYTE_CNT_AUX_TX_P0_MASK                                 0xff00
# define AUX_RX_DATA_BYTE_CNT_AUX_TX_P0_SHIFT                                8
# define AUX_RESERVED_RO_0_AUX_TX_P0_MASK                                    0xff
# define AUX_RESERVED_RO_0_AUX_TX_P0_SHIFT                                   0

#define MTK_DP_AUX_P0_3620              (AUX_OFFSET + 0x020)
# define AUX_RD_MODE_AUX_TX_P0_MASK                                          0x200
# define AUX_RD_MODE_AUX_TX_P0_SHIFT                                         9
# define AUX_RX_FIFO_READ_PULSE_TX_P0_MASK                                   0x100
# define AUX_RX_FIFO_R_PULSE_TX_P0_SHIFT                                     8
# define AUX_RX_FIFO_READ_DATA_AUX_TX_P0_MASK                                0xff
# define AUX_RX_FIFO_READ_DATA_AUX_TX_P0_SHIFT                               0

#define MTK_DP_AUX_P0_3624              (AUX_OFFSET + 0x024)
# define AUX_RX_REPLY_COMMAND_AUX_TX_P0_MASK                                 0xf
# define AUX_RX_REPLY_COMMAND_AUX_TX_P0_SHIFT                                0
# define AUX_RX_REPLY_ADDRESS_NONE_AUX_TX_P0_MASK                            0xf00
# define AUX_RX_REPLY_ADDRESS_NONE_AUX_TX_P0_SHIFT                           8

#define MTK_DP_AUX_P0_3628              (AUX_OFFSET + 0x028)
# define AUX_RESERVED_RO_1_AUX_TX_P0_MASK                                    0xfc00
# define AUX_RESERVED_RO_1_AUX_TX_P0_SHIFT                                   10
# define AUX_RX_PHY_STATE_AUX_TX_P0_MASK                                     0x3ff
# define AUX_RX_PHY_STATE_AUX_TX_P0_SHIFT                                    0
# define AUX_RX_PHY_STATE_AUX_TX_P0_RX_IDLE                                  (BIT(1) << AUX_RX_PHY_STATE_AUX_TX_P0_SHIFT)

#define MTK_DP_AUX_P0_362C              (AUX_OFFSET + 0x02C)
# define AUX_NO_LENGTH_AUX_TX_P0_MASK                                        0x1
# define AUX_NO_LENGTH_AUX_TX_P0_SHIFT                                       0
# define AUX_TX_AUXTX_OV_EN_AUX_TX_P0_MASK                                   0x2
# define AUX_TX_AUXTX_OV_EN_AUX_TX_P0_SHIFT                                  1
# define AUX_RESERVED_RW_0_AUX_TX_P0_MASK                                    0xfffc
# define AUX_RESERVED_RW_0_AUX_TX_P0_SHIFT                                   2

#define MTK_DP_AUX_P0_3630              (AUX_OFFSET + 0x030)
# define AUX_TX_REQUEST_READY_AUX_TX_P0_MASK                                 0x8
# define AUX_TX_REQUEST_READY_AUX_TX_P0_SHIFT                                3
# define AUX_TX_PRE_NUM_AUX_TX_P0_MASK                                       0xff00
# define AUX_TX_PRE_NUM_AUX_TX_P0_SHIFT                                      8

#define MTK_DP_AUX_P0_3634              (AUX_OFFSET + 0x034)
# define AUX_TX_OVER_SAMPLE_RATE_AUX_TX_P0_MASK                              0xff00
# define AUX_TX_OVER_SAMPLE_RATE_AUX_TX_P0_SHIFT                             8
# define AUX_TX_FIFO_WRITE_DATA_AUX_TX_P0_MASK                               0xff
# define AUX_TX_FIFO_WRITE_DATA_AUX_TX_P0_SHIFT                              0

#define MTK_DP_AUX_P0_3638              (AUX_OFFSET + 0x038)
# define AUX_TX_FIFO_READ_POINTER_AUX_TX_P0_MASK                             0xf0
# define AUX_TX_FIFO_READ_POINTER_AUX_TX_P0_SHIFT                            4
# define AUX_TX_FIFO_WRITE_POINTER_AUX_TX_P0_MASK                            0xf
# define AUX_TX_FIFO_WRITE_POINTER_AUX_TX_P0_SHIFT                           0

#define MTK_DP_AUX_P0_363C              (AUX_OFFSET + 0x03C)
# define AUX_TX_FIFO_FULL_AUX_TX_P0_MASK                                     0x1000
# define AUX_TX_FIFO_FULL_AUX_TX_P0_SHIFT                                    12
# define AUX_TX_FIFO_EMPTY_AUX_TX_P0_MASK                                    0x800
# define AUX_TX_FIFO_EMPTY_AUX_TX_P0_SHIFT                                   11
# define AUX_TX_PHY_STATE_AUX_TX_P0_MASK                                     0x7ff
# define AUX_TX_PHY_STATE_AUX_TX_P0_SHIFT                                    0

#define MTK_DP_AUX_P0_3640              (AUX_OFFSET + 0x040)
# define AUX_RX_RECV_COMPLETE_IRQ_TX_P0_MASK                                 0x40
# define AUX_RX_AUX_RECV_COMPLETE_IRQ_AUX_TX_P0_SHIFT                        6
# define AUX_RX_EDID_RECV_COMPLETE_IRQ_AUX_TX_P0_MASK                        0x20
# define AUX_RX_EDID_RECV_COMPLETE_IRQ_AUX_TX_P0_SHIFT                       5
# define AUX_RX_MCCS_RECV_COMPLETE_IRQ_AUX_TX_P0_MASK                        0x10
# define AUX_RX_MCCS_RECV_COMPLETE_IRQ_AUX_TX_P0_SHIFT                       4
# define AUX_RX_CMD_RECV_IRQ_AUX_TX_P0_MASK                                  0x8
# define AUX_RX_CMD_RECV_IRQ_AUX_TX_P0_SHIFT                                 3
# define AUX_RX_ADDR_RECV_IRQ_AUX_TX_P0_MASK                                 0x4
# define AUX_RX_ADDR_RECV_IRQ_AUX_TX_P0_SHIFT                                2
# define AUX_RX_DATA_RECV_IRQ_AUX_TX_P0_MASK                                 0x2
# define AUX_RX_DATA_RECV_IRQ_AUX_TX_P0_SHIFT                                1
# define AUX_400US_TIMEOUT_IRQ_AUX_TX_P0_MASK                                0x1
# define AUX_400US_TIMEOUT_IRQ_AUX_TX_P0_SHIFT                               0

#define MTK_DP_AUX_P0_3644              (AUX_OFFSET + 0x044)
# define MCU_REQUEST_COMMAND_AUX_TX_P0_MASK                                  0xf
# define MCU_REQUEST_COMMAND_AUX_TX_P0_SHIFT                                 0
# define AUX_STATE_AUX_TX_P0_MASK                                            0xf00
# define AUX_STATE_AUX_TX_P0_SHIFT                                           8

#define MTK_DP_AUX_P0_3648              (AUX_OFFSET + 0x048)
# define MCU_REQUEST_ADDRESS_LSB_AUX_TX_P0_MASK                              0xffff
# define MCU_REQUEST_ADDRESS_LSB_AUX_TX_P0_SHIFT                             0

#define MTK_DP_AUX_P0_364C              (AUX_OFFSET + 0x04C)
# define MCU_REQUEST_ADDRESS_MSB_AUX_TX_P0_MASK                              0xf
# define MCU_REQUEST_ADDRESS_MSB_AUX_TX_P0_SHIFT                             0

#define MTK_DP_AUX_P0_3650              (AUX_OFFSET + 0x050)
# define MCU_REQ_DATA_NUM_AUX_TX_P0_MASK                                     0xf000
# define MCU_REQ_DATA_NUM_AUX_TX_P0_SHIFT                                    12
# define PHY_FIFO_RST_AUX_TX_P0_MASK                                         0x200
# define PHY_FIFO_RST_AUX_TX_P0_SHIFT                                        9
# define MCU_ACK_TRAN_COMPLETE_AUX_TX_P0_MASK                                0x100
# define MCU_ACK_TRAN_COMPLETE_AUX_TX_P0_SHIFT                               8
# define AUX_TEST_CONFIG_AUX_TX_P0_MASK                                      0xff
# define AUX_TEST_CONFIG_AUX_TX_P0_SHIFT                                     0

#define MTK_DP_AUX_P0_3654              (AUX_OFFSET + 0x054)
# define TST_AUXRX_AUX_TX_P0_MASK                                            0xff
# define TST_AUXRX_AUX_TX_P0_SHIFT                                           0

#define MTK_DP_AUX_P0_3658              (AUX_OFFSET + 0x058)
# define AUX_TX_OV_EN_AUX_TX_P0_MASK                                         0x1
# define AUX_TX_OV_EN_AUX_TX_P0_SHIFT                                        0
# define AUX_TX_VALUE_SET_AUX_TX_P0_MASK                                     0x2
# define AUX_TX_VALUE_SET_AUX_TX_P0_SHIFT                                    1
# define AUX_TX_OEN_SET_AUX_TX_P0_MASK                                       0x4
# define AUX_TX_OEN_SET_AUX_TX_P0_SHIFT                                      2
# define AUX_TX_OV_MODE_AUX_TX_P0_MASK                                       0x8
# define AUX_TX_OV_MODE_AUX_TX_P0_SHIFT                                      3
# define AUX_TX_OFF_AUX_TX_P0_MASK                                           0x10
# define AUX_TX_OFF_AUX_TX_P0_SHIFT                                          4
# define EXT_AUX_PHY_MODE_AUX_TX_P0_MASK                                     0x20
# define EXT_AUX_PHY_MODE_AUX_TX_P0_SHIFT                                    5
# define EXT_TX_OEN_POLARITY_AUX_TX_P0_MASK                                  0x40
# define EXT_TX_OEN_POLARITY_AUX_TX_P0_SHIFT                                 6
# define AUX_RX_OEN_SET_AUX_TX_P0_MASK                                       0x80
# define AUX_RX_OEN_SET_AUX_TX_P0_SHIFT                                      7

#define MTK_DP_AUX_P0_365C              (AUX_OFFSET + 0x05C)
# define AUX_RCTRL_AUX_TX_P0_MASK                                            0x1f
# define AUX_RCTRL_AUX_TX_P0_SHIFT                                           0
# define AUX_RPD_AUX_TX_P0_MASK                                              0x20
# define AUX_RPD_AUX_TX_P0_SHIFT                                             5
# define AUX_RX_SEL_AUX_TX_P0_MASK                                           0x40
# define AUX_RX_SEL_AUX_TX_P0_SHIFT                                          6
# define AUXRX_DEBOUNCE_SEL_AUX_TX_P0_MASK                                   0x80
# define AUXRX_DEBOUNCE_SEL_AUX_TX_P0_SHIFT                                  7
# define AUXRXVALID_DEBOUNCE_SEL_AUX_TX_P0_MASK                              0x100
# define AUXRXVALID_DEBOUNCE_SEL_AUX_TX_P0_SHIFT                             8
# define AUX_DEBOUNCE_CLKSEL_AUX_TX_P0_MASK                                  0xe00
# define AUX_DEBOUNCE_CLKSEL_AUX_TX_P0_SHIFT                                 9
# define DATA_VALID_DEBOUNCE_SEL_AUX_TX_P0_MASK                              0x1000
# define DATA_VALID_DEBOUNCE_SEL_AUX_TX_P0_SHIFT                             12

#define MTK_DP_AUX_P0_3660              (AUX_OFFSET + 0x060)
# define DP_TX_INT_MASK_AUX_TX_P0_MASK                                       0xffff
# define DP_TX_INT_MASK_AUX_TX_P0_SHIFT                                      0

#define MTK_DP_AUX_P0_3664              (AUX_OFFSET + 0x064)
# define DP_TX_INT_FORCE_AUX_TX_P0_MASK                                      0xffff
# define DP_TX_INT_FORCE_AUX_TX_P0_SHIFT                                     0

#define MTK_DP_AUX_P0_3668              (AUX_OFFSET + 0x068)
# define DP_TX_INT_CLR_AUX_TX_P0_MASK                                        0xffff
# define DP_TX_INT_CLR_AUX_TX_P0_SHIFT                                       0

#define MTK_DP_AUX_P0_366C              (AUX_OFFSET + 0x06C)
# define XTAL_FREQ_AUX_TX_P0_MASK                                            0xff00
# define XTAL_FREQ_AUX_TX_P0_SHIFT                                           8

#define MTK_DP_AUX_P0_3670              (AUX_OFFSET + 0x070)
# define DPTX_GPIO_OEN_AUX_TX_P0_MASK                                        0x7
# define DPTX_GPIO_OEN_AUX_TX_P0_SHIFT                                       0
# define DPTX_GPIO_OUT_AUX_TX_P0_MASK                                        0x38
# define DPTX_GPIO_OUT_AUX_TX_P0_SHIFT                                       3
# define DPTX_GPIO_IN_AUX_TX_P0_MASK                                         0x1c0
# define DPTX_GPIO_IN_AUX_TX_P0_SHIFT                                        6
# define AUX_IN_AUX_TX_P0_MASK                                               0x200
# define AUX_IN_AUX_TX_P0_SHIFT                                              9
# define PD_AUX_RTERM_AUX_TX_P0_MASK                                         0x400
# define PD_AUX_RTERM_AUX_TX_P0_SHIFT                                        10
# define DPTX_GPIO_EN_AUX_TX_P0_MASK                                         0x7000
# define DPTX_GPIO_EN_AUX_TX_P0_SHIFT                                        12

#define MTK_DP_AUX_P0_3674              (AUX_OFFSET + 0x074)
# define AUXTX_ISEL_AUX_TX_P0_MASK                                           0x1f
# define AUXTX_ISEL_AUX_TX_P0_SHIFT                                          0
# define AUXRX_VTH_AUX_TX_P0_MASK                                            0x60
# define AUXRX_VTH_AUX_TX_P0_SHIFT                                           5
# define EN_RXCM_BOOST_AUX_TX_P0_MASK                                        0x80
# define EN_RXCM_BOOST_AUX_TX_P0_SHIFT                                       7
# define DPTX_AUX_R_CTRL_AUX_TX_P0_MASK                                      0x1f00
# define DPTX_AUX_R_CTRL_AUX_TX_P0_SHIFT                                     8
# define I2C_EN_AUXN_AUX_TX_P0_MASK                                          0x2000
# define I2C_EN_AUXN_AUX_TX_P0_SHIFT                                         13
# define I2C_EN_AUXP_AUX_TX_P0_MASK                                          0x4000
# define I2C_EN_AUXP_AUX_TX_P0_SHIFT                                         14

#define MTK_DP_AUX_P0_3678              (AUX_OFFSET + 0x078)
# define TEST_AUXTX_AUX_TX_P0_MASK                                           0xff00
# define TEST_AUXTX_AUX_TX_P0_SHIFT                                          8

#define MTK_DP_AUX_P0_367C              (AUX_OFFSET + 0x07C)
# define DPTX_AUXRX_AUX_TX_P0_MASK                                           0x4
# define DPTX_AUXRX_AUX_TX_P0_SHIFT                                          2
# define DPTX_AUXRX_VALID_AUX_TX_P0_MASK                                     0x8
# define DPTX_AUXRX_VALID_AUX_TX_P0_SHIFT                                    3
# define DPTX_AUXRX_WO_TH_AUX_TX_P0_MASK                                     0x10
# define DPTX_AUXRX_WO_TH_AUX_TX_P0_SHIFT                                    4
# define DPTX_AUXRX_L_TEST_AUX_TX_P0_MASK                                    0x20
# define DPTX_AUXRX_L_TEST_AUX_TX_P0_SHIFT                                   5
# define EN_AUXRX_AUX_TX_P0_MASK                                             0x400
# define EN_AUXRX_AUX_TX_P0_SHIFT                                            10
# define EN_AUXTX_AUX_TX_P0_MASK                                             0x800
# define EN_AUXTX_AUX_TX_P0_SHIFT                                            11
# define EN_AUX_AUX_TX_P0_MASK                                               0x1000
# define EN_AUX_AUX_TX_P0_SHIFT                                              12
# define EN_5V_TOL_AUX_TX_P0_MASK                                            0x2000
# define EN_5V_TOL_AUX_TX_P0_SHIFT                                           13
# define AUXP_I_AUX_TX_P0_MASK                                               0x4000
# define AUXP_I_AUX_TX_P0_SHIFT                                              14
# define AUXN_I_AUX_TX_P0_MASK                                               0x8000
# define AUXN_I_AUX_TX_P0_SHIFT                                              15

#define MTK_DP_AUX_P0_3680              (AUX_OFFSET + 0x080)
# define AUX_SWAP_TX_AUX_TX_P0_MASK                                          0x1
# define AUX_SWAP_TX_AUX_TX_P0_SHIFT                                         0

#define MTK_DP_AUX_P0_3684              (AUX_OFFSET + 0x084)
# define TEST_IO_LOOPBK_AUX_TX_P0_MASK                                       0x1f
# define TEST_IO_LOOPBK_AUX_TX_P0_SHIFT                                      0
# define RO_IO_LOOPBKT_AUX_TX_P0_MASK                                        0x300
# define RO_IO_LOOPBKT_AUX_TX_P0_SHIFT                                       8
# define SEL_TCLK_AUX_TX_P0_MASK                                             0x3000
# define SEL_TCLK_AUX_TX_P0_SHIFT                                            12
# define TESTEN_ASIO_AUX_TX_P0_MASK                                          0x4000
# define TESTEN_ASIO_AUX_TX_P0_SHIFT                                         14

#define MTK_DP_AUX_P0_3688              (AUX_OFFSET + 0x088)
# define TEST_AUXRX_VTH_AUX_TX_P0_MASK                                       0x7
# define TEST_AUXRX_VTH_AUX_TX_P0_SHIFT                                      0

#define MTK_DP_AUX_P0_368C              (AUX_OFFSET + 0x08C)
# define RX_FIFO_DONE_AUX_TX_P0_MASK                                         0x1
# define RX_FIFO_DONE_AUX_TX_P0_SHIFT                                        0
# define RX_FIFO_DONE_CLR_AUX_TX_P0_MASK                                     0x2
# define RX_FIFO_DONE_CLR_AUX_TX_P0_SHIFT                                    1
# define TX_FIFO_DONE_AUX_TX_P0_MASK                                         0x4
# define TX_FIFO_DONE_AUX_TX_P0_SHIFT                                        2
# define TX_FIFO_DONE_CLR_AUX_TX_P0_MASK                                     0x8
# define TX_FIFO_DONE_CLR_AUX_TX_P0_SHIFT                                    3

#define MTK_DP_AUX_P0_3690              (AUX_OFFSET + 0x090)
# define DATA_LOW_CNT_THRD_AUX_TX_P0_MASK                                    0x7f
# define DATA_LOW_CNT_THRD_AUX_TX_P0_SHIFT                                   0
# define RX_REPLY_COMPLETE_MODE_AUX_TX_P0_MASK                               0x100
# define RX_REPLY_COMPLETE_MODE_AUX_TX_P0_SHIFT                              8

#define MTK_DP_AUX_P0_36C0              (AUX_OFFSET + 0x0C0)
# define RX_GTC_VALUE_0_AUX_TX_P0_MASK                                       0xffff
# define RX_GTC_VALUE_0_AUX_TX_P0_SHIFT                                      0

#define MTK_DP_AUX_P0_36C4              (AUX_OFFSET + 0x0C4)
# define RX_GTC_VALUE_1_AUX_TX_P0_MASK                                       0xffff
# define RX_GTC_VALUE_1_AUX_TX_P0_SHIFT                                      0

#define MTK_DP_AUX_P0_36C8              (AUX_OFFSET + 0x0C8)
# define RX_GTC_MASTER_REQ_AUX_TX_P0_MASK                                    0x1
# define RX_GTC_MASTER_REQ_AUX_TX_P0_SHIFT                                   0
# define TX_GTC_VALUE_PHASE_SKEW_EN_AUX_TX_P0_MASK                           0x2
# define TX_GTC_VALUE_PHASE_SKEW_EN_AUX_TX_P0_SHIFT                          1
# define RX_GTC_FREQ_LOCK_DONE_AUX_TX_P0_MASK                                0x4
# define RX_GTC_FREQ_LOCK_DONE_AUX_TX_P0_SHIFT                               2

#define MTK_DP_AUX_P0_36CC              (AUX_OFFSET + 0x0CC)
# define RX_GTC_PHASE_SKEW_OFFSET_AUX_TX_P0_MASK                             0xffff
# define RX_GTC_PHASE_SKEW_OFFSET_AUX_TX_P0_SHIFT                            0

#define MTK_DP_AUX_P0_36D0              (AUX_OFFSET + 0x0D0)
# define TX_GTC_VALUE_0_AUX_TX_P0_MASK                                       0xffff
# define TX_GTC_VALUE_0_AUX_TX_P0_SHIFT                                      0

#define MTK_DP_AUX_P0_36D4              (AUX_OFFSET + 0x0D4)
# define TX_GTC_VALUE_1_AUX_TX_P0_MASK                                       0xffff
# define TX_GTC_VALUE_1_AUX_TX_P0_SHIFT                                      0

#define MTK_DP_AUX_P0_36D8              (AUX_OFFSET + 0x0D8)
# define RX_GTC_VALUE_PHASE_SKEW_EN_AUX_TX_P0_MASK                           0x1
# define RX_GTC_VALUE_PHASE_SKEW_EN_AUX_TX_P0_SHIFT                          0
# define TX_GTC_FREQ_LOCK_DONE_AUX_TX_P0_MASK                                0x2
# define TX_GTC_FREQ_LOCK_DONE_AUX_TX_P0_SHIFT                               1
# define TX_GTC_VALUE_PHASE_ADJUST_EN_AUX_TX_P0_MASK                         0x4
# define TX_GTC_VALUE_PHASE_ADJUST_EN_AUX_TX_P0_SHIFT                        2

#define MTK_DP_AUX_P0_36DC              (AUX_OFFSET + 0x0DC)
# define TX_GTC_PHASE_SKEW_OFFSET_AUX_TX_P0_MASK                             0xffff
# define TX_GTC_PHASE_SKEW_OFFSET_AUX_TX_P0_SHIFT                            0

#define MTK_DP_AUX_P0_36E0              (AUX_OFFSET + 0x0E0)
# define GTC_STATE_AUX_TX_P0_MASK                                            0xf
# define GTC_STATE_AUX_TX_P0_SHIFT                                           0
# define RX_MASTER_LOCK_ACCQUI_CHKTIME_AUX_TX_P0_MASK                        0xf0
# define RX_MASTER_LOCK_ACCQUI_CHKTIME_AUX_TX_P0_SHIFT                       4
# define FREQ_AUX_TX_P0_MASK                                                 0xff00
# define FREQ_AUX_TX_P0_SHIFT                                                8

#define MTK_DP_AUX_P0_36E4              (AUX_OFFSET + 0x0E4)
# define GTC_TX_1M_ADD_VAL_AUX_TX_P0_MASK                                    0x3ff
# define GTC_TX_1M_ADD_VAL_AUX_TX_P0_SHIFT                                   0
# define GTC_TX_10M_ADD_VAL_AUX_TX_P0_MASK                                   0xf000
# define GTC_TX_10M_ADD_VAL_AUX_TX_P0_SHIFT                                  12

#define MTK_DP_AUX_P0_36E8              (AUX_OFFSET + 0x0E8)
# define CHK_TX_PH_ADJUST_CHK_EN_AUX_TX_P0_MASK                              0x1
# define CHK_TX_PH_ADJUST_CHK_EN_AUX_TX_P0_SHIFT                             0
# define TX_SLAVE_WAIT_SKEW_EN_AUX_TX_P0_MASK                                0x2
# define TX_SLAVE_WAIT_SKEW_EN_AUX_TX_P0_SHIFT                               1
# define GTC_SEND_RCV_EN_AUX_TX_P0_MASK                                      0x4
# define GTC_SEND_RCV_EN_AUX_TX_P0_SHIFT                                     2
# define AUXTX_HW_ACCS_EN_AUX_TX_P0_MASK                                     0x8
# define AUXTX_HW_ACCS_EN_AUX_TX_P0_SHIFT                                    3
# define GTC_TX_MASTER_EN_AUX_TX_P0_MASK                                     0x10
# define GTC_TX_MASTER_EN_AUX_TX_P0_SHIFT                                    4
# define GTC_TX_SLAVE_EN_AUX_TX_P0_MASK                                      0x20
# define GTC_TX_SLAVE_EN_AUX_TX_P0_SHIFT                                     5
# define OFFSET_TRY_NUM_AUX_TX_P0_MASK                                       0xf00
# define OFFSET_TRY_NUM_AUX_TX_P0_SHIFT                                      8
# define HW_SW_ARBIT_AUX_TX_P0_MASK                                          0xc000
# define HW_SW_ARBIT_AUX_TX_P0_SHIFT                                         14

#define MTK_DP_AUX_P0_36EC              (AUX_OFFSET + 0x0EC)
# define GTC_DB_OPTION_AUX_TX_P0_MASK                                        0x7
# define GTC_DB_OPTION_AUX_TX_P0_SHIFT                                       0
# define TX_SLAVE_CHK_RX_LCK_EN_AUX_TX_P0_MASK                               0x8
# define TX_SLAVE_CHK_RX_LCK_EN_AUX_TX_P0_SHIFT                              3
# define GTC_PUL_DELAY_AUX_TX_P0_MASK                                        0xff00
# define GTC_PUL_DELAY_AUX_TX_P0_SHIFT                                       8

#define MTK_DP_AUX_P0_36F0              (AUX_OFFSET + 0x0F0)
# define GTC_TX_LCK_ACQ_SEND_NUM_AUX_TX_P0_MASK                              0x1f
# define GTC_TX_LCK_ACQ_SEND_NUM_AUX_TX_P0_SHIFT                             0

#define MTK_DP_AUX_P0_3700              (AUX_OFFSET + 0x100)
# define AUX_PHYWAKE_MODE_AUX_TX_P0_MASK                                     0x1
# define AUX_PHYWAKE_MODE_AUX_TX_P0_SHIFT                                    0
# define AUX_PHYWAKE_ONLY_AUX_TX_P0_MASK                                     0x2
# define AUX_PHYWAKE_ONLY_AUX_TX_P0_SHIFT                                    1
# define PHYWAKE_PRE_NUM_AUX_TX_P0_MASK                                      0x70
# define PHYWAKE_PRE_NUM_AUX_TX_P0_SHIFT                                     4

#define MTK_DP_AUX_P0_3704              (AUX_OFFSET + 0x104)
# define AUX_PHYWAKE_ACK_RECV_COMPLETE_IRQ_AUX_TX_P0_MASK                    0x1
# define AUX_PHYWAKE_ACK_RECV_COMPLETE_IRQ_AUX_TX_P0_SHIFT                   0
# define AUX_TX_FIFO_WRITE_DATA_NEW_MODE_TOGGLE_AUX_TX_P0_MASK               0x2
# define AUX_TX_FIFO_WRITE_DATA_NEW_MODE_TOGGLE_AUX_TX_P0_SHIFT              1
# define AUX_TX_FIFO_NEW_MODE_EN_AUX_TX_P0_MASK                              0x4
# define AUX_TX_FIFO_NEW_MODE_EN_AUX_TX_P0_SHIFT                             2

#define MTK_DP_AUX_P0_3708              (AUX_OFFSET + 0x108)
# define AUX_TX_FIFO_WRITE_DATA_BYTE0_AUX_TX_P0_MASK                         0xff
# define AUX_TX_FIFO_WRITE_DATA_BYTE0_AUX_TX_P0_SHIFT                        0
# define AUX_TX_FIFO_WRITE_DATA_BYTE1_AUX_TX_P0_MASK                         0xff00
# define AUX_TX_FIFO_WRITE_DATA_BYTE1_AUX_TX_P0_SHIFT                        8

#define MTK_DP_AUX_P0_370C              (AUX_OFFSET + 0x10C)
# define AUX_TX_FIFO_WRITE_DATA_BYTE2_AUX_TX_P0_MASK                         0xff
# define AUX_TX_FIFO_WRITE_DATA_BYTE2_AUX_TX_P0_SHIFT                        0
# define AUX_TX_FIFO_WRITE_DATA_BYTE3_AUX_TX_P0_MASK                         0xff00
# define AUX_TX_FIFO_WRITE_DATA_BYTE3_AUX_TX_P0_SHIFT                        8

#define MTK_DP_AUX_P0_3710              (AUX_OFFSET + 0x110)
# define AUX_TX_FIFO_WRITE_DATA_BYTE4_AUX_TX_P0_MASK                         0xff
# define AUX_TX_FIFO_WRITE_DATA_BYTE4_AUX_TX_P0_SHIFT                        0
# define AUX_TX_FIFO_WRITE_DATA_BYTE5_AUX_TX_P0_MASK                         0xff00
# define AUX_TX_FIFO_WRITE_DATA_BYTE5_AUX_TX_P0_SHIFT                        8

#define MTK_DP_AUX_P0_3714              (AUX_OFFSET + 0x114)
# define AUX_TX_FIFO_WRITE_DATA_BYTE6_AUX_TX_P0_MASK                         0xff
# define AUX_TX_FIFO_WRITE_DATA_BYTE6_AUX_TX_P0_SHIFT                        0
# define AUX_TX_FIFO_WRITE_DATA_BYTE7_AUX_TX_P0_MASK                         0xff00
# define AUX_TX_FIFO_WRITE_DATA_BYTE7_AUX_TX_P0_SHIFT                        8

#define MTK_DP_AUX_P0_3718              (AUX_OFFSET + 0x118)
# define AUX_TX_FIFO_WRITE_DATA_BYTE8_AUX_TX_P0_MASK                         0xff
# define AUX_TX_FIFO_WRITE_DATA_BYTE8_AUX_TX_P0_SHIFT                        0
# define AUX_TX_FIFO_WRITE_DATA_BYTE9_AUX_TX_P0_MASK                         0xff00
# define AUX_TX_FIFO_WRITE_DATA_BYTE9_AUX_TX_P0_SHIFT                        8

#define MTK_DP_AUX_P0_371C              (AUX_OFFSET + 0x11C)
# define AUX_TX_FIFO_WRITE_DATA_BYTE10_AUX_TX_P0_MASK                        0xff
# define AUX_TX_FIFO_WRITE_DATA_BYTE10_AUX_TX_P0_SHIFT                       0
# define AUX_TX_FIFO_WRITE_DATA_BYTE11_AUX_TX_P0_MASK                        0xff00
# define AUX_TX_FIFO_WRITE_DATA_BYTE11_AUX_TX_P0_SHIFT                       8

#define MTK_DP_AUX_P0_3720              (AUX_OFFSET + 0x120)
# define AUX_TX_FIFO_WRITE_DATA_BYTE12_AUX_TX_P0_MASK                        0xff
# define AUX_TX_FIFO_WRITE_DATA_BYTE12_AUX_TX_P0_SHIFT                       0
# define AUX_TX_FIFO_WRITE_DATA_BYTE13_AUX_TX_P0_MASK                        0xff00
# define AUX_TX_FIFO_WRITE_DATA_BYTE13_AUX_TX_P0_SHIFT                       8

#define MTK_DP_AUX_P0_3724              (AUX_OFFSET + 0x124)
# define AUX_TX_FIFO_WRITE_DATA_BYTE14_AUX_TX_P0_MASK                        0xff
# define AUX_TX_FIFO_WRITE_DATA_BYTE14_AUX_TX_P0_SHIFT                       0
# define AUX_TX_FIFO_WRITE_DATA_BYTE15_AUX_TX_P0_MASK                        0xff00
# define AUX_TX_FIFO_WRITE_DATA_BYTE15_AUX_TX_P0_SHIFT                       8

#define MTK_DP_AUX_P0_3740              (AUX_OFFSET + 0x140)
# define HPD_OEN_AUX_TX_P0_MASK                                              0x1
# define HPD_OEN_AUX_TX_P0_SHIFT                                             0
# define HPD_I_AUX_TX_P0_MASK                                                0x2
# define HPD_I_AUX_TX_P0_SHIFT                                               1

#define MTK_DP_AUX_P0_3744              (AUX_OFFSET + 0x144)
# define TEST_AUXRX_AUX_TX_P0_MASK                                           0xffff
# define TEST_AUXRX_AUX_TX_P0_SHIFT                                          0

#define MTK_DP_AUX_P0_3748              (AUX_OFFSET + 0x148)
# define CK_XTAL_AUX_TX_P0_MASK                                              0x1
# define CK_XTAL_AUX_TX_P0_SHIFT                                             0
# define EN_FT_MUX_AUX_TX_P0_MASK                                            0x2
# define EN_FT_MUX_AUX_TX_P0_SHIFT                                           1
# define EN_GPIO_AUX_TX_P0_MASK                                              0x4
# define EN_GPIO_AUX_TX_P0_SHIFT                                             2
# define EN_HBR3_AUX_TX_P0_MASK                                              0x8
# define EN_HBR3_AUX_TX_P0_SHIFT                                             3
# define PD_NGATE_OV_AUX_TX_P0_MASK                                          0x10
# define PD_NGATE_OV_AUX_TX_P0_SHIFT                                         4
# define PD_NGATE_OVEN_AUX_TX_P0_MASK                                        0x20
# define PD_NGATE_OVEN_AUX_TX_P0_SHIFT                                       5
# define PD_VCM_OP_AUX_TX_P0_MASK                                            0x40
# define PD_VCM_OP_AUX_TX_P0_SHIFT                                           6
# define CK_XTAL_SW_AUX_TX_P0_MASK                                           0x80
# define CK_XTAL_SW_AUX_TX_P0_SHIFT                                          7
# define SEL_FTMUX_AUX_TX_P0_MASK                                            0x300
# define SEL_FTMUX_AUX_TX_P0_SHIFT                                           8
# define GTC_EN_AUX_TX_P0_MASK                                               0x400
# define GTC_EN_AUX_TX_P0_SHIFT                                              10
# define GTC_DATA_IN_MODE_AUX_TX_P0_MASK                                     0x800
# define GTC_DATA_IN_MODE_AUX_TX_P0_SHIFT                                    11

#define MTK_DP_AUX_P0_374C              (AUX_OFFSET + 0x14C)
# define AUX_VALID_DB_TH_AUX_TX_P0_MASK                                      0xf
# define AUX_VALID_DB_TH_AUX_TX_P0_SHIFT                                     0
# define CLK_AUX_MUX_VALID_EN_AUX_TX_P0_MASK                                 0x100
# define CLK_AUX_MUX_VALID_EN_AUX_TX_P0_SHIFT                                8
# define CLK_AUX_MUX_VALID_INV_AUX_TX_P0_MASK                                0x200
# define CLK_AUX_MUX_VALID_INV_AUX_TX_P0_SHIFT                               9
# define CLK_AUX_MUX_VALID_SEL_AUX_TX_P0_MASK                                0xc00
# define CLK_AUX_MUX_VALID_SEL_AUX_TX_P0_SHIFT                               10
# define CLK_AUX_MUX_DATA_EN_AUX_TX_P0_MASK                                  0x1000
# define CLK_AUX_MUX_DATA_EN_AUX_TX_P0_SHIFT                                 12
# define CLK_AUX_MUX_DATA_INV_AUX_TX_P0_MASK                                 0x2000
# define CLK_AUX_MUX_DATA_INV_AUX_TX_P0_SHIFT                                13
# define CLK_AUX_MUX_DATA_SEL_AUX_TX_P0_MASK                                 0xc000
# define CLK_AUX_MUX_DATA_SEL_AUX_TX_P0_SHIFT                                14

#define MTK_DP_AUX_P0_3780              (AUX_OFFSET + 0x180)
# define AUX_RX_FIFO_DATA0_AUX_TX_P0_MASK                                    0xff
# define AUX_RX_FIFO_DATA0_AUX_TX_P0_SHIFT                                   0
# define AUX_RX_FIFO_DATA1_AUX_TX_P0_MASK                                    0xff00
# define AUX_RX_FIFO_DATA1_AUX_TX_P0_SHIFT                                   8

#define MTK_DP_AUX_P0_3784              (AUX_OFFSET + 0x184)
# define AUX_RX_FIFO_DATA2_AUX_TX_P0_MASK                                    0xff
# define AUX_RX_FIFO_DATA2_AUX_TX_P0_SHIFT                                   0
# define AUX_RX_FIFO_DATA3_AUX_TX_P0_MASK                                    0xff00
# define AUX_RX_FIFO_DATA3_AUX_TX_P0_SHIFT                                   8

#define MTK_DP_AUX_P0_3788              (AUX_OFFSET + 0x188)
# define AUX_RX_FIFO_DATA4_AUX_TX_P0_MASK                                    0xff
# define AUX_RX_FIFO_DATA4_AUX_TX_P0_SHIFT                                   0
# define AUX_RX_FIFO_DATA5_AUX_TX_P0_MASK                                    0xff00
# define AUX_RX_FIFO_DATA5_AUX_TX_P0_SHIFT                                   8

#define MTK_DP_AUX_P0_378C              (AUX_OFFSET + 0x18C)
# define AUX_RX_FIFO_DATA6_AUX_TX_P0_MASK                                    0xff
# define AUX_RX_FIFO_DATA6_AUX_TX_P0_SHIFT                                   0
# define AUX_RX_FIFO_DATA7_AUX_TX_P0_MASK                                    0xff00
# define AUX_RX_FIFO_DATA7_AUX_TX_P0_SHIFT                                   8

#define MTK_DP_AUX_P0_3790              (AUX_OFFSET + 0x190)
# define AUX_RX_FIFO_DATA8_AUX_TX_P0_MASK                                    0xff
# define AUX_RX_FIFO_DATA8_AUX_TX_P0_SHIFT                                   0
# define AUX_RX_FIFO_DATA9_AUX_TX_P0_MASK                                    0xff00
# define AUX_RX_FIFO_DATA9_AUX_TX_P0_SHIFT                                   8

#define MTK_DP_AUX_P0_3794              (AUX_OFFSET + 0x194)
# define AUX_RX_FIFO_DATA10_AUX_TX_P0_MASK                                   0xff
# define AUX_RX_FIFO_DATA10_AUX_TX_P0_SHIFT                                  0
# define AUX_RX_FIFO_DATA11_AUX_TX_P0_MASK                                   0xff00
# define AUX_RX_FIFO_DATA11_AUX_TX_P0_SHIFT                                  8

#define MTK_DP_AUX_P0_3798              (AUX_OFFSET + 0x198)
# define AUX_RX_FIFO_DATA12_AUX_TX_P0_MASK                                   0xff
# define AUX_RX_FIFO_DATA12_AUX_TX_P0_SHIFT                                  0
# define AUX_RX_FIFO_DATA13_AUX_TX_P0_MASK                                   0xff00
# define AUX_RX_FIFO_DATA13_AUX_TX_P0_SHIFT                                  8

#define MTK_DP_AUX_P0_379C              (AUX_OFFSET + 0x19C)
# define AUX_RX_FIFO_DATA14_AUX_TX_P0_MASK                                   0xff
# define AUX_RX_FIFO_DATA14_AUX_TX_P0_SHIFT                                  0
# define AUX_RX_FIFO_DATA15_AUX_TX_P0_MASK                                   0xff00
# define AUX_RX_FIFO_DATA15_AUX_TX_P0_SHIFT                                  8

#define MTK_DP_AUX_P0_37C0              (AUX_OFFSET + 0x1C0)
# define AUX_DRV_EN_TIME_THRD_AUX_TX_P0_MASK                                 0x1f
# define AUX_DRV_EN_TIME_THRD_AUX_TX_P0_SHIFT                                0
# define AUX_DRV_DIS_TIME_THRD_AUX_TX_P0_MASK                                0x1f00
# define AUX_DRV_DIS_TIME_THRD_AUX_TX_P0_SHIFT                               8

#define MTK_DP_AUX_P0_37C4              (AUX_OFFSET + 0x1C4)
# define AUX_WAIT_TRANSFER_TIME_THRD_AUX_TX_P0_MASK                          0xff
# define AUX_WAIT_TRANSFER_TIME_THRD_AUX_TX_P0_SHIFT                         0
# define AUX_WAIT_RECEIVE_TIME_THRD_AUX_TX_P0_MASK                           0xff00
# define AUX_WAIT_RECEIVE_TIME_THRD_AUX_TX_P0_SHIFT                          8

#define MTK_DP_AUX_P0_37C8              (AUX_OFFSET + 0x1C8)
# define MTK_ATOP_EN_AUX_TX_P0_MASK                                          0x1
# define MTK_ATOP_EN_AUX_TX_P0_SHIFT                                         0

#define MTK_DP_TOP_PWR_STATE              (TOP_OFFSET + 0x00)
# define DP_PWR_STATE_MASK                                                   0x3
# define DP_PWR_STATE_SHIFT                                                  0
# define DP_PWR_STATE_OFF                                                    0
# define DP_PWR_STATE_BANDGAP                                                1
# define DP_PWR_STATE_BANDGAP_TPLL                                           2
# define DP_PWR_STATE_BANDGAP_TPLL_LANE                                      3
# define DP_SCRAMB_EN_MASK                                                   0x4
# define DP_SCRAMB_EN_SHIFT                                                  2
# define DP_DISP_RST_MASK                                                    0x8
# define DP_DISP_RST_SHIFT                                                   3

#define MTK_DP_TOP_SWING_EMP              (TOP_OFFSET + 0x04)
# define DP_TX0_VOLT_SWING_MASK                                              0x3
# define DP_TX0_VOLT_SWING_SHIFT                                             0
# define DP_TX0_PRE_EMPH_MASK                                                0xc
# define DP_TX0_PRE_EMPH_SHIFT                                               2
# define DP_TX0_DATAK_MASK                                                   0xf0
# define DP_TX0_DATAK_SHIFT                                                  4
# define DP_TX1_VOLT_SWING_MASK                                              0x300
# define DP_TX1_VOLT_SWING_SHIFT                                             8
# define DP_TX1_PRE_EMPH_MASK                                                0xc00
# define DP_TX1_PRE_EMPH_SHIFT                                               10
# define DP_TX1_DATAK_MASK                                                   0xf000
# define DP_TX1_DATAK_SHIFT                                                  12
# define DP_TX2_VOLT_SWING_MASK                                              0x30000
# define DP_TX2_VOLT_SWING_SHIFT                                             16
# define DP_TX2_PRE_EMPH_MASK                                                0xc0000
# define DP_TX2_PRE_EMPH_SHIFT                                               18
# define DP_TX2_DATAK_MASK                                                   0xf00000
# define DP_TX2_DATAK_SHIFT                                                  20
# define DP_TX3_VOLT_SWING_MASK                                              0x3000000
# define DP_TX3_VOLT_SWING_SHIFT                                             24
# define DP_TX3_PRE_EMPH_MASK                                                0xc000000
# define DP_TX3_PRE_EMPH_SHIFT                                               26
# define DP_TX3_DATAK_MASK                                                   0xf0000000L
# define DP_TX3_DATAK_SHIFT                                                  28

#define MTK_DP_TOP_APB_WSTRB              (TOP_OFFSET + 0x10)
# define APB_WSTRB_MASK                                           0xf
# define APB_WSTRB_SHIFT                                          0
# define APB_WSTRB_EN_MASK                                        0x10
# define APB_WSTRB_EN_SHIFT                                       4

#define MTK_DP_TOP_RESERVED              (TOP_OFFSET + 0x14)
# define RESERVED_MASK                                            0xffffffffL
# define RESERVED_SHIFT                                           0

#define MTK_DP_TOP_RESET_AND_PROBE              (TOP_OFFSET + 0x20)
# define SW_RST_B_MASK                                            0x1f
# define SW_RST_B_SHIFT                                           0
# define SW_RST_B_PHYD                                          (BIT(4) << SW_RST_B_SHIFT)
# define PROBE_LOW_SEL_MASK                                       0x38000
# define PROBE_LOW_SEL_SHIFT                                      15
# define PROBE_HIGH_SEL_MASK                                      0x1c0000
# define PROBE_HIGH_SEL_SHIFT                                     18
# define PROBE_LOW_HIGH_SWAP_MASK                                 0x200000
# define PROBE_LOW_HIGH_SWAP_SHIFT                                21

#define MTK_DP_TOP_SOFT_PROBE              (TOP_OFFSET + 0x24)
# define SW_PROBE_VALUE_MASK                                      0xffffffffL
# define SW_PROBE_VALUE_SHIFT                                     0

#define MTK_DP_TOP_IRQ_STATUS              (TOP_OFFSET + 0x28)
# define RGS_IRQ_STATUS_MASK                                      0x7
# define RGS_IRQ_STATUS_SHIFT                                     0
# define RGS_IRQ_STATUS_ENCODER                                   (BIT(0) << RGS_IRQ_STATUS_SHIFT)
# define RGS_IRQ_STATUS_TRANSMITTER                               (BIT(1) << RGS_IRQ_STATUS_SHIFT)
# define RGS_IRQ_STATUS_AUX_TOP                                   (BIT(2) << RGS_IRQ_STATUS_SHIFT)

#define MTK_DP_TOP_IRQ_MASK              (TOP_OFFSET + 0x2C)
# define IRQ_MASK_MASK                                            0x7
# define IRQ_MASK_SHIFT                                           0
# define IRQ_MASK_ENCODER_IRQ                                     BIT(0)
# define IRQ_MASK_TRANSMITTER_IRQ                                 BIT(1)
# define IRQ_MASK_AUX_TOP_IRQ                                     BIT(2)
# define IRQ_OUT_HIGH_ACTIVE_MASK                                 0x100
# define IRQ_OUT_HIGH_ACTIVE_SHIFT                                8

#define MTK_DP_TOP_BLACK_SCREEN              (TOP_OFFSET + 0x30)
# define BLACK_SCREEN_ENABLE_MASK                                 0x1
# define BLACK_SCREEN_ENABLE_SHIFT                                0

#define MTK_DP_TOP_MEM_PD              (TOP_OFFSET + 0x38)
# define MEM_ISO_EN_MASK                                          0x1
# define MEM_ISO_EN_SHIFT                                         0
# define MEM_PD_MASK                                              0x2
# define MEM_PD_SHIFT                                             1
# define FUSE_SEL_MASK                                            0x4
# define FUSE_SEL_SHIFT                                           2
# define LOAD_PREFUSE_MASK                                        0x8
# define LOAD_PREFUSE_SHIFT                                       3

#define MTK_DP_TOP_MBIST_PREFUSE              (TOP_OFFSET + 0x3C)
# define RGS_PREFUSE_MASK                                          0xffff
# define RGS_PREFUSE_SHIFT                                         0

#define MTK_DP_TOP_MEM_DELSEL_0              (TOP_OFFSET + 0x40)
# define DELSEL_0_MASK                                             0xfffff
# define DELSEL_0_SHIFT                                            0
# define USE_DEFAULT_DELSEL_0_MASK                                 0x100000
# define USE_DEFAULT_DELSEL_0_SHIFT                                20

#define MTK_DP_TOP_MEM_DELSEL_1              (TOP_OFFSET + 0x44)
# define DELSEL_1_MASK                                             0xfffff
# define DELSEL_1_SHIFT                                            0
# define USE_DEFAULT_DELSEL_1_MASK                                 0x100000
# define USE_DEFAULT_DELSEL_1_SHIFT                                20

#define MTK_DP_TOP_MEM_DELSEL_2              (TOP_OFFSET + 0x48)
# define DELSEL_2_MASK                                             0xfffff
# define DELSEL_2_SHIFT                                            0
# define USE_DEFAULT_DELSEL_2_MASK                                 0x100000
# define USE_DEFAULT_DELSEL_2_SHIFT                                20

#define MTK_DP_TOP_MEM_DELSEL_3              (TOP_OFFSET + 0x4C)
# define DELSEL_3_MASK                                             0xfffff
# define DELSEL_3_SHIFT                                            0
# define USE_DEFAULT_DELSEL_3_MASK                                 0x100000
# define USE_DEFAULT_DELSEL_3_SHIFT                                20

#define MTK_DP_TOP_MEM_DELSEL_4              (TOP_OFFSET + 0x50)
# define DELSEL_4_MASK                                             0xfffff
# define DELSEL_4_SHIFT                                            0
# define USE_DEFAULT_DELSEL_4_MASK                                 0x100000
# define USE_DEFAULT_DELSEL_4_SHIFT                                20

#define MTK_DP_TOP_MEM_DELSEL_5              (TOP_OFFSET + 0x54)
# define DELSEL_5_MASK                                             0xfffff
# define DELSEL_5_SHIFT                                            0
# define USE_DEFAULT_DELSEL_5_MASK                                 0x100000
# define USE_DEFAULT_DELSEL_5_SHIFT                                20

#define MTK_DP_TOP_MEM_DELSEL_6              (TOP_OFFSET + 0x58)
# define DELSEL_6_MASK                                             0xfffff
# define DELSEL_6_SHIFT                                            0
# define USE_DEFAULT_DELSEL_6_MASK                                 0x100000
# define USE_DEFAULT_DELSEL_6_SHIFT                                20

#define MTK_DP_TOP_MEM_DELSEL_7              (TOP_OFFSET + 0x5C)
# define DELSEL_7_MASK                                             0xfffff
# define DELSEL_7_SHIFT                                            0
# define USE_DEFAULT_DELSEL_7_MASK                                 0x100000
# define USE_DEFAULT_DELSEL_7_SHIFT                                20

#define MTK_DP_TOP_MEM_DELSEL_8              (TOP_OFFSET + 0x60)
# define DELSEL_8_MASK                                             0xfffff
# define DELSEL_8_SHIFT                                            0
# define USE_DEFAULT_DELSEL_8_MASK                                 0x100000
# define USE_DEFAULT_DELSEL_8_SHIFT                                20

#define MTK_DP_TOP_MEM_DELSEL_9              (TOP_OFFSET + 0x64)
# define DELSEL_9_MASK                                             0xfffff
# define DELSEL_9_SHIFT                                            0
# define USE_DEFAULT_DELSEL_9_MASK                                 0x100000
# define USE_DEFAULT_DELSEL_9_SHIFT                                20

#define MTK_DP_TOP_MEM_DELSEL_10              (TOP_OFFSET + 0x68)
# define DELSEL_10_MASK                                            0xfffff
# define DELSEL_10_SHIFT                                           0
# define USE_DEFAULT_DELSEL_10_MASK                                0x100000
# define USE_DEFAULT_DELSEL_10_SHIFT                               20

#define MTK_DP_TOP_MEM_DELSEL_11              (TOP_OFFSET + 0x6C)
# define DELSEL_11_MASK                                            0xfffff
# define DELSEL_11_SHIFT                                           0
# define USE_DEFAULT_DELSEL_11_MASK                                0x100000
# define USE_DEFAULT_DELSEL_11_SHIFT                               20

#define MTK_DP_TOP_MEM_DELSEL_12              (TOP_OFFSET + 0x70)
# define DELSEL_12_MASK                                            0xfffff
# define DELSEL_12_SHIFT                                           0
# define USE_DEFAULT_DELSEL_12_MASK                                0x100000
# define USE_DEFAULT_DELSEL_12_SHIFT                               20

#define MTK_DP_TOP_PWR_ACK              (TOP_OFFSET + 0x80)
# define RGS_DP_TX_PWR_ACK_MASK                                    0x1
# define RGS_DP_TX_PWR_ACK_SHIFT                                   0
# define RGS_DP_TX_PWR_ACK_2ND_MASK                                0x2
# define RGS_DP_TX_PWR_ACK_2ND_SHIFT                               1

#define MTK_DP_SECUREREG0              (SEC_OFFSET + 0x00)
# define HDCP22_KS_XOR_LC128_KEY_0_MASK                            0xffffffffL
# define HDCP22_KS_XOR_LC128_KEY_0_SHIFT                           0

#define MTK_DP_SECUREREG1              (SEC_OFFSET + 0x04)
# define HDCP22_KS_XOR_LC128_KEY_1_MASK                            0xffffffffL
# define HDCP22_KS_XOR_LC128_KEY_1_SHIFT                           0

#define MTK_DP_SECUREREG2              (SEC_OFFSET + 0x08)
# define HDCP22_KS_XOR_LC128_KEY_2_MASK                            0xffffffffL
# define HDCP22_KS_XOR_LC128_KEY_2_SHIFT                           0

#define MTK_DP_SECUREREG3              (SEC_OFFSET + 0x0c)
# define HDCP22_KS_XOR_LC128_KEY_3_MASK                            0xffffffffL
# define HDCP22_KS_XOR_LC128_KEY_3_SHIFT                           0

#define MTK_DP_SECUREREG4              (SEC_OFFSET + 0x10)
# define HDCP22_RIV_0_MASK                                         0xffffffffL
# define HDCP22_RIV_0_SHIFT                                        0

#define MTK_DP_SECUREREG5              (SEC_OFFSET + 0x14)
# define HDCP22_RIV_1_MASK                                         0xffffffffL
# define HDCP22_RIV_1_SHIFT                                        0

#define MTK_DP_SECUREREG6              (SEC_OFFSET + 0x18)
# define HDCP13_LN_SEED_MASK                                       0xff
# define HDCP13_LN_SEED_SHIFT                                      0

#define MTK_DP_SECUREREG7              (SEC_OFFSET + 0x1C)
# define HDCP13_LN_CODE_0_MASK                                     0xffffffffL
# define HDCP13_LN_CODE_0_SHIFT                                    0

#define MTK_DP_SECUREREG8              (SEC_OFFSET + 0x20)
# define HDCP13_LN_CODE_1_MASK                                     0xffffff
# define HDCP13_LN_CODE_1_SHIFT                                    0

#define MTK_DP_SECUREREG9              (SEC_OFFSET + 0x24)
# define HDCP13_AN_CODE_0_MASK                                     0xffffffffL
# define HDCP13_AN_CODE_0_SHIFT                                    0

#define MTK_DP_SECUREREG10              (SEC_OFFSET + 0x28)
# define HDCP13_AN_CODE_1_MASK                                     0xffffffffL
# define HDCP13_AN_CODE_1_SHIFT                                    0

#define MTK_DP_SECUREREG11              (SEC_OFFSET + 0x2C)
# define DP_TX_TRANSMITTER_4P_RESET_SW_SECURE_MASK                 0x1
# define DP_TX_TRANSMITTER_4P_RESET_SW_SECURE_SHIFT                0
# define HDCP22_RST_SW_SECURE_MASK                                 0x2
# define HDCP22_RST_SW_SECURE_SHIFT                                1
# define HDCP13_RST_SW_SECURE_MASK                                 0x4
# define HDCP13_RST_SW_SECURE_SHIFT                                2
# define VIDEO_MUTE_SW_SECURE_MASK                                 0x8
# define VIDEO_MUTE_SW_SECURE_SHIFT                                3
# define VIDEO_MUTE_SEL_SECURE_MASK                                0x10
# define VIDEO_MUTE_SEL_SECURE_SHIFT                               4
# define HDCP_FRAME_EN_SECURE_MASK                                 0x20
# define HDCP_FRAME_EN_SECURE_SHIFT                                5
# define HDCP_FRAME_EN_SEL_SECURE_MASK                             0x40
# define HDCP_FRAME_EN_SEL_SECURE_SHIFT                            6
# define VSC_SEL_SECURE_MASK                                       0x80
# define VSC_SEL_SECURE_SHIFT                                      7
# define VSC_DATA_TOGGLE_VESA_SECURE_MASK                          0x100
# define VSC_DATA_TOGGLE_VESA_SECURE_SHIFT                         8
# define VSC_DATA_RDY_VESA_SECURE_MASK                             0x200
# define VSC_DATA_RDY_VESA_SECURE_SHIFT                            9
# define VSC_DATA_TOGGLE_CEA_SECURE_MASK                           0x400
# define VSC_DATA_TOGGLE_CEA_SECURE_SHIFT                          10
# define VSC_DATA_RDY_CEA_SECURE_MASK                              0x800
# define VSC_DATA_RDY_CEA_SECURE_SHIFT                             11

#define MTK_DP_SECUREREG12              (SEC_OFFSET + 0x30)
# define VSC_DATA_BYTE7_CEA_SECURE_MASK                            0xff000000L
# define VSC_DATA_BYTE7_CEA_SECURE_SHIFT                           24
# define VSC_DATA_BYTE6_CEA_SECURE_MASK                            0xff0000
# define VSC_DATA_BYTE6_CEA_SECURE_SHIFT                           16
# define VSC_DATA_BYTE5_CEA_SECURE_MASK                            0xff00
# define VSC_DATA_BYTE5_CEA_SECURE_SHIFT                           8
# define VSC_DATA_BYTE4_CEA_SECURE_MASK                            0xff
# define VSC_DATA_BYTE4_CEA_SECURE_SHIFT                           0

#define MTK_DP_SECUREREG13              (SEC_OFFSET + 0x34)
# define VSC_DATA_BYTE3_CEA_SECURE_MASK                            0xff000000L
# define VSC_DATA_BYTE3_CEA_SECURE_SHIFT                           24
# define VSC_DATA_BYTE2_CEA_SECURE_MASK                            0xff0000
# define VSC_DATA_BYTE2_CEA_SECURE_SHIFT                           16
# define VSC_DATA_BYTE1_CEA_SECURE_MASK                            0xff00
# define VSC_DATA_BYTE1_CEA_SECURE_SHIFT                           8
# define VSC_DATA_BYTE0_CEA_SECURE_MASK                            0xff
# define VSC_DATA_BYTE0_CEA_SECURE_SHIFT                           0

#define MTK_DP_SECUREREG14              (SEC_OFFSET + 0x38)
# define VSC_DATA_BYTE7_VESA_SECURE_MASK                           0xff000000L
# define VSC_DATA_BYTE7_VESA_SECURE_SHIFT                          24
# define VSC_DATA_BYTE6_VESA_SECURE_MASK                           0xff0000
# define VSC_DATA_BYTE6_VESA_SECURE_SHIFT                          16
# define VSC_DATA_BYTE5_VESA_SECURE_MASK                           0xff00
# define VSC_DATA_BYTE5_VESA_SECURE_SHIFT                          8
# define VSC_DATA_BYTE4_VESA_SECURE_MASK                           0xff
# define VSC_DATA_BYTE4_VESA_SECURE_SHIFT                          0

#define MTK_DP_SECUREREG15              (SEC_OFFSET + 0x3C)
# define VSC_DATA_BYTE3_VESA_SECURE_MASK                           0xff000000L
# define VSC_DATA_BYTE3_VESA_SECURE_SHIFT                          24
# define VSC_DATA_BYTE2_VESA_SECURE_MASK                           0xff0000
# define VSC_DATA_BYTE2_VESA_SECURE_SHIFT                          16
# define VSC_DATA_BYTE1_VESA_SECURE_MASK                           0xff00
# define VSC_DATA_BYTE1_VESA_SECURE_SHIFT                          8
# define VSC_DATA_BYTE0_VESA_SECURE_MASK                           0xff
# define VSC_DATA_BYTE0_VESA_SECURE_SHIFT                          0

#define MTK_DP_SECURESTATUS_0              (SEC_OFFSET + 0x80)
# define RGS_DP_TX_HDCP13_HDCP_AN_0_MASK                           0xffffffffL
# define RGS_DP_TX_HDCP13_HDCP_AN_0_SHIFT                          0

#define MTK_DP_SECURESTATUS_1              (SEC_OFFSET + 0x84)
# define RGS_DP_TX_HDCP13_HDCP_AN_1_MASK                           0xffffffffL
# define RGS_DP_TX_HDCP13_HDCP_AN_1_SHIFT                          0

#define MTK_DP_SECURESTATUS_2              (SEC_OFFSET + 0x88)
# define RGS_DP_TX_HDCP13_HDCP_R0_MASK                             0xffff
# define RGS_DP_TX_HDCP13_HDCP_R0_SHIFT                            0

#define MTK_DP_SECURESTATUS_3              (SEC_OFFSET + 0x8C)
# define RGS_DP_TX_HDCP13_HDCP_M0_0_MASK                           0xffffffffL
# define RGS_DP_TX_HDCP13_HDCP_M0_0_SHIFT                          0

#define MTK_DP_SECURESTATUS_4              (SEC_OFFSET + 0x90)
# define RGS_DP_TX_HDCP13_HDCP_M0_1_MASK                           0xffffffffL
# define RGS_DP_TX_HDCP13_HDCP_M0_1_SHIFT                          0

#define MTK_DP_SECUREACC_FAIL              (SEC_OFFSET + 0xf0)
# define NO_AUTH_READ_VALUE_MASK                                   0xffffffffL
# define NO_AUTH_READ_VALUE_SHIFT                                  0

#define DP_PHY_DIG_PLL_CTL_1					0x1014
# define SKIP_SPLL_ON						BIT(0)
# define BIAS_LPF_EN						BIT(1)
# define SPLL_SSC_EN						BIT(2)
# define TPLL_SSC_EN						BIT(3)

#define DP_PHY_DIG_BIT_RATE					0x103C
# define BIT_RATE_RBR						0
# define BIT_RATE_HBR						1
# define BIT_RATE_HBR2						2
# define BIT_RATE_HBR3						3

#define DP_PHY_DIG_SW_RST					0x1038
# define DP_GLB_SW_RST_PHYD					BIT(0)
# define DP_GLB_SW_RST_TFIFO					BIT(1)
# define DP_GLB_SW_RST_XTAL					BIT(2)
# define DP_GLB_SW_RST_LINK					BIT(3)

#define MTK_DP_LANE0_DRIVING_PARAM_3				0x1138
#define MTK_DP_LANE1_DRIVING_PARAM_3				0x1238
#define MTK_DP_LANE2_DRIVING_PARAM_3				0x1338
#define MTK_DP_LANE3_DRIVING_PARAM_3				0x1438
# define XTP_LN_TX_LCTXC0_SW0_PRE0_DEFAULT			0x10
# define XTP_LN_TX_LCTXC0_SW0_PRE1_DEFAULT			(0x14 << 8)
# define XTP_LN_TX_LCTXC0_SW0_PRE2_DEFAULT			(0x18 << 16)
# define XTP_LN_TX_LCTXC0_SW0_PRE3_DEFAULT			(0x20 << 24)
# define DRIVING_PARAM_3_DEFAULT				(XTP_LN_TX_LCTXC0_SW0_PRE0_DEFAULT | \
								 XTP_LN_TX_LCTXC0_SW0_PRE1_DEFAULT | \
								 XTP_LN_TX_LCTXC0_SW0_PRE2_DEFAULT | \
								 XTP_LN_TX_LCTXC0_SW0_PRE3_DEFAULT)

#define MTK_DP_LANE0_DRIVING_PARAM_4				0x113C
#define MTK_DP_LANE1_DRIVING_PARAM_4				0x123C
#define MTK_DP_LANE2_DRIVING_PARAM_4				0x133C
#define MTK_DP_LANE3_DRIVING_PARAM_4				0x143C
# define XTP_LN_TX_LCTXC0_SW1_PRE0_DEFAULT			0x18
# define XTP_LN_TX_LCTXC0_SW1_PRE1_DEFAULT			(0x1e << 8)
# define XTP_LN_TX_LCTXC0_SW1_PRE2_DEFAULT			(0x24 << 16)
# define XTP_LN_TX_LCTXC0_SW2_PRE0_DEFAULT			(0x20 << 24)
# define DRIVING_PARAM_4_DEFAULT				(XTP_LN_TX_LCTXC0_SW1_PRE0_DEFAULT | \
								 XTP_LN_TX_LCTXC0_SW1_PRE1_DEFAULT | \
								 XTP_LN_TX_LCTXC0_SW1_PRE2_DEFAULT | \
								 XTP_LN_TX_LCTXC0_SW2_PRE0_DEFAULT)

#define MTK_DP_LANE0_DRIVING_PARAM_5				0x1140
#define MTK_DP_LANE1_DRIVING_PARAM_5				0x1240
#define MTK_DP_LANE2_DRIVING_PARAM_5				0x1340
#define MTK_DP_LANE3_DRIVING_PARAM_5				0x1440
# define XTP_LN_TX_LCTXC0_SW2_PRE1_DEFAULT			0x28
# define XTP_LN_TX_LCTXC0_SW3_PRE0_DEFAULT			(0x30 << 8)
# define DRIVING_PARAM_5_DEFAULT				(XTP_LN_TX_LCTXC0_SW2_PRE1_DEFAULT | \
								 XTP_LN_TX_LCTXC0_SW3_PRE0_DEFAULT)

#define MTK_DP_LANE0_DRIVING_PARAM_6				0x1144
#define MTK_DP_LANE1_DRIVING_PARAM_6				0x1244
#define MTK_DP_LANE2_DRIVING_PARAM_6				0x1344
#define MTK_DP_LANE3_DRIVING_PARAM_6				0x1444
# define XTP_LN_TX_LCTXCP1_SW0_PRE0_DEFAULT			0x00
# define XTP_LN_TX_LCTXCP1_SW0_PRE1_DEFAULT			(0x04 << 8)
# define XTP_LN_TX_LCTXCP1_SW0_PRE2_DEFAULT			(0x08 << 16)
# define XTP_LN_TX_LCTXCP1_SW0_PRE3_DEFAULT			(0x10 << 24)
# define DRIVING_PARAM_6_DEFAULT				(XTP_LN_TX_LCTXCP1_SW0_PRE0_DEFAULT | \
								 XTP_LN_TX_LCTXCP1_SW0_PRE1_DEFAULT | \
								 XTP_LN_TX_LCTXCP1_SW0_PRE2_DEFAULT | \
								 XTP_LN_TX_LCTXCP1_SW0_PRE3_DEFAULT)

#define MTK_DP_LANE0_DRIVING_PARAM_7				0x1148
#define MTK_DP_LANE1_DRIVING_PARAM_7				0x1248
#define MTK_DP_LANE2_DRIVING_PARAM_7				0x1348
#define MTK_DP_LANE3_DRIVING_PARAM_7				0x1448
# define XTP_LN_TX_LCTXCP1_SW1_PRE0_DEFAULT			0x00
# define XTP_LN_TX_LCTXCP1_SW1_PRE1_DEFAULT			(0x06 << 8)
# define XTP_LN_TX_LCTXCP1_SW1_PRE2_DEFAULT			(0x0c << 16)
# define XTP_LN_TX_LCTXCP1_SW2_PRE0_DEFAULT			(0x00 << 24)
# define DRIVING_PARAM_7_DEFAULT				(XTP_LN_TX_LCTXCP1_SW1_PRE0_DEFAULT | \
								 XTP_LN_TX_LCTXCP1_SW1_PRE1_DEFAULT | \
								 XTP_LN_TX_LCTXCP1_SW1_PRE2_DEFAULT | \
								 XTP_LN_TX_LCTXCP1_SW2_PRE0_DEFAULT)

#define MTK_DP_LANE0_DRIVING_PARAM_8				0x114C
#define MTK_DP_LANE1_DRIVING_PARAM_8				0x214C
#define MTK_DP_LANE2_DRIVING_PARAM_8				0x314C
#define MTK_DP_LANE3_DRIVING_PARAM_8				0x414C
# define XTP_LN_TX_LCTXCP1_SW2_PRE1_DEFAULT			0x08
# define XTP_LN_TX_LCTXCP1_SW3_PRE0_DEFAULT			(0x00 << 8)
# define DRIVING_PARAM_8_DEFAULT				(XTP_LN_TX_LCTXCP1_SW2_PRE1_DEFAULT | \
								 XTP_LN_TX_LCTXCP1_SW3_PRE0_DEFAULT)

#endif /*_MTK_DP_REG_H_*/
