/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef __LINUX_HANTRO_MEDIA_H__
#define __LINUX_HANTRO_MEDIA_H__

/*
 * V4L2_CID_HANTRO_HEVC_SLICE_HEADER_SKIP -
 * the number of data (in bits) to skip in the
 * slice segment header.
 * If non-IDR, the bits to be skipped go from syntax element "pic_output_flag"
 * to before syntax element "slice_temporal_mvp_enabled_flag".
 * If IDR, the skipped bits are just "pic_output_flag"
 * (separate_colour_plane_flag is not supported).
 */
#define V4L2_CID_HANTRO_HEVC_SLICE_HEADER_SKIP	(V4L2_CID_USER_HANTRO_BASE + 0)

#endif
