/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Intel Keem Bay camera VPU source configuration
 *
 * Copyright (C) 2021 Intel Corporation
 */

#ifndef KEEMBAY_VPU_SRC_H
#define KEEMBAY_VPU_SRC_H

/*
 * struct kmb_ic_img_size - The structure contains information about image size
 *
 * @w: Image width
 * @h: Image height
 */
struct kmb_ic_img_size {
	u32 w;
	u32 h;
};

/*
 * struct kmb_ic_img_rect - The struct represents the coordinates of a
 *                          rectangular image
 *
 * @x1: Position of the bottom left corner
 * @y1: Position of the top left corner
 * @x2: Position of the bottom right corner
 * @y2: Position of the top right corner
 */
struct kmb_ic_img_rect {
	s32 x1;
	s32 y1;
	s32 x2;
	s32 y2;
};

/*
 * enum kmb_ic_source_instance - HW mipi/cif input devices
 *
 * @KMB_IC_SOURCE_0:
 * @KMB_IC_SOURCE_1:
 * @KMB_IC_SOURCE_2:
 * @KMB_IC_SOURCE_3:
 * @KMB_IC_SOURCE_4:
 * @KMB_IC_SOURCE_5:
 */
enum kmb_ic_source_instance {
	KMB_IC_SOURCE_0 = 0,
	KMB_IC_SOURCE_1 = 1,
	KMB_IC_SOURCE_2 = 2,
	KMB_IC_SOURCE_3 = 3,
	KMB_IC_SOURCE_4 = 4,
	KMB_IC_SOURCE_5 = 5,
};

/*
 * enum kmb_ic_bayer_format - Bayer pattern order
 *
 * @KMB_IC_BAYER_FORMAT_GRBG: Gr R B Gr
 * @KMB_IC_BAYER_FORMAT_RGGB: R Gr Gr B
 * @KMB_IC_BAYER_FORMAT_GBRG: Gr B R Gr
 * @KMB_IC_BAYER_FORMAT_BGGR: B Gr Gr R
 */
enum kmb_ic_bayer_format {
	KMB_IC_BAYER_FORMAT_GRBG = 0,
	KMB_IC_BAYER_FORMAT_RGGB = 1,
	KMB_IC_BAYER_FORMAT_GBRG = 2,
	KMB_IC_BAYER_FORMAT_BGGR = 3,
};

/*
 * enum kmb_ic_mipi_rx_ctrl_rec_not - List of receiver Id's for a specific
 *                                    sensor
 *
 * @KMB_IC_SIPP_DEVICE0:
 * @KMB_IC_SIPP_DEVICE1:
 * @KMB_IC_SIPP_DEVICE2:
 * @KMB_IC_SIPP_DEVICE3:
 * @KMB_IC_CIF0_DEVICE4:
 * @KMB_IC_CIF1_DEVICE5:
 */
enum kmb_ic_mipi_rx_ctrl_rec_not {
	KMB_IC_SIPP_DEVICE0 = 0,
	KMB_IC_SIPP_DEVICE1 = 1,
	KMB_IC_SIPP_DEVICE2 = 2,
	KMB_IC_SIPP_DEVICE3 = 3,
	KMB_IC_CIF0_DEVICE4 = 4,
	KMB_IC_CIF1_DEVICE5 = 5,
};

/*
 * enum kmb_ic_mipi_rx_ctrl_not - MIPI controller from chip
 *
 * @KMB_IC_MIPI_CTRL_0:
 * @KMB_IC_MIPI_CTRL_1:
 * @KMB_IC_MIPI_CTRL_2:
 * @KMB_IC_MIPI_CTRL_3:
 * @KMB_IC_MIPI_CTRL_4:
 * @KMB_IC_MIPI_CTRL_5:
 */
enum kmb_ic_mipi_rx_ctrl_not {
	KMB_IC_MIPI_CTRL_0 = 0,
	KMB_IC_MIPI_CTRL_1 = 1,
	KMB_IC_MIPI_CTRL_2 = 2,
	KMB_IC_MIPI_CTRL_3 = 3,
	KMB_IC_MIPI_CTRL_4 = 4,
	KMB_IC_MIPI_CTRL_5 = 5,
};

/*
 * enum kmb_ic_mipi_ex_data_type - All supported raw, sensor input formats
 *
 * @IC_IPIPE_YUV_420_B8:
 * @IC_IPIPE_RAW_8:
 * @IC_IPIPE_RAW_10:
 * @IC_IPIPE_RAW_12:
 * @IC_IPIPE_RAW_14:
 * @IC_IPIPE_EMBEDDED_8BIT:
 */
enum kmb_ic_mipi_rx_data_type {
	IC_IPIPE_YUV_420_B8       = 0x18,
	IC_IPIPE_RAW_8            = 0x2A,
	IC_IPIPE_RAW_10           = 0x2B,
	IC_IPIPE_RAW_12           = 0x2C,
	IC_IPIPE_RAW_14           = 0x2D,
	IC_IPIPE_EMBEDDED_8BIT    = 0x12
};

/*
 * struct kmb_ic_source_config_dynamic - Per-source configuration of parameters
 *                                       which can be modified dynamically.
 *                                       Setting will take effect during the
 *                                       next blanking interval
 *
 * @notification_line: Line number upon which IC_EVENT_TYPE_LINE will be sent
 *                     to the Lean OS. Set to -1 to disable notification
 */
struct kmb_ic_source_config_dynamic {
	s32 notification_line;
};

/*
 * struct kmb_ic_mipi_config - Mipi RX data configuration
 *
 * @no_controller: Number of controller
 * @no_lanes: Number of lanes
 * @lane_rate_mbps: Lane rate
 * @data_type: Mipi RX data type
 * @data_mode: Data mode
 * @rec_nrl:
 */
struct kmb_ic_mipi_config {
	u32 no_controller;
	u32 no_lanes;
	u32 lane_rate_mbps;
	u32 data_type;
	u32 data_mode;
	u32 rec_nrl;
};

/*
 * struct kmb_ic_source_config - Per-source configuration parameters - mostly
 *                               information needed to configure the MIPI Rx
 *                               filter
 *
 * @camera_output_size: Max frame size output by the camera
 * @crop_window: Crop window coordinates
 * @bayer_format: Bayer Format - Raw, Demosaic and LSC blocks should be
 *                programmed to match the Bayer order specified here.
 * @bpp: Bits per pixel
 * @mipi_rx_data: MIPI RX data configuration
 * @no_exposure: Number of different exposure frames
 * @metadata_width: Metadata width
 * @metadata_height: Medata height
 * @metadata_data_type: Metadata data type
 */
struct kmb_ic_source_config {
	struct kmb_ic_img_size camera_output_size;
	struct kmb_ic_img_rect crop_window;

	u32 bayer_format;
	u32 bpp;

	struct kmb_ic_mipi_config mipi_rx_data;

	u32 no_exposure;
	u32 metadata_width;
	u32 metadata_height;
	u32 metadata_data_type;
} __aligned(64);

#endif  /* KEEMBAY_VPU_SRC_H */
