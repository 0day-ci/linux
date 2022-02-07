// SPDX-License-Identifier: GPL-2.0-only
/*
 * mt9m114.c onsemi MT9M114 sensor driver
 *
 * Copyright (c) 2020 Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 * Copyright (c) 2012 Analog Devices Inc.
 *
 * Almost complete rewrite of work by Scott Jiang <Scott.Jiang.Linux@gmail.com>
 * itself based on work from Andrew Chew <achew@nvidia.com>.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/videodev2.h>

#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-subdev.h>

#define MT9M114_REG_8BIT(n)				((0 << 16) | (n))
#define MT9M114_REG_16BIT(n)				((1 << 16) | (n))
#define MT9M114_REG_32BIT(n)				((3 << 16) | (n))
#define MT9M114_REG_SIZE_SHIFT				16
#define MT9M114_REG_ADDR_MASK				0xffff

/* Sysctl registers */
#define MT9M114_CHIP_ID					MT9M114_REG_16BIT(0x0000)
#define MT9M114_COMMAND_REGISTER			MT9M114_REG_16BIT(0x0080)
#define MT9M114_COMMAND_REGISTER_APPLY_PATCH			BIT(0)
#define MT9M114_COMMAND_REGISTER_SET_STATE			BIT(1)
#define MT9M114_COMMAND_REGISTER_REFRESH			BIT(2)
#define MT9M114_COMMAND_REGISTER_WAIT_FOR_EVENT			BIT(3)
#define MT9M114_COMMAND_REGISTER_OK				BIT(15)
#define MT9M114_RESET_AND_MISC_CONTROL			MT9M114_REG_16BIT(0x001a)
#define MT9M114_RESET_SOC					BIT(0)
#define MT9M114_PAD_SLEW				MT9M114_REG_16BIT(0x001e)
#define MT9M114_PAD_CONTROL				MT9M114_REG_16BIT(0x0032)

/* XDMA registers */
#define MT9M114_ACCESS_CTL_STAT				MT9M114_REG_16BIT(0x0982)
#define MT9M114_PHYSICAL_ADDRESS_ACCESS			MT9M114_REG_16BIT(0x098a)
#define MT9M114_LOGICAL_ADDRESS_ACCESS			MT9M114_REG_16BIT(0x098e)

/* Sensor Core registers */
#define MT9M114_COARSE_INTEGRATION_TIME			MT9M114_REG_16BIT(0x3012)
#define MT9M114_FINE_INTEGRATION_TIME			MT9M114_REG_16BIT(0x3014)
#define MT9M114_RESET_REGISTER				MT9M114_REG_16BIT(0x301a)
#define MT9M114_RESET_REGISTER_LOCK_REG				BIT(3)
#define MT9M114_RESET_REGISTER_MASK_BAD				BIT(9)
#define MT9M114_FLASH					MT9M114_REG_16BIT(0x3046)
#define MT9M114_GREEN1_GAIN				MT9M114_REG_16BIT(0x3056)
#define MT9M114_BLUE_GAIN				MT9M114_REG_16BIT(0x3058)
#define MT9M114_RED_GAIN				MT9M114_REG_16BIT(0x305a)
#define MT9M114_GREEN2_GAIN				MT9M114_REG_16BIT(0x305c)
#define MT9M114_GLOBAL_GAIN				MT9M114_REG_16BIT(0x305e)
#define MT9M114_GAIN_DIGITAL_GAIN(n)				((n) << 12)
#define MT9M114_GAIN_DIGITAL_GAIN_MASK				(0xf << 12)
#define MT9M114_GAIN_ANALOG_GAIN(n)				((n) << 0)
#define MT9M114_GAIN_ANALOG_GAIN_MASK				(0xff << 0)
#define MT9M114_CUSTOMER_REV				MT9M114_REG_16BIT(0x31fe)

/* Monitor registers */
#define MT9M114_MON_MAJOR_VERSION			MT9M114_REG_16BIT(0x8000)
#define MT9M114_MON_MINOR_VERSION			MT9M114_REG_16BIT(0x8002)
#define MT9M114_MON_RELEASE_VERSION			MT9M114_REG_16BIT(0x8004)

/* Auto-Exposure Track registers */
#define MT9M114_AE_TRACK_ALGO				MT9M114_REG_16BIT(0xa804)
#define MT9M114_AE_TRACK_EXEC_AUTOMATIC_EXPOSURE		BIT(0)
#define MT9M114_AE_TRACK_AE_TRACKING_DAMPENING_SPEED	MT9M114_REG_8BIT(0xa80a)

/* Color Correction Matrix registers */
#define MT9M114_CCM_ALGO				MT9M114_REG_16BIT(0xb404)
#define MT9M114_CCM_EXEC_CALC_CCM_MATRIX			BIT(4)
#define MT9M114_CCM_DELTA_GAIN				MT9M114_REG_8BIT(0xb42a)

/* Camera Control registers */
#define MT9M114_CAM_SENSOR_CFG_Y_ADDR_START		MT9M114_REG_16BIT(0xc800)
#define MT9M114_CAM_SENSOR_CFG_X_ADDR_START		MT9M114_REG_16BIT(0xc802)
#define MT9M114_CAM_SENSOR_CFG_Y_ADDR_END		MT9M114_REG_16BIT(0xc804)
#define MT9M114_CAM_SENSOR_CFG_X_ADDR_END		MT9M114_REG_16BIT(0xc806)
#define MT9M114_CAM_SENSOR_CFG_PIXCLK			MT9M114_REG_32BIT(0xc808)
#define MT9M114_CAM_SENSOR_CFG_ROW_SPEED		MT9M114_REG_16BIT(0xc80c)
#define MT9M114_CAM_SENSOR_CFG_FINE_INTEG_TIME_MIN	MT9M114_REG_16BIT(0xc80e)
#define MT9M114_CAM_SENSOR_CFG_FINE_INTEG_TIME_MAX	MT9M114_REG_16BIT(0xc810)
#define MT9M114_CAM_SENSOR_CFG_FRAME_LENGTH_LINES	MT9M114_REG_16BIT(0xc812)
#define MT9M114_CAM_SENSOR_CFG_LINE_LENGTH_PCK		MT9M114_REG_16BIT(0xc814)
#define MT9M114_CAM_SENSOR_CFG_FINE_CORRECTION		MT9M114_REG_16BIT(0xc816)
#define MT9M114_CAM_SENSOR_CFG_CPIPE_LAST_ROW		MT9M114_REG_16BIT(0xc818)
#define MT9M114_CAM_SENSOR_CFG_REG_0_DATA		MT9M114_REG_16BIT(0xc826)
#define MT9M114_CAM_SENSOR_CONTROL_READ_MODE		MT9M114_REG_16BIT(0xc834)
#define MT9M114_CAM_SENSOR_CONTROL_HORZ_MIRROR_EN		BIT(0)
#define MT9M114_CAM_SENSOR_CONTROL_VERT_FLIP_EN			BIT(1)
#define MT9M114_CAM_SENSOR_CONTROL_X_READ_OUT_NORMAL		(0 << 4)
#define MT9M114_CAM_SENSOR_CONTROL_X_READ_OUT_SKIPPING		(1 << 4)
#define MT9M114_CAM_SENSOR_CONTROL_X_READ_OUT_AVERAGE		(2 << 4)
#define MT9M114_CAM_SENSOR_CONTROL_X_READ_OUT_SUMMING		(3 << 4)
#define MT9M114_CAM_SENSOR_CONTROL_X_READ_OUT_MASK		(3 << 4)
#define MT9M114_CAM_SENSOR_CONTROL_Y_READ_OUT_NORMAL		(0 << 8)
#define MT9M114_CAM_SENSOR_CONTROL_Y_READ_OUT_SKIPPING		(1 << 8)
#define MT9M114_CAM_SENSOR_CONTROL_Y_READ_OUT_AVERAGE		(2 << 8)
#define MT9M114_CAM_SENSOR_CONTROL_Y_READ_OUT_SUMMING		(3 << 8)
#define MT9M114_CAM_SENSOR_CONTROL_Y_READ_OUT_MASK		(3 << 8)
#define MT9M114_CAM_SENSOR_CONTROL_ANALOG_GAIN		MT9M114_REG_16BIT(0xc836)
#define MT9M114_CAM_SENSOR_CONTROL_COARSE_INTEGRATION_TIME	MT9M114_REG_16BIT(0xc83c)
#define MT9M114_CAM_SENSOR_CONTROL_FINE_INTEGRATION_TIME	MT9M114_REG_16BIT(0xc83e)
#define MT9M114_CAM_MODE_SELECT				MT9M114_REG_8BIT(0xc84c)
#define MT9M114_CAM_MODE_SELECT_NORMAL				(0 << 0)
#define MT9M114_CAM_MODE_SELECT_LENS_CALIBRATION		(1 << 0)
#define MT9M114_CAM_MODE_SELECT_TEST_PATTERN			(2 << 0)
#define MT9M114_CAM_MODE_TEST_PATTERN_SELECT		MT9M114_REG_8BIT(0xc84d)
#define MT9M114_CAM_MODE_TEST_PATTERN_SELECT_SOLID		(1 << 0)
#define MT9M114_CAM_MODE_TEST_PATTERN_SELECT_SOLID_BARS		(4 << 0)
#define MT9M114_CAM_MODE_TEST_PATTERN_SELECT_RANDOM		(5 << 0)
#define MT9M114_CAM_MODE_TEST_PATTERN_SELECT_FADING_BARS	(8 << 0)
#define MT9M114_CAM_MODE_TEST_PATTERN_SELECT_WALKING_1S_10B	(10 << 0)
#define MT9M114_CAM_MODE_TEST_PATTERN_SELECT_WALKING_1S_8B	(11 << 0)
#define MT9M114_CAM_MODE_TEST_PATTERN_RED		MT9M114_REG_16BIT(0xc84e)
#define MT9M114_CAM_MODE_TEST_PATTERN_GREEN		MT9M114_REG_16BIT(0xc850)
#define MT9M114_CAM_MODE_TEST_PATTERN_BLUE		MT9M114_REG_16BIT(0xc852)
#define MT9M114_CAM_CROP_WINDOW_XOFFSET			MT9M114_REG_16BIT(0xc854)
#define MT9M114_CAM_CROP_WINDOW_YOFFSET			MT9M114_REG_16BIT(0xc856)
#define MT9M114_CAM_CROP_WINDOW_WIDTH			MT9M114_REG_16BIT(0xc858)
#define MT9M114_CAM_CROP_WINDOW_HEIGHT			MT9M114_REG_16BIT(0xc85a)
#define MT9M114_CAM_CROP_CROPMODE			MT9M114_REG_8BIT(0xc85c)
#define MT9M114_CAM_CROP_MODE_AE_AUTO_CROP_EN			BIT(0)
#define MT9M114_CAM_CROP_MODE_AWB_AUTO_CROP_EN			BIT(1)
#define MT9M114_CAM_OUTPUT_WIDTH			MT9M114_REG_16BIT(0xc868)
#define MT9M114_CAM_OUTPUT_HEIGHT			MT9M114_REG_16BIT(0xc86a)
#define MT9M114_CAM_OUTPUT_FORMAT			MT9M114_REG_16BIT(0xc86c)
#define MT9M114_CAM_OUTPUT_FORMAT_SWAP_RED_BLUE			BIT(0)
#define MT9M114_CAM_OUTPUT_FORMAT_SWAP_BYTES			BIT(1)
#define MT9M114_CAM_OUTPUT_FORMAT_MONO_ENABLE			BIT(2)
#define MT9M114_CAM_OUTPUT_FORMAT_BT656_ENABLE			BIT(3)
#define MT9M114_CAM_OUTPUT_FORMAT_BT656_CROP_SCALE_DISABLE	BIT(4)
#define MT9M114_CAM_OUTPUT_FORMAT_FVLV_DISABLE			BIT(5)
#define MT9M114_CAM_OUTPUT_FORMAT_FORMAT_YUV			(0 << 8)
#define MT9M114_CAM_OUTPUT_FORMAT_FORMAT_RGB			(1 << 8)
#define MT9M114_CAM_OUTPUT_FORMAT_FORMAT_BAYER			(2 << 8)
#define MT9M114_CAM_OUTPUT_FORMAT_FORMAT_NONE			(3 << 8)
#define MT9M114_CAM_OUTPUT_FORMAT_FORMAT_MASK			(3 << 8)
#define MT9M114_CAM_OUTPUT_FORMAT_BAYER_FORMAT_RAWR10		(0 << 10)
#define MT9M114_CAM_OUTPUT_FORMAT_BAYER_FORMAT_PRELSC_8_2	(1 << 10)
#define MT9M114_CAM_OUTPUT_FORMAT_BAYER_FORMAT_POSTLSC_8_2	(2 << 10)
#define MT9M114_CAM_OUTPUT_FORMAT_BAYER_FORMAT_PROCESSED8	(3 << 10)
#define MT9M114_CAM_OUTPUT_FORMAT_BAYER_FORMAT_MASK		(3 << 10)
#define MT9M114_CAM_OUTPUT_FORMAT_RGB_FORMAT_565RGB		(0 << 12)
#define MT9M114_CAM_OUTPUT_FORMAT_RGB_FORMAT_555RGB		(1 << 12)
#define MT9M114_CAM_OUTPUT_FORMAT_RGB_FORMAT_444xRGB		(2 << 12)
#define MT9M114_CAM_OUTPUT_FORMAT_RGB_FORMAT_444RGBx		(3 << 12)
#define MT9M114_CAM_OUTPUT_FORMAT_RGB_FORMAT_MASK		(3 << 12)
#define MT9M114_CAM_OUTPUT_FORMAT_YUV			MT9M114_REG_16BIT(0xc86e)
#define MT9M114_CAM_OUTPUT_FORMAT_YUV_CLIP			BIT(5)
#define MT9M114_CAM_OUTPUT_FORMAT_YUV_AUV_OFFSET		BIT(4)
#define MT9M114_CAM_OUTPUT_FORMAT_YUV_SELECT_601		BIT(3)
#define MT9M114_CAM_OUTPUT_FORMAT_YUV_NORMALISE			BIT(2)
#define MT9M114_CAM_OUTPUT_FORMAT_YUV_SAMPLING_EVEN_UV		(0 << 0)
#define MT9M114_CAM_OUTPUT_FORMAT_YUV_SAMPLING_ODD_UV		(1 << 0)
#define MT9M114_CAM_OUTPUT_FORMAT_YUV_SAMPLING_EVENU_ODDV	(2 << 0)
#define MT9M114_CAM_OUTPUT_Y_OFFSET			MT9M114_REG_8BIT(0xc870)
#define MT9M114_CAM_AET_AEMODE				MT9M114_REG_8BIT(0xc878)
#define MT9M114_CAM_AET_EXEC_SET_INDOOR				BIT(0)
#define MT9M114_CAM_AET_DISCRETE_FRAMERATE			BIT(1)
#define MT9M114_CAM_AET_ADAPTATIVE_TARGET_LUMA			BIT(2)
#define MT9M114_CAM_AET_ADAPTATIVE_SKIP_FRAMES			BIT(3)
#define MT9M114_CAM_AET_SKIP_FRAMES			MT9M114_REG_8BIT(0xc879)
#define MT9M114_CAM_AET_TARGET_AVERAGE_LUMA		MT9M114_REG_8BIT(0xc87a)
#define MT9M114_CAM_AET_TARGET_AVERAGE_LUMA_DARK	MT9M114_REG_8BIT(0xc87b)
#define MT9M114_CAM_AET_BLACK_CLIPPING_TARGET		MT9M114_REG_16BIT(0xc87c)
#define MT9M114_CAM_AET_AE_MIN_VIRT_INT_TIME_PCLK	MT9M114_REG_16BIT(0xc87e)
#define MT9M114_CAM_AET_AE_MIN_VIRT_DGAIN		MT9M114_REG_16BIT(0xc880)
#define MT9M114_CAM_AET_AE_MAX_VIRT_DGAIN		MT9M114_REG_16BIT(0xc882)
#define MT9M114_CAM_AET_AE_MIN_VIRT_AGAIN		MT9M114_REG_16BIT(0xc884)
#define MT9M114_CAM_AET_AE_MAX_VIRT_AGAIN		MT9M114_REG_16BIT(0xc886)
#define MT9M114_CAM_AET_AE_VIRT_GAIN_TH_EG		MT9M114_REG_16BIT(0xc888)
#define MT9M114_CAM_AET_AE_EG_GATE_PERCENTAGE		MT9M114_REG_8BIT(0xc88a)
#define MT9M114_CAM_AET_FLICKER_FREQ_HZ			MT9M114_REG_8BIT(0xc88b)
#define MT9M114_CAM_AET_MAX_FRAME_RATE			MT9M114_REG_16BIT(0xc88c)
#define MT9M114_CAM_AET_MIN_FRAME_RATE			MT9M114_REG_16BIT(0xc88e)
#define MT9M114_CAM_AET_TARGET_GAIN			MT9M114_REG_16BIT(0xc890)
#define MT9M114_CAM_AWB_CCM_L(n)			MT9M114_REG_16BIT(0xc892 + (n) * 2)
#define MT9M114_CAM_AWB_CCM_M(n)			MT9M114_REG_16BIT(0xc8a4 + (n) * 2)
#define MT9M114_CAM_AWB_CCM_R(n)			MT9M114_REG_16BIT(0xc8b6 + (n) * 2)
#define MT9M114_CAM_AWB_CCM_L_RG_GAIN			MT9M114_REG_16BIT(0xc8c8)
#define MT9M114_CAM_AWB_CCM_L_BG_GAIN			MT9M114_REG_16BIT(0xc8ca)
#define MT9M114_CAM_AWB_CCM_M_RG_GAIN			MT9M114_REG_16BIT(0xc8cc)
#define MT9M114_CAM_AWB_CCM_M_BG_GAIN			MT9M114_REG_16BIT(0xc8ce)
#define MT9M114_CAM_AWB_CCM_R_RG_GAIN			MT9M114_REG_16BIT(0xc8d0)
#define MT9M114_CAM_AWB_CCM_R_BG_GAIN			MT9M114_REG_16BIT(0xc8d2)
#define MT9M114_CAM_AWB_CCM_L_CTEMP			MT9M114_REG_16BIT(0xc8d4)
#define MT9M114_CAM_AWB_CCM_M_CTEMP			MT9M114_REG_16BIT(0xc8d6)
#define MT9M114_CAM_AWB_CCM_R_CTEMP			MT9M114_REG_16BIT(0xc8d8)
#define MT9M114_CAM_AWB_AWB_XSCALE			MT9M114_REG_8BIT(0xc8f2)
#define MT9M114_CAM_AWB_AWB_YSCALE			MT9M114_REG_8BIT(0xc8f3)
#define MT9M114_CAM_AWB_AWB_WEIGHTS(n)			MT9M114_REG_16BIT(0xc8f4 + (n) * 2)
#define MT9M114_CAM_AWB_AWB_XSHIFT_PRE_ADJ		MT9M114_REG_16BIT(0xc904)
#define MT9M114_CAM_AWB_AWB_YSHIFT_PRE_ADJ		MT9M114_REG_16BIT(0xc906)
#define MT9M114_CAM_AWB_AWBMODE				MT9M114_REG_8BIT(0xc909)
#define MT9M114_CAM_AWB_MODE_AUTO				BIT(1)
#define MT9M114_CAM_AWB_MODE_EXCLUSIVE_AE			BIT(0)
#define MT9M114_CAM_AWB_K_R_L				MT9M114_REG_8BIT(0xc90c)
#define MT9M114_CAM_AWB_K_G_L				MT9M114_REG_8BIT(0xc90d)
#define MT9M114_CAM_AWB_K_B_L				MT9M114_REG_8BIT(0xc90e)
#define MT9M114_CAM_AWB_K_R_R				MT9M114_REG_8BIT(0xc90f)
#define MT9M114_CAM_AWB_K_G_R				MT9M114_REG_8BIT(0xc910)
#define MT9M114_CAM_AWB_K_B_R				MT9M114_REG_8BIT(0xc911)
#define MT9M114_CAM_STAT_AWB_CLIP_WINDOW_XSTART		MT9M114_REG_16BIT(0xc914)
#define MT9M114_CAM_STAT_AWB_CLIP_WINDOW_YSTART		MT9M114_REG_16BIT(0xc916)
#define MT9M114_CAM_STAT_AWB_CLIP_WINDOW_XEND		MT9M114_REG_16BIT(0xc918)
#define MT9M114_CAM_STAT_AWB_CLIP_WINDOW_YEND		MT9M114_REG_16BIT(0xc91a)
#define MT9M114_CAM_STAT_AE_INITIAL_WINDOW_XSTART	MT9M114_REG_16BIT(0xc91c)
#define MT9M114_CAM_STAT_AE_INITIAL_WINDOW_YSTART	MT9M114_REG_16BIT(0xc91e)
#define MT9M114_CAM_STAT_AE_INITIAL_WINDOW_XEND		MT9M114_REG_16BIT(0xc920)
#define MT9M114_CAM_STAT_AE_INITIAL_WINDOW_YEND		MT9M114_REG_16BIT(0xc922)
#define MT9M114_CAM_LL_LLMODE				MT9M114_REG_16BIT(0xc924)
#define MT9M114_CAM_LL_START_BRIGHTNESS			MT9M114_REG_16BIT(0xc926)
#define MT9M114_CAM_LL_STOP_BRIGHTNESS			MT9M114_REG_16BIT(0xc928)
#define MT9M114_CAM_LL_START_SATURATION			MT9M114_REG_8BIT(0xc92a)
#define MT9M114_CAM_LL_END_SATURATION			MT9M114_REG_8BIT(0xc92b)
#define MT9M114_CAM_LL_START_DESATURATION		MT9M114_REG_8BIT(0xc92c)
#define MT9M114_CAM_LL_END_DESATURATION			MT9M114_REG_8BIT(0xc92d)
#define MT9M114_CAM_LL_START_DEMOSAICING		MT9M114_REG_8BIT(0xc92e)
#define MT9M114_CAM_LL_START_AP_GAIN			MT9M114_REG_8BIT(0xc92f)
#define MT9M114_CAM_LL_START_AP_THRESH			MT9M114_REG_8BIT(0xc930)
#define MT9M114_CAM_LL_STOP_DEMOSAICING			MT9M114_REG_8BIT(0xc931)
#define MT9M114_CAM_LL_STOP_AP_GAIN			MT9M114_REG_8BIT(0xc932)
#define MT9M114_CAM_LL_STOP_AP_THRESH			MT9M114_REG_8BIT(0xc933)
#define MT9M114_CAM_LL_START_NR_RED			MT9M114_REG_8BIT(0xc934)
#define MT9M114_CAM_LL_START_NR_GREEN			MT9M114_REG_8BIT(0xc935)
#define MT9M114_CAM_LL_START_NR_BLUE			MT9M114_REG_8BIT(0xc936)
#define MT9M114_CAM_LL_START_NR_THRESH			MT9M114_REG_8BIT(0xc937)
#define MT9M114_CAM_LL_STOP_NR_RED			MT9M114_REG_8BIT(0xc938)
#define MT9M114_CAM_LL_STOP_NR_GREEN			MT9M114_REG_8BIT(0xc939)
#define MT9M114_CAM_LL_STOP_NR_BLUE			MT9M114_REG_8BIT(0xc93a)
#define MT9M114_CAM_LL_STOP_NR_THRESH			MT9M114_REG_8BIT(0xc93b)
#define MT9M114_CAM_LL_START_CONTRAST_BM		MT9M114_REG_16BIT(0xc93c)
#define MT9M114_CAM_LL_STOP_CONTRAST_BM			MT9M114_REG_16BIT(0xc93e)
#define MT9M114_CAM_LL_GAMMA				MT9M114_REG_16BIT(0xc940)
#define MT9M114_CAM_LL_START_CONTRAST_GRADIENT		MT9M114_REG_8BIT(0xc942)
#define MT9M114_CAM_LL_STOP_CONTRAST_GRADIENT		MT9M114_REG_8BIT(0xc943)
#define MT9M114_CAM_LL_START_CONTRAST_LUMA_PERCENTAGE	MT9M114_REG_8BIT(0xc944)
#define MT9M114_CAM_LL_STOP_CONTRAST_LUMA_PERCENTAGE	MT9M114_REG_8BIT(0xc945)
#define MT9M114_CAM_LL_START_GAIN_METRIC		MT9M114_REG_16BIT(0xc946)
#define MT9M114_CAM_LL_STOP_GAIN_METRIC			MT9M114_REG_16BIT(0xc948)
#define MT9M114_CAM_LL_START_FADE_TO_BLACK_LUMA		MT9M114_REG_16BIT(0xc94a)
#define MT9M114_CAM_LL_STOP_FADE_TO_BLACK_LUMA		MT9M114_REG_16BIT(0xc94c)
#define MT9M114_CAM_LL_CLUSTER_DC_TH_BM			MT9M114_REG_16BIT(0xc94e)
#define MT9M114_CAM_LL_CLUSTER_DC_GATE_PERCENTAGE	MT9M114_REG_8BIT(0xc950)
#define MT9M114_CAM_LL_SUMMING_SENSITIVITY_FACTOR	MT9M114_REG_8BIT(0xc951)
#define MT9M114_CAM_LL_START_TARGET_LUMA_BM		MT9M114_REG_16BIT(0xc952)
#define MT9M114_CAM_LL_STOP_TARGET_LUMA_BM		MT9M114_REG_16BIT(0xc954)
#define MT9M114_CAM_PGA_PGA_CONTROL			MT9M114_REG_16BIT(0xc95e)
#define MT9M114_CAM_SYSCTL_PLL_ENABLE			MT9M114_REG_8BIT(0xc97e)
#define MT9M114_CAM_SYSCTL_PLL_ENABLE_VALUE			BIT(0)
#define MT9M114_CAM_SYSCTL_PLL_DIVIDER_M_N		MT9M114_REG_16BIT(0xc980)
#define MT9M114_CAM_SYSCTL_PLL_DIVIDER_VALUE(m, n)		(((n) << 8) | (m))
#define MT9M114_CAM_SYSCTL_PLL_DIVIDER_P		MT9M114_REG_16BIT(0xc982)
#define MT9M114_CAM_SYSCTL_PLL_DIVIDER_P_VALUE(p)		((p) << 8)
#define MT9M114_CAM_PORT_OUTPUT_CONTROL			MT9M114_REG_16BIT(0xc984)
#define MT9M114_CAM_PORT_PORT_SELECT_PARALLEL			(0 << 0)
#define MT9M114_CAM_PORT_PORT_SELECT_MIPI			(1 << 0)
#define MT9M114_CAM_PORT_CLOCK_SLOWDOWN				BIT(3)
#define MT9M114_CAM_PORT_TRUNCATE_RAW_BAYER			BIT(4)
#define MT9M114_CAM_PORT_PIXCLK_GATE				BIT(5)
#define MT9M114_CAM_PORT_CONT_MIPI_CLK				BIT(6)
#define MT9M114_CAM_PORT_CHAN_NUM(vc)				((vc) << 8)
#define MT9M114_CAM_PORT_MIPI_TIMING_T_HS_ZERO		MT9M114_REG_16BIT(0xc988)
#define MT9M114_CAM_PORT_MIPI_TIMING_T_HS_ZERO_VALUE(n)		((n) << 8)
#define MT9M114_CAM_PORT_MIPI_TIMING_T_HS_EXIT_TRAIL	MT9M114_REG_16BIT(0xc98a)
#define MT9M114_CAM_PORT_MIPI_TIMING_T_HS_EXIT_VALUE(n)		((n) << 8)
#define MT9M114_CAM_PORT_MIPI_TIMING_T_HS_TRAIL_VALUE(n)	((n) << 0)
#define MT9M114_CAM_PORT_MIPI_TIMING_T_CLK_POST_PRE	MT9M114_REG_16BIT(0xc98c)
#define MT9M114_CAM_PORT_MIPI_TIMING_T_CLK_POST_VALUE(n)	((n) << 8)
#define MT9M114_CAM_PORT_MIPI_TIMING_T_CLK_PRE_VALUE(n)		((n) << 0)
#define MT9M114_CAM_PORT_MIPI_TIMING_T_CLK_TRAIL_ZERO	MT9M114_REG_16BIT(0xc98e)
#define MT9M114_CAM_PORT_MIPI_TIMING_T_CLK_TRAIL_VALUE(n)	((n) << 8)
#define MT9M114_CAM_PORT_MIPI_TIMING_T_CLK_ZERO_VALUE(n)	((n) << 0)

/* System Manager registers */
#define MT9M114_SYSMGR_NEXT_STATE			MT9M114_REG_8BIT(0xdc00)
#define MT9M114_SYSMGR_CURRENT_STATE			MT9M114_REG_8BIT(0xdc01)
#define MT9M114_SYSMGR_CMD_STATUS			MT9M114_REG_8BIT(0xdc02)

/* Patch Loader registers */
#define MT9M114_PATCHLDR_LOADER_ADDRESS			MT9M114_REG_16BIT(0xe000)
#define MT9M114_PATCHLDR_PATCH_ID			MT9M114_REG_16BIT(0xe002)
#define MT9M114_PATCHLDR_FIRMWARE_ID			MT9M114_REG_32BIT(0xe004)
#define MT9M114_PATCHLDR_APPLY_STATUS			MT9M114_REG_8BIT(0xe008)
#define MT9M114_PATCHLDR_NUM_PATCHES			MT9M114_REG_8BIT(0xe009)
#define MT9M114_PATCHLDR_PATCH_ID_0			MT9M114_REG_16BIT(0xe00a)
#define MT9M114_PATCHLDR_PATCH_ID_1			MT9M114_REG_16BIT(0xe00c)
#define MT9M114_PATCHLDR_PATCH_ID_2			MT9M114_REG_16BIT(0xe00e)
#define MT9M114_PATCHLDR_PATCH_ID_3			MT9M114_REG_16BIT(0xe010)
#define MT9M114_PATCHLDR_PATCH_ID_4			MT9M114_REG_16BIT(0xe012)
#define MT9M114_PATCHLDR_PATCH_ID_5			MT9M114_REG_16BIT(0xe014)
#define MT9M114_PATCHLDR_PATCH_ID_6			MT9M114_REG_16BIT(0xe016)
#define MT9M114_PATCHLDR_PATCH_ID_7			MT9M114_REG_16BIT(0xe018)

/* SYS_STATE values (for SYSMGR_NEXT_STATE and SYSMGR_CURRENT_STATE) */
#define MT9M114_SYS_STATE_ENTER_CONFIG_CHANGE		0x28
#define MT9M114_SYS_STATE_STREAMING			0x31
#define MT9M114_SYS_STATE_START_STREAMING		0x34
#define MT9M114_SYS_STATE_ENTER_SUSPEND			0x40
#define MT9M114_SYS_STATE_SUSPENDED			0x41
#define MT9M114_SYS_STATE_ENTER_STANDBY			0x50
#define MT9M114_SYS_STATE_STANDBY			0x52
#define MT9M114_SYS_STATE_LEAVE_STANDBY			0x54

/* Result status of last SET_STATE comamnd */
#define MT9M114_SET_STATE_RESULT_ENOERR			0x00
#define MT9M114_SET_STATE_RESULT_EINVAL			0x0c
#define MT9M114_SET_STATE_RESULT_ENOSPC			0x0d

#define MT9M114_MAX_FRAME_RATE				30
#define MT9M114_LINE_LENGTH				1590
#define MT9M114_FRAME_LENGTH				1006

#define MT9M114_PIXEL_ARRAY_WIDTH			1296U
#define MT9M114_PIXEL_ARRAY_HEIGHT			976U

/*
 * These values are not well documented and are semi-arbitrary. The pixel array
 * minimum output size is 8 pixels larger than the minimum scaler cropped input
 * width to account for the demosaicing.
 */
#define MT9M114_PIXEL_ARRAY_MIN_OUTPUT_WIDTH		(32U + 8U)
#define MT9M114_PIXEL_ARRAY_MIN_OUTPUT_HEIGHT		(32U + 8U)
#define MT9M114_SCALER_CROPPED_INPUT_WIDTH		32U
#define MT9M114_SCALER_CROPPED_INPUT_HEIGHT		32U

/* Indices into the mt9m114.ifp.tpg array. */
#define MT9M114_TPG_PATTERN				0
#define MT9M114_TPG_RED					1
#define MT9M114_TPG_GREEN				2
#define MT9M114_TPG_BLUE				3

/* -----------------------------------------------------------------------------
 * Data Structures
 */

enum mt9m114_format_flag {
	MT9M114_FMT_FLAG_PARALLEL = BIT(0),
	MT9M114_FMT_FLAG_CSI2 = BIT(1),
};

struct mt9m114_format_info {
	u32 code;
	u32 output_format;
	u32 flags;
};

struct mt9m114_reg {
	u32 reg;
	u32 val;
};

struct mt9m114 {
	struct i2c_client *client;

	struct clk *clk;
	struct gpio_desc *reset;
	struct regulator_bulk_data supplies[3];
	struct v4l2_fwnode_endpoint bus_cfg;

	unsigned int pixrate;
	bool streaming;

	struct mutex lock;

	/* Pixel Array */
	struct {
		struct v4l2_subdev sd;
		struct media_pad pad;

		struct v4l2_mbus_framefmt format;
		struct v4l2_rect crop;

		struct v4l2_ctrl_handler hdl;
		struct v4l2_ctrl *exposure;
		struct v4l2_ctrl *gain;
	} pa;

	/* Image Flow Processor */
	struct {
		struct v4l2_subdev sd;
		struct media_pad pads[2];

		struct v4l2_mbus_framefmt formats[2];
		struct v4l2_rect crop;
		struct v4l2_rect compose;
		const struct mt9m114_format_info *info;

		struct v4l2_ctrl_handler hdl;
		unsigned int frame_rate;

		struct v4l2_ctrl *tpg[4];
	} ifp;
};

/* -----------------------------------------------------------------------------
 * Formats
 */

static const struct mt9m114_format_info mt9m114_format_infos[] = {
	{
		/*
		 * The first two entries are used as defaults, for parallel and
		 * CSI-2 buses respectively. Keep them in that order.
		 */
		.code = MEDIA_BUS_FMT_UYVY8_2X8,
		.flags = MT9M114_FMT_FLAG_PARALLEL,
		.output_format = MT9M114_CAM_OUTPUT_FORMAT_FORMAT_YUV,
	}, {
		.code = MEDIA_BUS_FMT_UYVY8_1X16,
		.flags = MT9M114_FMT_FLAG_CSI2,
		.output_format = MT9M114_CAM_OUTPUT_FORMAT_FORMAT_YUV,
	}, {
		.code = MEDIA_BUS_FMT_YUYV8_2X8,
		.flags = MT9M114_FMT_FLAG_PARALLEL,
		.output_format = MT9M114_CAM_OUTPUT_FORMAT_FORMAT_YUV
			       | MT9M114_CAM_OUTPUT_FORMAT_SWAP_BYTES,
	}, {
		.code = MEDIA_BUS_FMT_YUYV8_1X16,
		.flags = MT9M114_FMT_FLAG_CSI2,
		.output_format = MT9M114_CAM_OUTPUT_FORMAT_FORMAT_YUV
			       | MT9M114_CAM_OUTPUT_FORMAT_SWAP_BYTES,
	}, {
		.code = MEDIA_BUS_FMT_RGB565_2X8_LE,
		.flags = MT9M114_FMT_FLAG_PARALLEL,
		.output_format = MT9M114_CAM_OUTPUT_FORMAT_RGB_FORMAT_565RGB
			       | MT9M114_CAM_OUTPUT_FORMAT_FORMAT_RGB
			       | MT9M114_CAM_OUTPUT_FORMAT_SWAP_BYTES,
	}, {
		.code = MEDIA_BUS_FMT_RGB565_2X8_BE,
		.flags = MT9M114_FMT_FLAG_PARALLEL,
		.output_format = MT9M114_CAM_OUTPUT_FORMAT_RGB_FORMAT_565RGB
			       | MT9M114_CAM_OUTPUT_FORMAT_FORMAT_RGB,
	}, {
		.code = MEDIA_BUS_FMT_RGB565_1X16,
		.flags = MT9M114_FMT_FLAG_CSI2,
		.output_format = MT9M114_CAM_OUTPUT_FORMAT_RGB_FORMAT_565RGB
			       | MT9M114_CAM_OUTPUT_FORMAT_FORMAT_RGB,
	}, {
		.code = MEDIA_BUS_FMT_SGRBG8_1X8,
		.output_format = MT9M114_CAM_OUTPUT_FORMAT_BAYER_FORMAT_PROCESSED8
			       | MT9M114_CAM_OUTPUT_FORMAT_FORMAT_BAYER,
		.flags = MT9M114_FMT_FLAG_PARALLEL | MT9M114_FMT_FLAG_CSI2,
	}, {
		/* Keep the format compatible with the IFP sink pad last. */
		.code = MEDIA_BUS_FMT_SGRBG10_1X10,
		.output_format = MT9M114_CAM_OUTPUT_FORMAT_BAYER_FORMAT_RAWR10
			| MT9M114_CAM_OUTPUT_FORMAT_FORMAT_BAYER,
		.flags = MT9M114_FMT_FLAG_PARALLEL | MT9M114_FMT_FLAG_CSI2,
	}
};

static const struct mt9m114_format_info *
mt9m114_default_format_info(struct mt9m114 *sensor)
{
	if (sensor->bus_cfg.bus_type == V4L2_MBUS_CSI2_DPHY)
		return &mt9m114_format_infos[1];
	else
		return &mt9m114_format_infos[0];
}

static const struct mt9m114_format_info *
mt9m114_format_info(struct mt9m114 *sensor, unsigned int pad, u32 code)
{
	const unsigned int num_formats = ARRAY_SIZE(mt9m114_format_infos);
	unsigned int flag;
	unsigned int i;

	switch (pad) {
	case 0:
		return &mt9m114_format_infos[num_formats - 1];

	case 1:
		if (sensor->bus_cfg.bus_type == V4L2_MBUS_CSI2_DPHY)
			flag = MT9M114_FMT_FLAG_CSI2;
		else
			flag = MT9M114_FMT_FLAG_PARALLEL;

		for (i = 0; i < num_formats; ++i) {
			const struct mt9m114_format_info *info =
				&mt9m114_format_infos[i];

			if (info->code == code && info->flags & flag)
				return info;
		}

		return mt9m114_default_format_info(sensor);

	default:
		return NULL;
	}
}

/* -----------------------------------------------------------------------------
 * Initialization
 */

static const struct mt9m114_reg mt9m114_init[] = {
	{ MT9M114_RESET_REGISTER,			MT9M114_RESET_REGISTER_MASK_BAD |
							MT9M114_RESET_REGISTER_LOCK_REG |
							0x0010 },

	/* Sensor optimization */
	{ MT9M114_REG_16BIT(0x316a), 0x8270 },
	{ MT9M114_REG_16BIT(0x316c), 0x8270 },
	{ MT9M114_REG_16BIT(0x3ed0), 0x2305 },
	{ MT9M114_REG_16BIT(0x3ed2), 0x77cf },
	{ MT9M114_REG_16BIT(0x316e), 0x8202 },
	{ MT9M114_REG_16BIT(0x3180), 0x87ff },
	{ MT9M114_REG_16BIT(0x30d4), 0x6080 },
	{ MT9M114_REG_16BIT(0xa802), 0x0008 },

	{ MT9M114_REG_16BIT(0x3e14), 0xff39 },

	/* APGA */
	{ MT9M114_CAM_PGA_PGA_CONTROL,			0x0000 },

	/* Automatic White balance */
	{ MT9M114_CAM_AWB_CCM_L(0),			0x0267 },
	{ MT9M114_CAM_AWB_CCM_L(1),			0xff1a },
	{ MT9M114_CAM_AWB_CCM_L(2),			0xffb3 },
	{ MT9M114_CAM_AWB_CCM_L(3),			0xff80 },
	{ MT9M114_CAM_AWB_CCM_L(4),			0x0166 },
	{ MT9M114_CAM_AWB_CCM_L(5),			0x0003 },
	{ MT9M114_CAM_AWB_CCM_L(6),			0xff9a },
	{ MT9M114_CAM_AWB_CCM_L(7),			0xfeb4 },
	{ MT9M114_CAM_AWB_CCM_L(8),			0x024d },
	{ MT9M114_CAM_AWB_CCM_M(0),			0x01bf },
	{ MT9M114_CAM_AWB_CCM_M(1),			0xff01 },
	{ MT9M114_CAM_AWB_CCM_M(2),			0xfff3 },
	{ MT9M114_CAM_AWB_CCM_M(3),			0xff75 },
	{ MT9M114_CAM_AWB_CCM_M(4),			0x0198 },
	{ MT9M114_CAM_AWB_CCM_M(5),			0xfffd },
	{ MT9M114_CAM_AWB_CCM_M(6),			0xff9a },
	{ MT9M114_CAM_AWB_CCM_M(7),			0xfee7 },
	{ MT9M114_CAM_AWB_CCM_M(8),			0x02a8 },
	{ MT9M114_CAM_AWB_CCM_R(0),			0x01d9 },
	{ MT9M114_CAM_AWB_CCM_R(1),			0xff26 },
	{ MT9M114_CAM_AWB_CCM_R(2),			0xfff3 },
	{ MT9M114_CAM_AWB_CCM_R(3),			0xffb3 },
	{ MT9M114_CAM_AWB_CCM_R(4),			0x0132 },
	{ MT9M114_CAM_AWB_CCM_R(5),			0xffe8 },
	{ MT9M114_CAM_AWB_CCM_R(6),			0xffda },
	{ MT9M114_CAM_AWB_CCM_R(7),			0xfecd },
	{ MT9M114_CAM_AWB_CCM_R(8),			0x02c2 },
	{ MT9M114_CAM_AWB_CCM_L_RG_GAIN,		0x0075 },
	{ MT9M114_CAM_AWB_CCM_L_BG_GAIN,		0x011c },
	{ MT9M114_CAM_AWB_CCM_M_RG_GAIN,		0x009a },
	{ MT9M114_CAM_AWB_CCM_M_BG_GAIN,		0x0105 },
	{ MT9M114_CAM_AWB_CCM_R_RG_GAIN,		0x00a4 },
	{ MT9M114_CAM_AWB_CCM_R_BG_GAIN,		0x00ac },
	{ MT9M114_CAM_AWB_CCM_L_CTEMP,			0x0a8c },
	{ MT9M114_CAM_AWB_CCM_M_CTEMP,			0x0f0a },
	{ MT9M114_CAM_AWB_CCM_R_CTEMP,			0x1964 },
	{ MT9M114_CAM_AWB_AWB_XSHIFT_PRE_ADJ,		51 },
	{ MT9M114_CAM_AWB_AWB_YSHIFT_PRE_ADJ,		60 },
	{ MT9M114_CAM_AWB_AWB_XSCALE,			3 },
	{ MT9M114_CAM_AWB_AWB_YSCALE,			2 },
	{ MT9M114_CAM_AWB_AWB_WEIGHTS(0),		0x0000 },
	{ MT9M114_CAM_AWB_AWB_WEIGHTS(1),		0x0000 },
	{ MT9M114_CAM_AWB_AWB_WEIGHTS(2),		0x0000 },
	{ MT9M114_CAM_AWB_AWB_WEIGHTS(3),		0xe724 },
	{ MT9M114_CAM_AWB_AWB_WEIGHTS(4),		0x1583 },
	{ MT9M114_CAM_AWB_AWB_WEIGHTS(5),		0x2045 },
	{ MT9M114_CAM_AWB_AWB_WEIGHTS(6),		0x03ff },
	{ MT9M114_CAM_AWB_AWB_WEIGHTS(7),		0x007c },
	{ MT9M114_CAM_AWB_K_R_L,			0x80 },
	{ MT9M114_CAM_AWB_K_G_L,			0x80 },
	{ MT9M114_CAM_AWB_K_B_L,			0x80 },
	{ MT9M114_CAM_AWB_K_R_R,			0x88 },
	{ MT9M114_CAM_AWB_K_G_R,			0x80 },
	{ MT9M114_CAM_AWB_K_B_R,			0x80 },

	/* Low-Light Image Enhancements */
	{ MT9M114_CAM_LL_START_BRIGHTNESS,		0x0020 },
	{ MT9M114_CAM_LL_STOP_BRIGHTNESS,		0x009a },
	{ MT9M114_CAM_LL_START_GAIN_METRIC,		0x0070 },
	{ MT9M114_CAM_LL_STOP_GAIN_METRIC,		0x00f3 },
	{ MT9M114_CAM_LL_START_CONTRAST_LUMA_PERCENTAGE, 0x20 },
	{ MT9M114_CAM_LL_STOP_CONTRAST_LUMA_PERCENTAGE,	0x9a },
	{ MT9M114_CAM_LL_START_SATURATION,		0x80 },
	{ MT9M114_CAM_LL_END_SATURATION,		0x4b },
	{ MT9M114_CAM_LL_START_DESATURATION,		0x00 },
	{ MT9M114_CAM_LL_END_DESATURATION,		0xff },
	{ MT9M114_CAM_LL_START_DEMOSAICING,		0x3c },
	{ MT9M114_CAM_LL_START_AP_GAIN,			0x02 },
	{ MT9M114_CAM_LL_START_AP_THRESH,		0x06 },
	{ MT9M114_CAM_LL_STOP_DEMOSAICING,		0x64 },
	{ MT9M114_CAM_LL_STOP_AP_GAIN,			0x01 },
	{ MT9M114_CAM_LL_STOP_AP_THRESH,		0x0c },
	{ MT9M114_CAM_LL_START_NR_RED,			0x3c },
	{ MT9M114_CAM_LL_START_NR_GREEN,		0x3c },
	{ MT9M114_CAM_LL_START_NR_BLUE,			0x3c },
	{ MT9M114_CAM_LL_START_NR_THRESH,		0x0f },
	{ MT9M114_CAM_LL_STOP_NR_RED,			0x64 },
	{ MT9M114_CAM_LL_STOP_NR_GREEN,			0x64 },
	{ MT9M114_CAM_LL_STOP_NR_BLUE,			0x64 },
	{ MT9M114_CAM_LL_STOP_NR_THRESH,		0x32 },
	{ MT9M114_CAM_LL_START_CONTRAST_BM,		0x0020 },
	{ MT9M114_CAM_LL_STOP_CONTRAST_BM,		0x009a },
	{ MT9M114_CAM_LL_GAMMA,				0x00dc },
	{ MT9M114_CAM_LL_START_CONTRAST_GRADIENT,	0x38 },
	{ MT9M114_CAM_LL_STOP_CONTRAST_GRADIENT,	0x30 },
	{ MT9M114_CAM_LL_START_CONTRAST_LUMA_PERCENTAGE, 0x50 },
	{ MT9M114_CAM_LL_STOP_CONTRAST_LUMA_PERCENTAGE,	0x19 },
	{ MT9M114_CAM_LL_START_FADE_TO_BLACK_LUMA,	0x0230 },
	{ MT9M114_CAM_LL_STOP_FADE_TO_BLACK_LUMA,	0x0010 },
	{ MT9M114_CAM_LL_CLUSTER_DC_TH_BM,		0x01cd },
	{ MT9M114_CAM_LL_CLUSTER_DC_GATE_PERCENTAGE,	0x05 },
	{ MT9M114_CAM_LL_SUMMING_SENSITIVITY_FACTOR,	0x40 },

	/* Auto-Exposure */
	{ MT9M114_CAM_AET_TARGET_AVERAGE_LUMA_DARK,	0x1b },
	{ MT9M114_CAM_AET_AEMODE,			0x00 },
	{ MT9M114_CAM_AET_TARGET_GAIN,			0x0080 },
	{ MT9M114_CAM_AET_AE_MAX_VIRT_AGAIN,		0x0100 },
	{ MT9M114_CAM_AET_BLACK_CLIPPING_TARGET,	0x005a },

	{ MT9M114_CCM_DELTA_GAIN,			0x05 },
	{ MT9M114_AE_TRACK_AE_TRACKING_DAMPENING_SPEED,	0x20 },

	/* Pixel array timings and integration time */
	{ MT9M114_CAM_SENSOR_CFG_ROW_SPEED,		1 },
	{ MT9M114_CAM_SENSOR_CFG_FINE_INTEG_TIME_MIN,	219 },
	{ MT9M114_CAM_SENSOR_CFG_FINE_INTEG_TIME_MAX,	1459 },
	{ MT9M114_CAM_SENSOR_CFG_FRAME_LENGTH_LINES,	MT9M114_FRAME_LENGTH },
	{ MT9M114_CAM_SENSOR_CFG_LINE_LENGTH_PCK,	MT9M114_LINE_LENGTH },
	{ MT9M114_CAM_SENSOR_CFG_FINE_CORRECTION,	96 },
	{ MT9M114_CAM_SENSOR_CFG_REG_0_DATA,		32 },

	/* Miscellaneous settings */
	{ MT9M114_PAD_SLEW,				0x0777 },
};

/* -----------------------------------------------------------------------------
 * Hardware Configuration
 */

static int mt9m114_read(struct mt9m114 *sensor, u32 addr, u32 *value)
{
	struct i2c_client *client = sensor->client;
	__be16 reg;
	u8 val[4];
	struct i2c_msg msg[] = {
		{
			.addr	= client->addr,
			.flags	= 0,
			.len	= 2,
			.buf	= (u8 *)&reg,
		},
		{
			.addr	= client->addr,
			.flags	= I2C_M_RD,
			.buf	= (u8 *)&val,
		},
	};
	unsigned int len = ((addr >> MT9M114_REG_SIZE_SHIFT) & 3) + 1;
	unsigned int i;
	int ret;

	reg = cpu_to_be16(addr & MT9M114_REG_ADDR_MASK);

	msg[1].len = len;

	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to read register 0x%04x: %d\n",
			addr & MT9M114_REG_ADDR_MASK, ret);
		return ret;
	}

	*value = 0;
	for (i = 0; i < len; ++i) {
		*value <<= 8;
		*value |= val[i];
	}

	return 0;
}

static void mt9m114_write(struct mt9m114 *sensor, u32 addr, u32 value,
			  int *error)
{
	struct i2c_client *client = sensor->client;
	struct {
		__be16 reg;
		u8 val[4];
	} __packed buf;
	struct i2c_msg msg = {
		.addr	= client->addr,
		.buf	= (u8 *)&buf,
	};
	unsigned int len = ((addr >> MT9M114_REG_SIZE_SHIFT) & 3) + 1;
	unsigned int i;
	int ret;

	if (*error < 0)
		return;

	buf.reg = cpu_to_be16(addr & MT9M114_REG_ADDR_MASK);
	for (i = 0; i < len; ++i) {
		buf.val[len - i - 1] = value & 0xff;
		value >>= 8;
	}

	msg.len	= len + 2;

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to write register 0x%04x: %d\n",
			addr & MT9M114_REG_ADDR_MASK, ret);
		*error = ret;
	}
}

static int mt9m114_writeregs(struct mt9m114 *sensor,
			     const struct mt9m114_reg *regs, unsigned int len)
{
	unsigned int i;
	int ret = 0;

	for (i = 0; i < len; i++)
		mt9m114_write(sensor, regs[i].reg, regs[i].val, &ret);

	return ret;
}

static int mt9m114_configure(struct mt9m114 *sensor)
{
	u32 value;
	int ret = 0;

	/*
	 * Pixel array crop and binning. The CAM_SENSOR_CFG_CPIPE_LAST_ROW
	 * register isn't clearly documented, but is always set to the number
	 * of output rows minus 4 in all example sensor modes.
	 */
	mt9m114_write(sensor, MT9M114_CAM_SENSOR_CFG_X_ADDR_START,
		      sensor->pa.crop.left, &ret);
	mt9m114_write(sensor, MT9M114_CAM_SENSOR_CFG_Y_ADDR_START,
		      sensor->pa.crop.top, &ret);
	mt9m114_write(sensor, MT9M114_CAM_SENSOR_CFG_X_ADDR_END,
		      sensor->pa.crop.width + sensor->pa.crop.left - 1, &ret);
	mt9m114_write(sensor, MT9M114_CAM_SENSOR_CFG_Y_ADDR_END,
		      sensor->pa.crop.height + sensor->pa.crop.top - 1, &ret);
	mt9m114_write(sensor, MT9M114_CAM_SENSOR_CFG_CPIPE_LAST_ROW,
		      sensor->pa.format.height - 4 - 1, &ret);
	if (ret < 0)
		return ret;

	ret = mt9m114_read(sensor, MT9M114_CAM_SENSOR_CONTROL_READ_MODE,
			   &value);
	if (ret < 0)
		return ret;

	value &= ~(MT9M114_CAM_SENSOR_CONTROL_X_READ_OUT_MASK |
		   MT9M114_CAM_SENSOR_CONTROL_Y_READ_OUT_MASK);

	if (sensor->pa.crop.width != sensor->pa.format.width)
		value |= MT9M114_CAM_SENSOR_CONTROL_X_READ_OUT_SUMMING;
	if (sensor->pa.crop.height != sensor->pa.format.height)
		value |= MT9M114_CAM_SENSOR_CONTROL_Y_READ_OUT_SUMMING;

	mt9m114_write(sensor, MT9M114_CAM_SENSOR_CONTROL_READ_MODE, value,
		      &ret);

	/*
	 * Color pipeline (IFP) cropping and scaling. Subtract 4 from the left
	 * and top coordinates to compensate for the lines and columns removed
	 * by demosaicing that are taken into account in the crop rectangle but
	 * not in the hardware.
	 */
	mt9m114_write(sensor, MT9M114_CAM_CROP_WINDOW_XOFFSET,
		      sensor->ifp.crop.left - 4, &ret);
	mt9m114_write(sensor, MT9M114_CAM_CROP_WINDOW_YOFFSET,
		      sensor->ifp.crop.top - 4, &ret);
	mt9m114_write(sensor, MT9M114_CAM_CROP_WINDOW_WIDTH,
		      sensor->ifp.crop.width, &ret);
	mt9m114_write(sensor, MT9M114_CAM_CROP_WINDOW_HEIGHT,
		      sensor->ifp.crop.height, &ret);

	mt9m114_write(sensor, MT9M114_CAM_OUTPUT_WIDTH,
		      sensor->ifp.compose.width, &ret);
	mt9m114_write(sensor, MT9M114_CAM_OUTPUT_HEIGHT,
		      sensor->ifp.compose.height, &ret);

	/* AWB and AE windows, use the full frame. */
	mt9m114_write(sensor, MT9M114_CAM_STAT_AWB_CLIP_WINDOW_XSTART, 0, &ret);
	mt9m114_write(sensor, MT9M114_CAM_STAT_AWB_CLIP_WINDOW_YSTART, 0, &ret);
	mt9m114_write(sensor, MT9M114_CAM_STAT_AWB_CLIP_WINDOW_XEND,
		      sensor->ifp.compose.width - 1, &ret);
	mt9m114_write(sensor, MT9M114_CAM_STAT_AWB_CLIP_WINDOW_YEND,
		      sensor->ifp.compose.height - 1, &ret);

	mt9m114_write(sensor, MT9M114_CAM_STAT_AE_INITIAL_WINDOW_XSTART,
		      0, &ret);
	mt9m114_write(sensor, MT9M114_CAM_STAT_AE_INITIAL_WINDOW_YSTART,
		      0, &ret);
	mt9m114_write(sensor, MT9M114_CAM_STAT_AE_INITIAL_WINDOW_XEND,
		      sensor->ifp.compose.width / 5 - 1, &ret);
	mt9m114_write(sensor, MT9M114_CAM_STAT_AE_INITIAL_WINDOW_YEND,
		      sensor->ifp.compose.height / 5 - 1, &ret);

	mt9m114_write(sensor, MT9M114_CAM_CROP_CROPMODE,
		      MT9M114_CAM_CROP_MODE_AWB_AUTO_CROP_EN |
		      MT9M114_CAM_CROP_MODE_AE_AUTO_CROP_EN, &ret);

	if (ret < 0)
		return ret;

	/* Set the media bus code. */
	ret = mt9m114_read(sensor, MT9M114_CAM_OUTPUT_FORMAT, &value);
	if (ret < 0)
		return ret;

	value &= ~(MT9M114_CAM_OUTPUT_FORMAT_RGB_FORMAT_MASK |
		   MT9M114_CAM_OUTPUT_FORMAT_BAYER_FORMAT_MASK |
		   MT9M114_CAM_OUTPUT_FORMAT_FORMAT_MASK |
		   MT9M114_CAM_OUTPUT_FORMAT_SWAP_BYTES |
		   MT9M114_CAM_OUTPUT_FORMAT_SWAP_RED_BLUE);
	value |= sensor->ifp.info->output_format;

	mt9m114_write(sensor, MT9M114_CAM_OUTPUT_FORMAT, value, &ret);
	return ret;
}

/* Wait for a command to complete. */
static int mt9m114_poll_command(struct mt9m114 *sensor, u32 command)
{
	unsigned int i;
	u32 value;
	int ret;

	for (i = 0; i < 100; ++i) {
		ret = mt9m114_read(sensor, MT9M114_COMMAND_REGISTER, &value);
		if (ret < 0)
			return ret;

		if (!(value & command))
			break;

		usleep_range(5000, 6000);
	}

	if (value & command) {
		dev_err(&sensor->client->dev, "Command %u completion timeout\n",
			command);
		return -ETIMEDOUT;
	}

	if (!(value & MT9M114_COMMAND_REGISTER_OK)) {
		dev_err(&sensor->client->dev, "Command %u failed\n", command);
		return -EIO;
	}

	return 0;
}

/* Wait for a state to be entered. */
static int mt9m114_poll_state(struct mt9m114 *sensor, u32 state)
{
	unsigned int i;
	u32 value;
	int ret;

	for (i = 0; i < 100; ++i) {
		ret = mt9m114_read(sensor, MT9M114_SYSMGR_CURRENT_STATE,
				   &value);
		if (ret < 0)
			return ret;

		if (value == state)
			return 0;

		usleep_range(1000, 1500);
	}

	dev_err(&sensor->client->dev, "Timeout waiting for state 0x%02x\n",
		state);
	return -ETIMEDOUT;
}

static int mt9m114_set_state(struct mt9m114 *sensor, u8 next_state)
{
	int ret = 0;

	/* Set the next desired state and start the state transition. */
	mt9m114_write(sensor, MT9M114_SYSMGR_NEXT_STATE, next_state, &ret);
	if (ret < 0)
		return ret;

	mt9m114_write(sensor, MT9M114_COMMAND_REGISTER,
		      MT9M114_COMMAND_REGISTER_OK |
		      MT9M114_COMMAND_REGISTER_SET_STATE, &ret);
	if (ret < 0)
		return ret;

	/* Wait for the state transition to complete. */
	ret = mt9m114_poll_command(sensor, MT9M114_COMMAND_REGISTER_SET_STATE);
	if (ret < 0)
		return ret;

	return 0;
}

static int mt9m114_power_on(struct mt9m114 *sensor)
{
	int ret;

	/* Enable power and clocks. */
	ret = regulator_bulk_enable(ARRAY_SIZE(sensor->supplies),
				    sensor->supplies);
	if (ret < 0)
		return ret;

	ret = clk_prepare_enable(sensor->clk);
	if (ret < 0)
		goto error_regulator;

	/* Perform a hard reset if available, or a soft reset otherwise. */
	if (sensor->reset) {
		long freq = clk_get_rate(sensor->clk);
		unsigned int duration;

		/*
		 * The minimum duration is 50 clock cycles, thus typically
		 * around 2µs. Double it to be safe.
		 */
		duration = DIV_ROUND_UP(2 * 50 * 1000000, freq);

		gpiod_set_value(sensor->reset, 1);
		udelay(duration);
		gpiod_set_value(sensor->reset, 0);
	} else {
		/*
		 * The power may have just been turned on, we need to wait for
		 * the sensor to be ready to accept I2C commands.
		 */
		usleep_range(44500, 50000);

		mt9m114_write(sensor, MT9M114_RESET_AND_MISC_CONTROL,
			      MT9M114_RESET_SOC, &ret);
		if (ret < 0) {
			dev_err(&sensor->client->dev, "Soft reset failed\n");
			goto error_clock;
		}

		mt9m114_write(sensor, MT9M114_RESET_AND_MISC_CONTROL, 0, &ret);
		if (ret < 0)
			goto error_clock;
	}

	/*
	 * Wait for the sensor to be ready to accept I2C commands by polling the
	 * command register to wait for initialization to complete.
	 */
	usleep_range(44500, 50000);

	ret = mt9m114_poll_command(sensor, MT9M114_COMMAND_REGISTER_SET_STATE);
	if (ret < 0)
		goto error_clock;

	if (sensor->bus_cfg.bus_type == V4L2_MBUS_PARALLEL) {
		/*
		 * In parallel mode (OE set to low), the sensor will enter the
		 * streaming state after initialization. Enter the standby
		 * manually to stop streaming.
		 */
		ret = mt9m114_set_state(sensor,
					MT9M114_SYS_STATE_ENTER_STANDBY);
		if (ret < 0)
			goto error_clock;
	}

	/*
	 * Before issuing any Set-State command, we must ensure that the sensor
	 * reaches the standby mode (either initiated manually above in
	 * parallel mode, or automatically after reset in MIPI mode).
	 */
	ret = mt9m114_poll_state(sensor, MT9M114_SYS_STATE_STANDBY);
	if (ret < 0)
		goto error_clock;

	return 0;

error_clock:
	clk_disable_unprepare(sensor->clk);
error_regulator:
	regulator_bulk_disable(ARRAY_SIZE(sensor->supplies), sensor->supplies);
	return ret;
}

static void mt9m114_power_off(struct mt9m114 *sensor)
{
	clk_disable_unprepare(sensor->clk);
	regulator_bulk_disable(ARRAY_SIZE(sensor->supplies), sensor->supplies);
}

static int mt9m114_initialize(struct mt9m114 *sensor)
{
	unsigned int pll_m = 32;
	unsigned int pll_n = 1;
	unsigned int pll_p = 7;
	u32 value;
	int ret;

	ret = mt9m114_writeregs(sensor, mt9m114_init, ARRAY_SIZE(mt9m114_init));
	if (ret < 0) {
		dev_err(&sensor->client->dev,
			"Failed to initialize the sensor\n");
		return ret;
	}

	/* Configure the PLL with hardcoded multiplier and dividers. */
	sensor->pixrate = clk_get_rate(sensor->clk) * pll_m
			/ ((pll_n + 1) * (pll_p + 1));
	mt9m114_write(sensor, MT9M114_CAM_SYSCTL_PLL_ENABLE,
		      MT9M114_CAM_SYSCTL_PLL_ENABLE_VALUE, &ret);
	mt9m114_write(sensor, MT9M114_CAM_SYSCTL_PLL_DIVIDER_M_N,
		      MT9M114_CAM_SYSCTL_PLL_DIVIDER_VALUE(pll_m, pll_n), &ret);
	mt9m114_write(sensor, MT9M114_CAM_SYSCTL_PLL_DIVIDER_P,
		      MT9M114_CAM_SYSCTL_PLL_DIVIDER_P_VALUE(pll_p), &ret);
	mt9m114_write(sensor, MT9M114_CAM_SENSOR_CFG_PIXCLK, sensor->pixrate,
		      &ret);
	if (ret < 0)
		return ret;

	/* Configure the output mode. */
	if (sensor->bus_cfg.bus_type == V4L2_MBUS_CSI2_DPHY) {
		value = MT9M114_CAM_PORT_PORT_SELECT_MIPI
		      | MT9M114_CAM_PORT_CHAN_NUM(0);
		if (sensor->bus_cfg.bus.mipi_csi2.flags &
		    V4L2_MBUS_CSI2_CONTINUOUS_CLOCK)
			value |= MT9M114_CAM_PORT_CONT_MIPI_CLK;
	} else {
		value = MT9M114_CAM_PORT_PORT_SELECT_PARALLEL
		      | MT9M114_CAM_PORT_CONT_MIPI_CLK
		      | 0x8000;
	}
	mt9m114_write(sensor, MT9M114_CAM_PORT_OUTPUT_CONTROL, value, &ret);
	if (ret < 0)
		return ret;

	ret = mt9m114_set_state(sensor, MT9M114_SYS_STATE_ENTER_CONFIG_CHANGE);
	if (ret < 0)
		return ret;

	ret = mt9m114_set_state(sensor, MT9M114_SYS_STATE_ENTER_SUSPEND);
	if (ret < 0)
		return ret;

	return 0;
}

/* -----------------------------------------------------------------------------
 * Common Subdev Operations
 */

static const struct media_entity_operations mt9m114_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

/* -----------------------------------------------------------------------------
 * Pixel Array Control Operations
 */

static inline struct mt9m114 *pa_ctrl_to_mt9m114(struct v4l2_ctrl *ctrl)
{
	return container_of(ctrl->handler, struct mt9m114, pa.hdl);
}

static int mt9m114_pa_g_ctrl(struct v4l2_ctrl *ctrl)
{
	struct mt9m114 *sensor = pa_ctrl_to_mt9m114(ctrl);
	u32 value;
	int ret = 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		ret = mt9m114_read(sensor,
				   MT9M114_CAM_SENSOR_CONTROL_COARSE_INTEGRATION_TIME,
				   &value);
		if (ret < 0)
			break;

		ctrl->val = value;
		break;

	case V4L2_CID_ANALOGUE_GAIN:
		ret = mt9m114_read(sensor,
				   MT9M114_CAM_SENSOR_CONTROL_ANALOG_GAIN,
				   &value);
		if (ret < 0)
			break;

		ctrl->val = value;
		break;

	default:
		return -EINVAL;
	}

	return ret;
}

static int mt9m114_pa_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct mt9m114 *sensor = pa_ctrl_to_mt9m114(ctrl);
	int ret = 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		mt9m114_write(sensor,
			      MT9M114_CAM_SENSOR_CONTROL_COARSE_INTEGRATION_TIME,
			      ctrl->val, &ret);
		break;

	case V4L2_CID_ANALOGUE_GAIN:
		/*
		 * The CAM_SENSOR_CONTROL_ANALOG_GAIN contains linear analog
		 * gain values that are mapped to the GLOBAL_GAIN register
		 * values by the sensor firmware.
		 */
		mt9m114_write(sensor, MT9M114_CAM_SENSOR_CONTROL_ANALOG_GAIN,
			      ctrl->val, &ret);
		break;

	default:
		return -EINVAL;
	}

	return ret;
}

static const struct v4l2_ctrl_ops mt9m114_pa_ctrl_ops = {
	.g_volatile_ctrl = mt9m114_pa_g_ctrl,
	.s_ctrl = mt9m114_pa_s_ctrl,
};

static void mt9m114_pa_update_controls(struct mt9m114 *sensor, bool manual)
{
	mutex_lock(sensor->pa.hdl.lock);

	/*
	 * Update the volatile flag on the manual exposure and gain controls.
	 * If the controls have switched to manual, read their current value
	 * from the hardware to ensure that control read and write operations
	 * will behave correctly
	 */
	if (manual) {
		mt9m114_pa_g_ctrl(sensor->pa.exposure);
		sensor->pa.exposure->cur.val = sensor->pa.exposure->val;
		sensor->pa.exposure->flags &= ~V4L2_CTRL_FLAG_VOLATILE;

		mt9m114_pa_g_ctrl(sensor->pa.gain);
		sensor->pa.gain->cur.val = sensor->pa.gain->val;
		sensor->pa.gain->flags &= ~V4L2_CTRL_FLAG_VOLATILE;
	} else {
		sensor->pa.exposure->flags |= V4L2_CTRL_FLAG_VOLATILE;
		sensor->pa.gain->flags |= V4L2_CTRL_FLAG_VOLATILE;
	}

	mutex_unlock(sensor->pa.hdl.lock);
}

/* -----------------------------------------------------------------------------
 * Pixel Array Subdev Operations
 */

static inline struct mt9m114 *pa_to_mt9m114(struct v4l2_subdev *sd)
{
	return container_of(sd, struct mt9m114, pa.sd);
}

static struct v4l2_mbus_framefmt *
__mt9m114_pa_get_pad_format(struct mt9m114 *sensor,
			    struct v4l2_subdev_state *state,
			    unsigned int pad, u32 which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_format(&sensor->pa.sd, state, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &sensor->pa.format;
	default:
		return NULL;
	}
}

static struct v4l2_rect *
__mt9m114_pa_get_pad_crop(struct mt9m114 *sensor,
			  struct v4l2_subdev_state *state,
			  unsigned int pad, u32 which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_crop(&sensor->pa.sd, state, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &sensor->pa.crop;
	default:
		return NULL;
	}
}

static int mt9m114_pa_init_cfg(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *state)
{
	u32 which = state ? V4L2_SUBDEV_FORMAT_TRY : V4L2_SUBDEV_FORMAT_ACTIVE;
	struct mt9m114 *sensor = pa_to_mt9m114(sd);
	struct v4l2_mbus_framefmt *format;
	struct v4l2_rect *crop;

	crop = __mt9m114_pa_get_pad_crop(sensor, state, 0, which);

	crop->left = 0;
	crop->top = 0;
	crop->width = MT9M114_PIXEL_ARRAY_WIDTH;
	crop->height = MT9M114_PIXEL_ARRAY_HEIGHT;

	format = __mt9m114_pa_get_pad_format(sensor, state, 0, which);
	memset(format, 0, sizeof(*format));

	format->width = MT9M114_PIXEL_ARRAY_WIDTH;
	format->height = MT9M114_PIXEL_ARRAY_HEIGHT;
	format->code = MEDIA_BUS_FMT_SGRBG10_1X10;
	format->field = V4L2_FIELD_NONE;
	format->colorspace = V4L2_COLORSPACE_SRGB;
	format->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	format->quantization = V4L2_QUANTIZATION_DEFAULT;
	format->xfer_func = V4L2_XFER_FUNC_DEFAULT;

	return 0;
}

static int mt9m114_pa_enum_mbus_code(struct v4l2_subdev *sd,
				     struct v4l2_subdev_state *state,
				     struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index > 0)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_SGRBG10_1X10;

	return 0;
}

static int mt9m114_pa_enum_framesizes(struct v4l2_subdev *sd,
				      struct v4l2_subdev_state *state,
				      struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index > 1)
		return -EINVAL;

	if (fse->code != MEDIA_BUS_FMT_SGRBG10_1X10)
		return -EINVAL;

	/* Report binning capability through frame size enumeration. */
	fse->min_width = MT9M114_PIXEL_ARRAY_WIDTH / (fse->index + 1);
	fse->max_width = MT9M114_PIXEL_ARRAY_WIDTH / (fse->index + 1);
	fse->min_height = MT9M114_PIXEL_ARRAY_HEIGHT / (fse->index + 1);
	fse->max_height = MT9M114_PIXEL_ARRAY_HEIGHT / (fse->index + 1);

	return 0;
}

static int mt9m114_pa_get_fmt(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *state,
			      struct v4l2_subdev_format *fmt)
{
	struct mt9m114 *sensor = pa_to_mt9m114(sd);
	const struct v4l2_mbus_framefmt *format;

	format = __mt9m114_pa_get_pad_format(sensor, state, fmt->pad,
					     fmt->which);

	mutex_lock(&sensor->lock);
	fmt->format = *format;
	mutex_unlock(&sensor->lock);

	return 0;
}

static int mt9m114_pa_set_fmt(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *state,
			      struct v4l2_subdev_format *fmt)
{
	struct mt9m114 *sensor = pa_to_mt9m114(sd);
	struct v4l2_mbus_framefmt *format;
	struct v4l2_rect *crop;
	unsigned int hscale;
	unsigned int vscale;

	crop = __mt9m114_pa_get_pad_crop(sensor, state, fmt->pad, fmt->which);
	format = __mt9m114_pa_get_pad_format(sensor, state, fmt->pad,
					     fmt->which);

	mutex_lock(&sensor->lock);

	/* The sensor can bin horizontally and vertically. */
	hscale = DIV_ROUND_CLOSEST(crop->width, fmt->format.width ? : 1);
	vscale = DIV_ROUND_CLOSEST(crop->height, fmt->format.height ? : 1);
	format->width = crop->width / clamp(hscale, 1U, 2U);
	format->height = crop->height / clamp(vscale, 1U, 2U);

	fmt->format = *format;

	mutex_unlock(&sensor->lock);

	return 0;
}

static int mt9m114_pa_get_selection(struct v4l2_subdev *sd,
				    struct v4l2_subdev_state *state,
				    struct v4l2_subdev_selection *sel)
{
	struct mt9m114 *sensor = pa_to_mt9m114(sd);
	const struct v4l2_rect *crop;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		crop = __mt9m114_pa_get_pad_crop(sensor, state, sel->pad,
						 sel->which);
		mutex_lock(&sensor->lock);
		sel->r = *crop;
		mutex_unlock(&sensor->lock);
		return 0;

	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
	case V4L2_SEL_TGT_NATIVE_SIZE:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = MT9M114_PIXEL_ARRAY_WIDTH;
		sel->r.height = MT9M114_PIXEL_ARRAY_HEIGHT;
		return 0;

	default:
		return -EINVAL;
	}
}

static int mt9m114_pa_set_selection(struct v4l2_subdev *sd,
				    struct v4l2_subdev_state *state,
				    struct v4l2_subdev_selection *sel)
{
	struct mt9m114 *sensor = pa_to_mt9m114(sd);
	struct v4l2_mbus_framefmt *format;
	struct v4l2_rect *crop;

	if (sel->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	crop = __mt9m114_pa_get_pad_crop(sensor, state, sel->pad, sel->which);
	format = __mt9m114_pa_get_pad_format(sensor, state, sel->pad,
					     sel->which);

	mutex_lock(&sensor->lock);

	/*
	 * Clamp the crop rectangle. The vertical coordinates must be even, and
	 * the horizontal coordinates must be a multiple of 4.
	 *
	 * FIXME: The horizontal coordinates must be a multiple of 8 when
	 * binning, but binning is configured after setting the selection, so
	 * we can't know tell here if it will be used.
	 */
	crop->left = ALIGN(sel->r.left, 4);
	crop->top = ALIGN(sel->r.top, 2);
	crop->width = clamp_t(unsigned int, ALIGN(sel->r.width, 4),
			      MT9M114_PIXEL_ARRAY_MIN_OUTPUT_WIDTH,
			      MT9M114_PIXEL_ARRAY_WIDTH - crop->left);
	crop->height = clamp_t(unsigned int, ALIGN(sel->r.height, 2),
			       MT9M114_PIXEL_ARRAY_MIN_OUTPUT_HEIGHT,
			       MT9M114_PIXEL_ARRAY_HEIGHT - crop->top);

	sel->r = *crop;

	/* Reset the format. */
	format->width = crop->width;
	format->height = crop->height;

	mutex_unlock(&sensor->lock);

	return 0;
}

static const struct v4l2_subdev_pad_ops mt9m114_pa_pad_ops = {
	.init_cfg = mt9m114_pa_init_cfg,
	.enum_mbus_code = mt9m114_pa_enum_mbus_code,
	.enum_frame_size = mt9m114_pa_enum_framesizes,
	.get_fmt = mt9m114_pa_get_fmt,
	.set_fmt = mt9m114_pa_set_fmt,
	.get_selection = mt9m114_pa_get_selection,
	.set_selection = mt9m114_pa_set_selection,
};

static const struct v4l2_subdev_ops mt9m114_pa_ops = {
	.pad = &mt9m114_pa_pad_ops,
};

static int mt9m114_pa_init(struct mt9m114 *sensor)
{
	struct v4l2_ctrl_handler *hdl = &sensor->pa.hdl;
	struct v4l2_subdev *sd = &sensor->pa.sd;
	struct media_pad *pads = &sensor->pa.pad;
	int ret;

	/* Initialize the subdev. */
	v4l2_subdev_init(sd, &mt9m114_pa_ops);
	v4l2_i2c_subdev_set_name(sd, sensor->client, "mt9m114", " pixel array");

	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	sd->owner = THIS_MODULE;
	sd->dev = &sensor->client->dev;
	v4l2_set_subdevdata(sd, sensor->client);

	/* Initialize the media entity. */
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	sd->entity.ops = &mt9m114_entity_ops;
	pads[0].flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&sd->entity, 1, pads);
	if (ret < 0)
		return ret;

	/* Initialize the control handler. */
	v4l2_ctrl_handler_init(hdl, 3);

	/*
	 * The maximum corse integratime is MT9M114_FRAME_LENGTH - 2 lines. The
	 * default is taken directly from the datasheet, but makes little sense
	 * as auto-exposure is enabled by default.
	 */
	sensor->pa.exposure =
		v4l2_ctrl_new_std(hdl, &mt9m114_pa_ctrl_ops,
				  V4L2_CID_EXPOSURE,
				  1, MT9M114_FRAME_LENGTH - 2, 1, 16);
	if (!sensor->pa.exposure)
		return hdl->error;
	sensor->pa.exposure->flags |= V4L2_CTRL_FLAG_VOLATILE;

	sensor->pa.gain =
		v4l2_ctrl_new_std(hdl, &mt9m114_pa_ctrl_ops,
				  V4L2_CID_ANALOGUE_GAIN,
				  1, 511, 1, 32);
	if (!sensor->pa.gain)
		return hdl->error;
	sensor->pa.gain->flags |= V4L2_CTRL_FLAG_VOLATILE;

	v4l2_ctrl_new_std(hdl, &mt9m114_pa_ctrl_ops,
			  V4L2_CID_PIXEL_RATE,
			  sensor->pixrate, sensor->pixrate, 1,
			  sensor->pixrate);

	ret = v4l2_ctrl_handler_setup(hdl);
	if (ret < 0)
		return ret;

	sd->ctrl_handler = hdl;

	/* Initialize the pads formats and selection rectangles. */
	mt9m114_pa_init_cfg(sd, NULL);

	return 0;
}

static void mt9m114_pa_cleanup(struct mt9m114 *sensor)
{
	media_entity_cleanup(&sensor->pa.sd.entity);
}

/* -----------------------------------------------------------------------------
 * Image Flow Processor Control Operations
 */

static const char * const mt9m114_test_pattern_menu[] = {
	"Disabled",
	"Solid Color",
	"100% Color Bars",
	"Pseudo-Random",
	"Fade-to-Gray Color Bars",
	"Walking Ones 10-bit",
	"Walking Ones 8-bit",
};

/* Keep in sync with mt9m114_test_pattern_menu */
static const unsigned int mt9m114_test_pattern_value[] = {
	MT9M114_CAM_MODE_TEST_PATTERN_SELECT_SOLID,
	MT9M114_CAM_MODE_TEST_PATTERN_SELECT_SOLID_BARS,
	MT9M114_CAM_MODE_TEST_PATTERN_SELECT_RANDOM,
	MT9M114_CAM_MODE_TEST_PATTERN_SELECT_FADING_BARS,
	MT9M114_CAM_MODE_TEST_PATTERN_SELECT_WALKING_1S_10B,
	MT9M114_CAM_MODE_TEST_PATTERN_SELECT_WALKING_1S_8B,
};

static inline struct mt9m114 *ifp_ctrl_to_mt9m114(struct v4l2_ctrl *ctrl)
{
	return container_of(ctrl->handler, struct mt9m114, ifp.hdl);
}

static int mt9m114_ifp_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct mt9m114 *sensor = ifp_ctrl_to_mt9m114(ctrl);
	u32 value;
	int ret = 0;

	switch (ctrl->id) {
	case V4L2_CID_AUTO_WHITE_BALANCE:
		/* Control both the AWB mode and the CCM algorithm. */
		if (ctrl->val)
			value = MT9M114_CAM_AWB_MODE_AUTO
			      | MT9M114_CAM_AWB_MODE_EXCLUSIVE_AE;
		else
			value = 0;

		mt9m114_write(sensor, MT9M114_CAM_AWB_AWBMODE, value, &ret);

		if (ctrl->val)
			value = MT9M114_CCM_EXEC_CALC_CCM_MATRIX
			      | 0x22;
		else
			value = 0;

		mt9m114_write(sensor, MT9M114_CCM_ALGO, value, &ret);
		break;

	case V4L2_CID_HFLIP:
		mt9m114_read(sensor, MT9M114_CAM_SENSOR_CONTROL_READ_MODE,
			     &value);
		if (ctrl->val)
			value |= MT9M114_CAM_SENSOR_CONTROL_HORZ_MIRROR_EN;
		else
			value &= ~MT9M114_CAM_SENSOR_CONTROL_HORZ_MIRROR_EN;
		mt9m114_write(sensor, MT9M114_CAM_SENSOR_CONTROL_READ_MODE,
			      value, &ret);
		break;

	case V4L2_CID_VFLIP:
		mt9m114_read(sensor, MT9M114_CAM_SENSOR_CONTROL_READ_MODE,
			     &value);
		if (ctrl->val)
			value |= MT9M114_CAM_SENSOR_CONTROL_VERT_FLIP_EN;
		else
			value &= ~MT9M114_CAM_SENSOR_CONTROL_VERT_FLIP_EN;
		mt9m114_write(sensor, MT9M114_CAM_SENSOR_CONTROL_READ_MODE,
			      value, &ret);
		break;

	case V4L2_CID_EXPOSURE_AUTO:
		if (ctrl->val == V4L2_EXPOSURE_AUTO)
			value = MT9M114_AE_TRACK_EXEC_AUTOMATIC_EXPOSURE
			      | 0x00fe;
		else
			value = 0;

		mt9m114_write(sensor, MT9M114_AE_TRACK_ALGO, value, &ret);
		if (ret)
			break;

		mt9m114_pa_update_controls(sensor,
					   ctrl->val != V4L2_EXPOSURE_AUTO);
		break;

	case V4L2_CID_TEST_PATTERN:
	case V4L2_CID_TEST_PATTERN_RED:
	case V4L2_CID_TEST_PATTERN_GREENR:
	case V4L2_CID_TEST_PATTERN_BLUE: {
		unsigned int pattern = sensor->ifp.tpg[MT9M114_TPG_PATTERN]->val;

		if (pattern) {
			mt9m114_write(sensor, MT9M114_CAM_MODE_SELECT,
				      MT9M114_CAM_MODE_SELECT_TEST_PATTERN,
				      &ret);
			mt9m114_write(sensor,
				      MT9M114_CAM_MODE_TEST_PATTERN_SELECT,
				      mt9m114_test_pattern_value[pattern - 1],
				      &ret);
			mt9m114_write(sensor,
				      MT9M114_CAM_MODE_TEST_PATTERN_RED,
				      sensor->ifp.tpg[MT9M114_TPG_RED]->val,
				      &ret);
			mt9m114_write(sensor,
				      MT9M114_CAM_MODE_TEST_PATTERN_GREEN,
				      sensor->ifp.tpg[MT9M114_TPG_GREEN]->val,
				      &ret);
			mt9m114_write(sensor,
				      MT9M114_CAM_MODE_TEST_PATTERN_BLUE,
				      sensor->ifp.tpg[MT9M114_TPG_BLUE]->val,
				      &ret);
		} else {
			mt9m114_write(sensor, MT9M114_CAM_MODE_SELECT,
				      MT9M114_CAM_MODE_SELECT_NORMAL, &ret);
		}

		/*
		 * A Config-Change needs to be issued for the change to take
		 * effect. If we're not streaming ignore this, the change will
		 * be applied when the stream is started.
		 */
		if (ret || !sensor->streaming)
			break;

		ret = mt9m114_set_state(sensor,
					MT9M114_SYS_STATE_ENTER_CONFIG_CHANGE);
		break;
	}

	default:
		return -EINVAL;
	}

	return ret;
}

static const struct v4l2_ctrl_ops mt9m114_ifp_ctrl_ops = {
	.s_ctrl = mt9m114_ifp_s_ctrl,
};

/* -----------------------------------------------------------------------------
 * Image Flow Processor Subdev Operations
 */

static inline struct mt9m114 *ifp_to_mt9m114(struct v4l2_subdev *sd)
{
	return container_of(sd, struct mt9m114, ifp.sd);
}

static int mt9m114_ifp_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct mt9m114 *sensor = ifp_to_mt9m114(sd);
	int ret;

	mutex_lock(sensor->ifp.hdl.lock);

	if (!enable) {
		ret = mt9m114_set_state(sensor,
					MT9M114_SYS_STATE_ENTER_SUSPEND);
		sensor->streaming = false;
		goto done;
	}

	mutex_lock(&sensor->lock);
	ret = mt9m114_configure(sensor);
	mutex_unlock(&sensor->lock);
	if (ret < 0)
		return ret;

	/*
	 * The Change-Config state is transient and moves to the streaming
	 * state automatically.
	 */
	ret = mt9m114_set_state(sensor, MT9M114_SYS_STATE_ENTER_CONFIG_CHANGE);
	if (ret < 0)
		goto done;

	sensor->streaming = true;

done:
	mutex_unlock(sensor->ifp.hdl.lock);
	return ret;
}

static int mt9m114_ifp_g_frame_interval(struct v4l2_subdev *sd,
					struct v4l2_subdev_frame_interval *interval)
{
	struct v4l2_fract *ival = &interval->interval;
	struct mt9m114 *sensor = ifp_to_mt9m114(sd);

	ival->numerator = 1;
	ival->denominator = sensor->ifp.frame_rate;

	return 0;
}

static int mt9m114_ifp_s_frame_interval(struct v4l2_subdev *sd,
					struct v4l2_subdev_frame_interval *interval)
{
	struct v4l2_fract *ival = &interval->interval;
	struct mt9m114 *sensor = ifp_to_mt9m114(sd);
	u16 frame_rate;
	int ret = 0;

	if (ival->numerator != 0 && ival->denominator != 0)
		sensor->ifp.frame_rate = min_t(unsigned int,
					       ival->denominator / ival->numerator,
					       MT9M114_MAX_FRAME_RATE);
	else
		sensor->ifp.frame_rate = MT9M114_MAX_FRAME_RATE;

	ival->numerator = 1;
	ival->denominator = sensor->ifp.frame_rate;

	frame_rate = sensor->ifp.frame_rate << 8;
	mt9m114_write(sensor, MT9M114_CAM_AET_MIN_FRAME_RATE, frame_rate, &ret);
	mt9m114_write(sensor, MT9M114_CAM_AET_MAX_FRAME_RATE, frame_rate, &ret);

	return ret;
}

static struct v4l2_mbus_framefmt *
__mt9m114_ifp_get_pad_format(struct mt9m114 *sensor,
			     struct v4l2_subdev_state *state,
			     unsigned int pad, u32 which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_format(&sensor->ifp.sd, state, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &sensor->ifp.formats[pad];
	default:
		return NULL;
	}
}

static struct v4l2_rect *
__mt9m114_ifp_get_pad_crop(struct mt9m114 *sensor,
			   struct v4l2_subdev_state *state,
			   unsigned int pad, u32 which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_crop(&sensor->ifp.sd, state, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &sensor->ifp.crop;
	default:
		return NULL;
	}
}

static struct v4l2_rect *
__mt9m114_ifp_get_pad_compose(struct mt9m114 *sensor,
			      struct v4l2_subdev_state *state,
			      unsigned int pad, u32 which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_compose(&sensor->ifp.sd, state, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &sensor->ifp.compose;
	default:
		return NULL;
	}
}

static int mt9m114_ifp_init_cfg(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state)
{
	u32 which = state ? V4L2_SUBDEV_FORMAT_TRY : V4L2_SUBDEV_FORMAT_ACTIVE;
	struct mt9m114 *sensor = ifp_to_mt9m114(sd);
	struct v4l2_mbus_framefmt *format;
	struct v4l2_rect *crop;
	struct v4l2_rect *compose;

	format = __mt9m114_ifp_get_pad_format(sensor, state, 0, which);
	memset(format, 0, sizeof(*format));

	format->width = MT9M114_PIXEL_ARRAY_WIDTH;
	format->height = MT9M114_PIXEL_ARRAY_HEIGHT;
	format->code = MEDIA_BUS_FMT_SGRBG10_1X10;
	format->field = V4L2_FIELD_NONE;
	format->colorspace = V4L2_COLORSPACE_SRGB;
	format->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	format->quantization = V4L2_QUANTIZATION_DEFAULT;
	format->xfer_func = V4L2_XFER_FUNC_DEFAULT;

	crop = __mt9m114_ifp_get_pad_crop(sensor, state, 0, which);

	crop->left = 4;
	crop->top = 4;
	crop->width = format->width - 8;
	crop->height = format->height - 8;

	compose = __mt9m114_ifp_get_pad_compose(sensor, state, 0, which);

	compose->left = 0;
	compose->top = 0;
	compose->width = crop->width;
	compose->height = crop->height;

	format = __mt9m114_ifp_get_pad_format(sensor, state, 1, which);
	memset(format, 0, sizeof(*format));

	format->width = compose->width;
	format->height = compose->height;
	format->code = mt9m114_default_format_info(sensor)->code;
	format->field = V4L2_FIELD_NONE;
	format->colorspace = V4L2_COLORSPACE_SRGB;
	format->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	format->quantization = V4L2_QUANTIZATION_DEFAULT;
	format->xfer_func = V4L2_XFER_FUNC_DEFAULT;

	return 0;
}

static int mt9m114_ifp_enum_mbus_code(struct v4l2_subdev *sd,
				      struct v4l2_subdev_state *state,
				      struct v4l2_subdev_mbus_code_enum *code)
{
	const unsigned int num_formats = ARRAY_SIZE(mt9m114_format_infos);
	struct mt9m114 *sensor = ifp_to_mt9m114(sd);
	unsigned int index = 0;
	unsigned int flag;
	unsigned int i;

	switch (code->pad) {
	case 0:
		if (code->index != 0)
			return -EINVAL;

		code->code = mt9m114_format_infos[num_formats - 1].code;
		return 0;

	case 1:
		if (sensor->bus_cfg.bus_type == V4L2_MBUS_CSI2_DPHY)
			flag = MT9M114_FMT_FLAG_CSI2;
		else
			flag = MT9M114_FMT_FLAG_PARALLEL;

		for (i = 0; i < num_formats; ++i) {
			const struct mt9m114_format_info *info =
				&mt9m114_format_infos[i];

			if (info->flags & flag) {
				if (index == code->index) {
					code->code = info->code;
					return 0;
				}

				index++;
			}
		}

		return -EINVAL;

	default:
		return -EINVAL;
	}
}

static int mt9m114_ifp_enum_framesizes(struct v4l2_subdev *sd,
				       struct v4l2_subdev_state *state,
				       struct v4l2_subdev_frame_size_enum *fse)
{
	struct mt9m114 *sensor = ifp_to_mt9m114(sd);
	const struct mt9m114_format_info *info;

	if (fse->index > 0)
		return -EINVAL;

	info = mt9m114_format_info(sensor, fse->pad, fse->code);
	if (!info || info->code != fse->code)
		return -EINVAL;

	if (fse->pad == 0) {
		fse->min_width = MT9M114_PIXEL_ARRAY_MIN_OUTPUT_WIDTH;
		fse->max_width = MT9M114_PIXEL_ARRAY_WIDTH;
		fse->min_height = MT9M114_PIXEL_ARRAY_MIN_OUTPUT_HEIGHT;
		fse->max_height = MT9M114_PIXEL_ARRAY_HEIGHT;
	} else {
		const struct v4l2_rect *crop;

		crop = __mt9m114_ifp_get_pad_crop(sensor, state, 0,
						  V4L2_SUBDEV_FORMAT_TRY);

		mutex_lock(&sensor->lock);
		fse->max_width = crop->width;
		fse->max_height = crop->height;
		mutex_unlock(&sensor->lock);

		fse->min_width = fse->max_width / 4;
		fse->min_height = fse->max_height / 4;
	}

	return 0;
}

static int mt9m114_ifp_enum_frameintervals(struct v4l2_subdev *sd,
					   struct v4l2_subdev_state *state,
					   struct v4l2_subdev_frame_interval_enum *fie)
{
	struct mt9m114 *sensor = ifp_to_mt9m114(sd);
	const struct mt9m114_format_info *info;

	if (fie->index > 0)
		return -EINVAL;

	info = mt9m114_format_info(sensor, fie->pad, fie->code);
	if (!info || info->code != fie->code)
		return -EINVAL;

	fie->interval.numerator = 1;
	fie->interval.denominator = MT9M114_MAX_FRAME_RATE;

	return 0;
}

static int mt9m114_ifp_get_fmt(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *state,
			       struct v4l2_subdev_format *fmt)
{
	struct mt9m114 *sensor = ifp_to_mt9m114(sd);
	struct v4l2_mbus_framefmt *format;

	format = __mt9m114_ifp_get_pad_format(sensor, state, fmt->pad,
					      fmt->which);

	mutex_lock(&sensor->lock);
	fmt->format = *format;
	mutex_unlock(&sensor->lock);

	return 0;
}

static int mt9m114_ifp_set_fmt(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *state,
			       struct v4l2_subdev_format *fmt)
{
	struct mt9m114 *sensor = ifp_to_mt9m114(sd);
	struct v4l2_mbus_framefmt *format;

	format = __mt9m114_ifp_get_pad_format(sensor, state, fmt->pad,
					      fmt->which);

	mutex_lock(&sensor->lock);

	if (fmt->pad == 0) {
		/* Only the size can be changed on the sink pad. */
		format->width = clamp(ALIGN(fmt->format.width, 8),
				      MT9M114_PIXEL_ARRAY_MIN_OUTPUT_WIDTH,
				      MT9M114_PIXEL_ARRAY_WIDTH);
		format->height = clamp(ALIGN(fmt->format.height, 8),
				       MT9M114_PIXEL_ARRAY_MIN_OUTPUT_HEIGHT,
				       MT9M114_PIXEL_ARRAY_HEIGHT);
	} else {
		const struct mt9m114_format_info *info;

		/* Only the media bus code can be changed on the source pad. */
		info = mt9m114_format_info(sensor, 1, fmt->format.code);

		format->code = info->code;
		if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE)
			sensor->ifp.info = info;

		/* If the output format is RAW10, bypass the scaler. */
		if (format->code == MEDIA_BUS_FMT_SGRBG10_1X10)
			*format = *__mt9m114_ifp_get_pad_format(sensor, state,
								0, fmt->which);
	}

	fmt->format = *format;

	mutex_unlock(&sensor->lock);

	return 0;
}

static int mt9m114_ifp_get_selection(struct v4l2_subdev *sd,
				     struct v4l2_subdev_state *state,
				     struct v4l2_subdev_selection *sel)
{
	struct mt9m114 *sensor = ifp_to_mt9m114(sd);
	const struct v4l2_mbus_framefmt *format;
	const struct v4l2_rect *crop;
	int ret = 0;

	/* Crop and compose are only supported on the sink pad. */
	if (sel->pad != 0)
		return -EINVAL;

	mutex_lock(&sensor->lock);

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		sel->r = *__mt9m114_ifp_get_pad_crop(sensor, state, 0,
						     sel->which);
		break;

	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		/*
		 * The crop default and bounds are equal to the sink
		 * format size minus 4 pixels on each side for demosaicing.
		 */
		format = __mt9m114_ifp_get_pad_format(sensor, state, 0,
						      sel->which);

		sel->r.left = 4;
		sel->r.top = 4;
		sel->r.width = format->width - 8;
		sel->r.height = format->height - 8;
		break;

	case V4L2_SEL_TGT_COMPOSE:
		sel->r = *__mt9m114_ifp_get_pad_compose(sensor, state, 0,
							sel->which);
		break;

	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
		/*
		 * The compose default and bounds sizes are equal to the sink
		 * crop rectangle size.
		 */
		crop = __mt9m114_ifp_get_pad_crop(sensor, state, 0, sel->which);
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = crop->width;
		sel->r.height = crop->height;
		break;

	default:
		ret = -EINVAL;
		break;
	}

	mutex_unlock(&sensor->lock);
	return ret;
}

static int mt9m114_ifp_set_selection(struct v4l2_subdev *sd,
				     struct v4l2_subdev_state *state,
				     struct v4l2_subdev_selection *sel)
{
	struct mt9m114 *sensor = ifp_to_mt9m114(sd);
	struct v4l2_mbus_framefmt *format;
	struct v4l2_rect *crop;
	struct v4l2_rect *compose;

	if (sel->target != V4L2_SEL_TGT_CROP &&
	    sel->target != V4L2_SEL_TGT_COMPOSE)
		return -EINVAL;

	/* Crop and compose are only supported on the sink pad. */
	if (sel->pad != 0)
		return -EINVAL;

	format = __mt9m114_ifp_get_pad_format(sensor, state, 0, sel->which);
	crop = __mt9m114_ifp_get_pad_crop(sensor, state, 0, sel->which);
	compose = __mt9m114_ifp_get_pad_compose(sensor, state, 0, sel->which);

	mutex_lock(&sensor->lock);

	if (sel->target == V4L2_SEL_TGT_CROP) {
		/*
		 * Clamp the crop rectangle. Demosaicing removes 4 pixels on
		 * each side of the image.
		 */
		crop->left = clamp_t(unsigned int, ALIGN(sel->r.left, 2), 4,
				     format->width - 4 -
				     MT9M114_SCALER_CROPPED_INPUT_WIDTH);
		crop->top = clamp_t(unsigned int, ALIGN(sel->r.top, 2), 4,
				    format->height - 4 -
				    MT9M114_SCALER_CROPPED_INPUT_HEIGHT);
		crop->width = clamp_t(unsigned int, ALIGN(sel->r.width, 2),
				      MT9M114_SCALER_CROPPED_INPUT_WIDTH,
				      format->width - 4 - crop->left);
		crop->height = clamp_t(unsigned int, ALIGN(sel->r.height, 2),
				       MT9M114_SCALER_CROPPED_INPUT_HEIGHT,
				       format->height - 4 - crop->top);

		sel->r = *crop;

		/* Propagate to the compose rectangle. */
		compose->width = crop->width;
		compose->height = crop->height;
	} else {
		/*
		 * Clamp the compose rectangle. The scaler can only downscale.
		 */
		compose->left = 0;
		compose->top = 0;
		compose->width = clamp_t(unsigned int, ALIGN(sel->r.width, 2),
					 MT9M114_SCALER_CROPPED_INPUT_WIDTH,
					 crop->width);
		compose->height = clamp_t(unsigned int, ALIGN(sel->r.height, 2),
					  MT9M114_SCALER_CROPPED_INPUT_HEIGHT,
					  crop->height);

		sel->r = *compose;
	}

	/* Propagate the compose rectangle to the source format. */
	format = __mt9m114_ifp_get_pad_format(sensor, state, 1, sel->which);
	format->width = compose->width;
	format->height = compose->height;

	mutex_unlock(&sensor->lock);

	return 0;
}

static void mt9m114_ifp_unregistered(struct v4l2_subdev *sd)
{
	struct mt9m114 *sensor = ifp_to_mt9m114(sd);

	v4l2_device_unregister_subdev(&sensor->pa.sd);
}

static int mt9m114_ifp_registered(struct v4l2_subdev *sd)
{
	struct mt9m114 *sensor = ifp_to_mt9m114(sd);
	int ret;

	ret = v4l2_device_register_subdev(sd->v4l2_dev, &sensor->pa.sd);
	if (ret < 0) {
		dev_err(&sensor->client->dev,
			"Failed to register pixel array subdev\n");
		return ret;
	}

	ret = media_create_pad_link(&sensor->pa.sd.entity, 0,
				    &sensor->ifp.sd.entity, 0,
				    MEDIA_LNK_FL_ENABLED |
				    MEDIA_LNK_FL_IMMUTABLE);
	if (ret < 0) {
		dev_err(&sensor->client->dev,
			"Failed to link pixel array to ifp\n");
		v4l2_device_unregister_subdev(&sensor->pa.sd);
		return ret;
	}

	return 0;
}

static const struct v4l2_subdev_video_ops mt9m114_ifp_video_ops = {
	.s_stream = mt9m114_ifp_s_stream,
	.g_frame_interval = mt9m114_ifp_g_frame_interval,
	.s_frame_interval = mt9m114_ifp_s_frame_interval,
};

static const struct v4l2_subdev_pad_ops mt9m114_ifp_pad_ops = {
	.init_cfg = mt9m114_ifp_init_cfg,
	.enum_mbus_code = mt9m114_ifp_enum_mbus_code,
	.enum_frame_size = mt9m114_ifp_enum_framesizes,
	.enum_frame_interval = mt9m114_ifp_enum_frameintervals,
	.get_fmt = mt9m114_ifp_get_fmt,
	.set_fmt = mt9m114_ifp_set_fmt,
	.get_selection = mt9m114_ifp_get_selection,
	.set_selection = mt9m114_ifp_set_selection,
};

static const struct v4l2_subdev_ops mt9m114_ifp_ops = {
	.video = &mt9m114_ifp_video_ops,
	.pad = &mt9m114_ifp_pad_ops,
};

static const struct v4l2_subdev_internal_ops mt9m114_ifp_internal_ops = {
	.registered = mt9m114_ifp_registered,
	.unregistered = mt9m114_ifp_unregistered,
};

static int mt9m114_ifp_init(struct mt9m114 *sensor)
{
	struct v4l2_subdev *sd = &sensor->ifp.sd;
	struct media_pad *pads = sensor->ifp.pads;
	struct v4l2_ctrl_handler *hdl = &sensor->ifp.hdl;
	int ret;

	/* Initialize the subdev. */
	v4l2_i2c_subdev_init(sd, sensor->client, &mt9m114_ifp_ops);
	v4l2_i2c_subdev_set_name(sd, sensor->client, "mt9m114", " ifp");

	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	sd->internal_ops = &mt9m114_ifp_internal_ops;

	/* Initialize the media entity. */
	sd->entity.function = MEDIA_ENT_F_PROC_VIDEO_ISP;
	sd->entity.ops = &mt9m114_entity_ops;
	pads[0].flags = MEDIA_PAD_FL_SINK;
	pads[1].flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&sd->entity, 2, pads);
	if (ret < 0)
		return ret;

	/* Initialize the control handler. */
	v4l2_ctrl_handler_init(hdl, 8);
	v4l2_ctrl_new_std(hdl, &mt9m114_ifp_ctrl_ops,
			  V4L2_CID_AUTO_WHITE_BALANCE,
			  0, 1, 1, 1);
	v4l2_ctrl_new_std(hdl, &mt9m114_ifp_ctrl_ops,
			  V4L2_CID_HFLIP,
			  0, 1, 1, 0);
	v4l2_ctrl_new_std(hdl, &mt9m114_ifp_ctrl_ops,
			  V4L2_CID_VFLIP,
			  0, 1, 1, 0);
	v4l2_ctrl_new_std_menu(hdl, &mt9m114_ifp_ctrl_ops,
			       V4L2_CID_EXPOSURE_AUTO,
			       V4L2_EXPOSURE_MANUAL, 0,
			       V4L2_EXPOSURE_AUTO);
	v4l2_ctrl_new_std(hdl, &mt9m114_ifp_ctrl_ops,
			  V4L2_CID_PIXEL_RATE,
			  sensor->pixrate, sensor->pixrate, 1,
			  sensor->pixrate);

	sensor->ifp.tpg[MT9M114_TPG_PATTERN] =
		v4l2_ctrl_new_std_menu_items(hdl, &mt9m114_ifp_ctrl_ops,
					     V4L2_CID_TEST_PATTERN,
					     ARRAY_SIZE(mt9m114_test_pattern_menu) - 1,
					     0, 0, mt9m114_test_pattern_menu);
	sensor->ifp.tpg[MT9M114_TPG_RED] =
		v4l2_ctrl_new_std(hdl, &mt9m114_ifp_ctrl_ops,
				  V4L2_CID_TEST_PATTERN_RED,
				  0, 1023, 1, 1023);
	sensor->ifp.tpg[MT9M114_TPG_GREEN] =
		v4l2_ctrl_new_std(hdl, &mt9m114_ifp_ctrl_ops,
				  V4L2_CID_TEST_PATTERN_GREENR,
				  0, 1023, 1, 1023);
	sensor->ifp.tpg[MT9M114_TPG_BLUE] =
		v4l2_ctrl_new_std(hdl, &mt9m114_ifp_ctrl_ops,
				  V4L2_CID_TEST_PATTERN_BLUE,
				  0, 1023, 1, 1023);

	v4l2_ctrl_cluster(ARRAY_SIZE(sensor->ifp.tpg), sensor->ifp.tpg);

	if (hdl->error)
		return hdl->error;

	ret = v4l2_ctrl_handler_setup(hdl);
	if (ret < 0)
		return ret;

	sd->ctrl_handler = hdl;

	/* Initialize the pads formats and selection rectangles. */
	mt9m114_ifp_init_cfg(sd, NULL);

	sensor->ifp.frame_rate = MT9M114_MAX_FRAME_RATE;

	return 0;
}

static void mt9m114_ifp_cleanup(struct mt9m114 *sensor)
{
	v4l2_ctrl_handler_free(&sensor->ifp.hdl);
	media_entity_cleanup(&sensor->ifp.sd.entity);
}

/* -----------------------------------------------------------------------------
 * Probe & Remove
 */

static int mt9m114_identify(struct mt9m114 *sensor)
{
	u32 major, minor, release, customer;
	u32 value;
	int ret;

	ret = mt9m114_read(sensor, MT9M114_CHIP_ID, &value);
	if (ret < 0) {
		dev_err(&sensor->client->dev, "Failed to read chip ID\n");
		return -ENXIO;
	}

	if (value != 0x2481) {
		dev_err(&sensor->client->dev, "Invalid chip ID 0x%04x\n",
			value);
		return -ENXIO;
	}

	ret = mt9m114_read(sensor, MT9M114_MON_MAJOR_VERSION, &major);
	ret |= mt9m114_read(sensor, MT9M114_MON_MINOR_VERSION, &minor);
	ret |= mt9m114_read(sensor, MT9M114_MON_RELEASE_VERSION, &release);
	ret |= mt9m114_read(sensor, MT9M114_CUSTOMER_REV, &customer);
	if (ret) {
		dev_err(&sensor->client->dev, "Failed to read version\n");
		return -ENXIO;
	}

	dev_dbg(&sensor->client->dev,
		"monitor v%u.%u.%04x customer rev 0x%04x\n",
		major, minor, release, customer);

	return 0;
}

static int mt9m114_parse_dt(struct mt9m114 *sensor)
{
	struct fwnode_handle *fwnode = dev_fwnode(&sensor->client->dev);
	struct fwnode_handle *ep;
	int ret;

	if (!fwnode)
		return -ENXIO;

	ep = fwnode_graph_get_next_endpoint(fwnode, NULL);
	if (!ep) {
		dev_err(&sensor->client->dev, "No endpoint found\n");
		return -EINVAL;
	}

	sensor->bus_cfg.bus_type = V4L2_MBUS_UNKNOWN;
	ret = v4l2_fwnode_endpoint_alloc_parse(ep, &sensor->bus_cfg);
	fwnode_handle_put(ep);
	if (ret < 0) {
		dev_err(&sensor->client->dev, "Failed to parse endpoint\n");
		goto error;
	}

	switch (sensor->bus_cfg.bus_type) {
	case V4L2_MBUS_CSI2_DPHY:
	case V4L2_MBUS_PARALLEL:
		break;

	default:
		dev_err(&sensor->client->dev, "unsupported bus type %u\n",
			sensor->bus_cfg.bus_type);
		ret = -EINVAL;
		goto error;
	}

	return 0;

error:
	v4l2_fwnode_endpoint_free(&sensor->bus_cfg);
	return ret;
}

static int mt9m114_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct mt9m114 *sensor;
	int ret;

	/* Check if the adapter supports the needed features. */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -EIO;

	sensor = devm_kzalloc(&client->dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;

	mutex_init(&sensor->lock);
	sensor->client = client;

	ret = mt9m114_parse_dt(sensor);
	if (ret < 0)
		goto error_mutex;

	/* Acquire clocks, GPIOs and regulators. */
	sensor->clk = devm_clk_get(&client->dev, NULL);
	if (IS_ERR(sensor->clk)) {
		if (PTR_ERR(sensor->clk) != -EPROBE_DEFER)
			dev_err(&client->dev, "Failed to get clock: %ld\n",
				PTR_ERR(sensor->clk));

		ret = PTR_ERR(sensor->clk);
		goto error_ep_free;
	}

	sensor->reset = devm_gpiod_get_optional(&client->dev, "reset",
						GPIOD_OUT_LOW);
	if (IS_ERR(sensor->reset)) {
		if (PTR_ERR(sensor->reset) != -EPROBE_DEFER)
			dev_err(&client->dev, "Failed to get reset GPIO: %ld\n",
				PTR_ERR(sensor->reset));

		ret = PTR_ERR(sensor->reset);
		goto error_ep_free;
	}

	sensor->supplies[0].supply = "vddio";
	sensor->supplies[1].supply = "vdd";
	sensor->supplies[2].supply = "vaa";

	ret = devm_regulator_bulk_get(&client->dev,
				      ARRAY_SIZE(sensor->supplies),
				      sensor->supplies);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to get regulators: %d\n", ret);
		goto error_ep_free;
	}

	/* Identify the sensor. */
	ret = mt9m114_power_on(sensor);
	if (ret < 0)
		goto error_ep_free;

	ret = mt9m114_identify(sensor);
	if (ret < 0)
		goto error_power_off;

	/* Reset and initialize sensor. */
	ret = mt9m114_initialize(sensor);
	if (ret < 0)
		goto error_power_off;

	/* Initialize the subdevices. */
	ret = mt9m114_pa_init(sensor);
	if (ret < 0)
		goto error_power_off;

	ret = mt9m114_ifp_init(sensor);
	if (ret < 0)
		goto error_subdevs;

	ret = v4l2_async_register_subdev(&sensor->ifp.sd);
	if (ret < 0)
		goto error_subdevs;

	dev_info(&sensor->client->dev, "MT9M114 initialized\n");

	return 0;

error_subdevs:
	mt9m114_ifp_cleanup(sensor);
	mt9m114_pa_cleanup(sensor);
error_power_off:
	mt9m114_power_off(sensor);
error_ep_free:
	v4l2_fwnode_endpoint_free(&sensor->bus_cfg);
error_mutex:
	mutex_destroy(&sensor->lock);
	return ret;
}

static int mt9m114_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct mt9m114 *sensor = ifp_to_mt9m114(sd);

	mt9m114_ifp_cleanup(sensor);
	mt9m114_pa_cleanup(sensor);
	v4l2_async_unregister_subdev(&sensor->ifp.sd);
	mt9m114_power_off(sensor);
	v4l2_fwnode_endpoint_free(&sensor->bus_cfg);
	mutex_destroy(&sensor->lock);

	return 0;
}

static const struct of_device_id mt9m114_of_ids[] = {
	{ .compatible = "onnn,mt9m114" },
	{ /* sentinel */ },
};

MODULE_DEVICE_TABLE(of, mt9m114_of_ids);

static struct i2c_driver mt9m114_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.of_match_table = mt9m114_of_ids,
		.name	= "mt9m114",
	},
	.probe		= mt9m114_probe,
	.remove		= mt9m114_remove,
};

module_i2c_driver(mt9m114_driver);

MODULE_DESCRIPTION("onsemi MT9M114 Sensor Driver");
MODULE_AUTHOR("Laurent Pinchart <laurent.pinchart@ideasonboard.com>");
MODULE_LICENSE("GPL v2");
