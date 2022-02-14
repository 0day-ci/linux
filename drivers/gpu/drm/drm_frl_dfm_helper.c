// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corp
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <drm/drm_frl_dfm_helper.h>
#include <drm/drm_connector.h>

/* Total frl charecters per super block */
static unsigned int drm_get_frl_char_per_super_blk(unsigned int lanes)
{
	unsigned int frl_char_per_sb;

	frl_char_per_sb = (4 * FRL_CHAR_PER_CHAR_BLK) + lanes;
	return frl_char_per_sb;
}

/*
 * Determine the overhead due to the inclusion of
 * the SR and SSB FRL charecters used for
 * super block framing
 */
static unsigned int drm_get_overhead_super_blk(unsigned int lanes)
{
	return (lanes * EFFICIENCY_MULTIPLIER) / drm_get_frl_char_per_super_blk(lanes);
}

/*
 * Determine the overhead due to the inclusion of RS FEC pairity
 * symbols. Each charecter block uses 8 FRL charecters for RS Pairity
 * and there are 4 charecter blocks per super block
 */
static unsigned int drm_get_overhead_rs(unsigned int lanes)
{
	return (8 * 4 * EFFICIENCY_MULTIPLIER) /  drm_get_frl_char_per_super_blk(lanes);
}

/* Determine the overhead due to FRL Map charecters.
 * In a bandwidth constrained application, the FRL packets will be long,
 * there will typically be two FRL Map Charecters per Super Block most of the time.
 * When a tracnsition occurs between Hactive and Hblank (uncomperssed video) or
 * HCactive and HCblank (compressed video transport), there may be a
 * third FRL Map Charected. Therefore this spec assumes 2.5 FRL Map Charecters
 * per Super Block.
 */
static unsigned int drm_get_overhead_frl_map_char(unsigned int lanes)
{
	return (25  * EFFICIENCY_MULTIPLIER) / (10 * drm_get_frl_char_per_super_blk(lanes));
}

/* Total minimum overhead multiplied by EFFICIENCY_MULIPLIER */
static unsigned int drm_get_total_minimum_overhead(unsigned int lanes)
{
	unsigned int total_overhead_min;
	unsigned int overhead_sb = drm_get_overhead_super_blk(lanes);
	unsigned int overhead_rs = drm_get_overhead_rs(lanes);
	unsigned int overhead_map = drm_get_overhead_frl_map_char(lanes);

	total_overhead_min = overhead_sb + overhead_rs + overhead_map;

	return total_overhead_min;
}

/*
 * Additional margin to the overhead is provided to account for the possibility
 * of more Map Charecters, zero padding at the end of HCactive, and other minor
 * items
 */
static unsigned int drm_get_max_overhead(unsigned int total_overhead_min)
{
	unsigned int total_overhead_max;

	total_overhead_max = total_overhead_min + OVERHEAD_M;
	return total_overhead_max;
}

/* Collect the link charecteristics */

/* Determine the maximum legal pixel rate */
static unsigned int drm_get_max_legal_pixel_rate(unsigned int fpixel_clock_nominal_k)
{
	unsigned int fpixel_clock_max_k = (fpixel_clock_nominal_k *
				  (1000 + TOLERANCE_PIXEL_CLOCK)) / 1000;
	return fpixel_clock_max_k;
}

/* Determine the minimum Video Line period */
static unsigned int drm_get_min_video_line_period(unsigned int hactive, unsigned int hblank,
						  unsigned int fpixel_clock_max_k)
{
	unsigned int line_time_ns;

	line_time_ns = ((hactive + hblank) * FRL_TIMING_NS_MULTIPLIER) /
		       fpixel_clock_max_k;
	return line_time_ns;
}

/* Determine the worst-case slow FRL Bit Rate in kbps*/
static unsigned int drm_get_min_frl_bit_rate(unsigned int frl_bit_rate_nominal_k)
{
	unsigned int frl_bit_rate_min_k;

	frl_bit_rate_min_k = (frl_bit_rate_nominal_k / 1000000) *
			     (1000000 - TOLERANCE_FRL_BIT_RATE);
	return frl_bit_rate_min_k;
}

/* Determine the worst-case slow FRL Charecter Rate */
static unsigned int drm_get_min_frl_char_rate(unsigned int frl_bit_rate_min_k)
{
	unsigned int frl_char_rate_min_k;

	frl_char_rate_min_k = frl_bit_rate_min_k / 18;
	return frl_char_rate_min_k;
}

/* Determine the Minimum Total FRL charecters per line period */
static unsigned int
drm_get_total_frl_char_per_line_period(unsigned int line_time_ns, unsigned int frl_char_rate_min_k,
				       unsigned int lanes)
{
	unsigned int frl_char_per_line_period;

	frl_char_per_line_period = (line_time_ns * frl_char_rate_min_k * lanes *
				    1000) / FRL_TIMING_NS_MULTIPLIER;
	return frl_char_per_line_period;
}

/* Audio Support Verification Computations */

/*
 * Determine Audio Related Packet Rate considering the audio clock
 * increased to maximim rate permitted by Tolerance Audio clock
 */
static unsigned int
drm_get_audio_pkt_rate(unsigned int f_audio, unsigned int num_audio_pkt)
{
	unsigned int audio_pkt_rate;

	audio_pkt_rate = ((f_audio *  num_audio_pkt + (2 * ACR_RATE_MAX)) *
			 (1000000 + TOLERANCE_AUDIO_CLOCK)) / 1000000;
	return audio_pkt_rate;
}

/*
 * Average required packets per line is
 * Number of audio packets needed during Hblank
 */
static unsigned int
drm_get_audio_pkts_hblank(unsigned int audio_pkt_rate, unsigned int line_time_ns)
{
	unsigned int avg_audio_pkts_per_line;

	avg_audio_pkts_per_line = DIV_ROUND_UP(audio_pkt_rate * line_time_ns,
					       FRL_TIMING_NS_MULTIPLIER);
	return avg_audio_pkts_per_line;
}

/*
 * Minimum required Hblank assuming no Control Period RC Compression
 * This includes Video Guard band, Two Island Guard bands, two 12 character
 * Control Periods and 32 * AudioPackets_Line.
 * In addition, 32 character periods are allocated for the transmission of an
 * ACR packet
 */
static unsigned int
drm_get_audio_hblank_min(unsigned int audio_pkts_line)
{
	unsigned int  hblank_audio_min;

	hblank_audio_min = 32 + 32 * audio_pkts_line;
	return hblank_audio_min;
}

/*
 * During the Hblank period, Audio packets (32 frl characters each),
 * ACR packets (32 frl characters each), Island guard band (4 total frl characters)
 * and Video guard band (3 frl characters) do not benefit from RC compression
 * Therefore start by determining the number of Control Characters that maybe
 * RC compressible
 */
static unsigned int
drm_get_num_char_rc_compressible(unsigned int color_format,
				 unsigned int bpc, unsigned int audio_packets_line, unsigned int hblank)
{
	unsigned int cfrl_free;
	unsigned int kcd, k420;

	if (color_format == DRM_COLOR_FORMAT_YCBCR420)
		k420 = 2;
	else
		k420 = 1;

	if (color_format == DRM_COLOR_FORMAT_YCBCR422)
		kcd = 1;
	else
		kcd = bpc / 8;

	cfrl_free = max(((hblank * kcd) / k420 - 32 * audio_packets_line - 7),
			U32_MIN);
	return cfrl_free;
}

/*
 * Determine the actual number of characters made available by
 * RC compression
 */
static unsigned int
drm_get_num_char_compression_savings(unsigned int cfrl_free)
{
	/*In order to be conservative, situations are considered where
	 * maximum RC compression may not be possible.
	 * Add one character each for RC break caused by:
	 * • Island Preamble not aligned to the RC Compression
	 * • Video Preamble not aligned to the RC Compression
	 * • HSYNC lead edge not aligned to the RC Compression
	 * • HSYNC trail edge not aligned to the RC Compression
	 */
	const unsigned int cfrl_margin = 4;
	unsigned int cfrl_savings = max(((7 * cfrl_free) / 8) - cfrl_margin, U32_MIN);
	return cfrl_savings;
}

static unsigned int
drm_get_frl_bits_per_pixel(unsigned int color_format, unsigned int bpc)
{
	unsigned int kcd, k420, bpp;

	if (color_format == DRM_COLOR_FORMAT_YCBCR420)
		k420 = 2;
	else
		k420 = 1;

	if (color_format == DRM_COLOR_FORMAT_YCBCR422)
		kcd = 1;
	else
		kcd = bpc / 8;

	bpp = (24 * kcd) / k420;
	return bpp;
}

static unsigned int
drm_get_video_bytes_per_line(unsigned int bpp, unsigned int hactive)
{
	unsigned int bytes_per_line;

	bytes_per_line = (bpp * hactive) / 8;
	return bytes_per_line;
}

/*
 * Determine the required number of tribytes to carry active video
 * per line
 */
static unsigned int
drm_get_active_video_tribytes_reqd(unsigned int bytes_per_line)
{
	unsigned int tribyte_active;

	tribyte_active = DIV_ROUND_UP(bytes_per_line, 3);
	return tribyte_active;
}

/* Determine the total available tribytes during the blanking period */
static unsigned int
drm_get_blanking_tribytes_avail(unsigned int color_format,
				unsigned int hblank, unsigned int bpc)
{
	unsigned int tribytes_blank;
	unsigned int kcd, k420;

	if (color_format == DRM_COLOR_FORMAT_YCBCR420)
		k420 = 2;
	else
		k420 = 1;

	if (color_format == DRM_COLOR_FORMAT_YCBCR422)
		kcd = 1;
	else
		kcd = bpc / 8;

	tribytes_blank = (hblank * kcd) / k420;
	return tribytes_blank;
}

/* Determine the average tribyte rate in kilo tribytes per sec */
static unsigned int
drm_get_avg_tribyte_rate(unsigned int pixel_clk_max_khz, unsigned int tb_active, unsigned int tb_blank,
			 unsigned int hactive, unsigned int hblank)
{
	unsigned int ftb_avg_k;

	ftb_avg_k = (pixel_clk_max_khz * (tb_active + tb_blank)) / (hactive + hblank);
	return ftb_avg_k;
}

/*
 * Determine the time required to transmit the active portion of the
 * minimum possible active line period in the base timing
 */
static unsigned int
drm_get_tactive_ref(unsigned int line_time_ns, unsigned int hblank, unsigned int hactive)
{
	unsigned int tactive_ref_ns;

	tactive_ref_ns = (line_time_ns * hactive) / (hblank + hactive);
	return tactive_ref_ns;
}

/*
 * Determine the time required to transmit the Video blanking portion
 * of the minimum possible active line period in the base timing
 */
static unsigned int
drm_get_tblank_ref(unsigned int line_time_ns, unsigned int hblank, unsigned int hactive)
{
	unsigned int tblank_ref_ns;

	tblank_ref_ns = (line_time_ns * hactive) / (hblank + hactive);
	return tblank_ref_ns;
}

/*
 * Determine the minimum time necessary to transmit the active tribytes
 * considering frl bandwidth limitation.
 * Given the available bandwidth (i.e after overhead is considered),
 * tactive_min represents the amount of time needed to transmit all the
 * active data
 */
static unsigned int
drm_get_tactive_min(unsigned int num_lanes, unsigned int tribyte_active,
		    unsigned int overhead_max_k, unsigned int frl_char_min_rate_k)
{
	unsigned int tactive_min_ns, nr, dr;

	nr = (3 * tribyte_active * FRL_TIMING_NS_MULTIPLIER) / 2;
	dr = (num_lanes * frl_char_min_rate_k * 1000 *
	      (EFFICIENCY_MULTIPLIER - overhead_max_k)) / EFFICIENCY_MULTIPLIER;
	tactive_min_ns = nr / dr;

	return tactive_min_ns;
}

/*
 * Determine the minimum time necessary to transmit the video blanking
 * tribytes considering frl bandwidth limitations
 */
static unsigned int
drm_get_tblank_min(unsigned int num_lanes, unsigned int tribyte_blank,
		   unsigned int overhead_max_k, unsigned int frl_char_min_rate_k)
{
	unsigned int tblank_min_ns, nr, dr;

	nr = tribyte_blank * FRL_TIMING_NS_MULTIPLIER;
	dr = (num_lanes * frl_char_min_rate_k * 1000 *
	      (EFFICIENCY_MULTIPLIER - overhead_max_k)) / EFFICIENCY_MULTIPLIER;
	tblank_min_ns = nr / dr;
	return tblank_min_ns;
}

/* Determine the disparity in tribytes */
static unsigned int
drm_get_tribytes_borrowed(unsigned int tborrowed_ns, unsigned int ftb_avg_k)
{
	unsigned int tribytes_borrowed;

	tribytes_borrowed = DIV_ROUND_UP((tborrowed_ns * ftb_avg_k * 1000),
					 FRL_TIMING_NS_MULTIPLIER);
	return tribytes_borrowed;
}

/*
 * Determine the actual number of payload FRL characters required to carry each
 * video line
 */
static unsigned int
drm_get_frl_char_payload_actual(unsigned int tribytes_active, unsigned int tribytes_blank, unsigned int cfrl_savings)
{
	unsigned int frl_char_payload_actual;

	frl_char_payload_actual = DIV_ROUND_UP(3 * tribytes_active, 2) + tribytes_blank - cfrl_savings;
	return frl_char_payload_actual;
}

/* Determine the payload utilization of the total number of FRL characters */
static unsigned int
drm_compute_payload_utilization(unsigned int frl_char_payload_actual, unsigned int frl_char_per_line_period)
{
	unsigned int utilization;

	utilization = (frl_char_payload_actual * EFFICIENCY_MULTIPLIER) / frl_char_per_line_period;
	return utilization;
}
