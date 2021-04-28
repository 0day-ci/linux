/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 - present Synopsys, Inc. and/or its affiliates.
 * HDMI generic PHY options.
 *
 * Author: Nelson Costa <nelson.costa@synopsys.com>
 */
#ifndef __PHY_HDMI_H_
#define __PHY_HDMI_H_

#include <linux/types.h>

/**
 * struct phy_configure_opts_hdmi - HDMI PHY configuration set
 *
 * This structure is used to represent the configuration state of an
 * HDMI PHY.
 */
struct phy_configure_opts_hdmi {
	/**
	 * @color_depth:
	 *
	 * Color depth, as specified by HDMI specification, represents the
	 * number of bits per pixel.
	 *
	 * Allowed values: 24, 30, 36, 48
	 *
	 */
	u8 color_depth;

	/**
	 * @tmds_bit_clock_ratio:
	 *
	 * Flag indicating, as specified by HDMI specification, the relation
	 * between TMDS Clock Rate and TMDS Character Rate.
	 *
	 * As specified by HDMI specification:
	 *
	 * tmds_bit_clock_ratio = 0, for TMDS Character Rates <= 340 Mcsc
	 * (TMDS Clock Rate = TMDS Character Rate)
	 *
	 * tmds_bit_clock_ratio = 1, for TMDS Character Rates > 340 Mcsc
	 * (TMDS Clock Rate = TMDS Character Rate / 4)
	 *
	 */
	u8 tmds_bit_clock_ratio;

	/**
	 * @scrambling:
	 *
	 * Scrambling, as specified by HDMI specification, enables the technique
	 * to reduce the EMI/RFI.
	 *
	 */
	u8 scrambling;

	/**
	 * @calibration_acq:
	 *
	 * Calibration acquisitions number for the calibration algorithm.
	 *
	 */
	unsigned int calibration_acq;

	/**
	 * @calibration_force:
	 *
	 * Flag indicating, to force calibration algorithm even if the MPLL
	 * status didn't change from previous run calibration.
	 *
	 */
	u8 calibration_force;

	/**
	 * @set_color_depth:
	 *
	 * Flag indicating, whether or not reconfigure deep_color
	 * to requested values.
	 *
	 */
	u8 set_color_depth : 1;

	/**
	 * @set_tmds_bit_clock_ratio:
	 *
	 * Flag indicating, whether or not reconfigure tmds_bit_clock_ratio
	 * to requested values.
	 *
	 */
	u8 set_tmds_bit_clock_ratio : 1;

	/**
	 * @set_scrambling:
	 *
	 * Flag indicating, whether or not reconfigure scrambling
	 * to requested values.
	 *
	 */
	u8 set_scrambling : 1;
};

#endif /* __PHY_HDMI_H_ */
