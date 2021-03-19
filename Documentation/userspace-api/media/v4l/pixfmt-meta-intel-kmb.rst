.. SPDX-License-Identifier: GPL-2.0 OR GFDL-1.1-no-invariants-or-later

.. _v4l2-meta-fmt-params:
.. _v4l2-meta-fmt-stats:

*******************************************************************
V4L2_META_FMT_KMB_PARAMS ('kmbp'), V4L2_META_FMT_KMB_STATS ('kmbs')
*******************************************************************

.. kmb_isp_stats

ISP statistics
==============

The Keembay ISP statistics blocks collect different statistics over
an input Bayer frame in non-HDR mode, or up to three input Bayer frames
in HDR mode. Those statistics are obtained from the "keembay-metadata-stats"
metadata capture video node, using the :c:type:`v4l2_meta_format` interface.
They are formatted as described by the :c:type:`kmb_isp_stats` structure.

The statistics collected are AE/AWB (Auto-exposure/Auto-white balance),
AF (Auto-focus) filter response, luma histogram, rgb histograms and dehaze statistics.
Dehaze statistic are collected after HDR fusion in HDR mode.

The struct :c:type:`kmb_isp_params` contain all configurable parameters for the
statistics:

- The struct :c:type:`kmb_raw_params` contain enable flags for all
  statistics except dehaze (always enabled) and configuration for flicker rows
  statistics.
- The struct :c:type:`kmb_ae_awb_params` contain configuration parameters for AE/AWB
  statistics.
- The struct :c:type:`kmb_af_params` contain configuration for AF (Auto-focus) filter
  response statistics.
- The struct :c:type:`kmb_hist_params` contain configuration for luma and rgb histograms.
- The struct :c:type:`kmb_hist_params` contain configuration for luma and rgb histograms.
- The struct :c:type:`kmb_dehaze_params` contain configuration for dehaze statistics.

.. code-block:: c

	struct kmb_isp_stats {
		struct {
			__u8 ae_awb_stats[KMB_CAM_AE_AWB_STATS_SIZE];
			__u8 af_stats[KMB_CAM_AF_STATS_SIZE];
			__u8 hist_luma[KMB_CAM_HIST_LUMA_SIZE];
			__u8 hist_rgb[KMB_CAM_HIST_RGB_SIZE];
			__u8 flicker_rows[KMB_CAM_FLICKER_ROWS_SIZE];
		} exposure[KMB_CAM_MAX_EXPOSURES];
		__u8 dehaze[MAX_DHZ_AIRLIGHT_STATS_SIZE];
		struct kmb_isp_stats_flags update;
	};

.. kmb_isp_stats

ISP parameters
==============

The ISP parameters are passed to the "keembay-metadata-params" metadata
output video node, using the :c:type:`v4l2_meta_format` interface. They are
formatted as described by the :c:type:`kmb_isp_params` structure.

Both ISP statistics and ISP parameters described here are closely tied to
the underlying camera sub-system (VPU Camera) APIs. They are usually consumed
and produced by dedicated user space libraries that comprise the important
tuning tools, thus freeing the developers from being bothered with the low
level hardware and algorithm details.

.. code-block:: c

	struct kmb_isp_params {
		struct kmb_isp_params_flags update;
		struct kmb_blc_params blc[KMB_CAM_MAX_EXPOSURES];
		struct kmb_sigma_dns_params sigma_dns[KMB_CAM_MAX_EXPOSURES];
		struct kmb_lsc_params lsc;
		struct kmb_raw_params raw;
		struct kmb_ae_awb_params ae_awb;
		struct kmb_af_params af;
		struct kmb_hist_params histogram;
		struct kmb_lca_params lca;
		struct kmb_debayer_params debayer;
		struct kmb_dog_dns_params dog_dns;
		struct kmb_luma_dns_params luma_dns;
		struct kmb_sharpen_params sharpen;
		struct kmb_chroma_gen_params chroma_gen;
		struct kmb_median_params median;
		struct kmb_chroma_dns_params chroma_dns;
		struct kmb_color_comb_params color_comb;
		struct kmb_hdr_params hdr;
		struct kmb_lut_params lut;
		struct kmb_tnf_params tnf;
		struct kmb_dehaze_params dehaze;
		struct kmb_warp_params warp;
	};

Keembay ISP uAPI data types
===============================

.. kernel-doc:: include/uapi/linux/keembay-isp-ctl.h
