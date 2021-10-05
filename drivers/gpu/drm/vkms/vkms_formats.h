/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef _VKMS_FORMATS_H_
#define _VKMS_FORMATS_H_

#include <drm/drm_rect.h>

#define pixel_offset(composer, x, y) \
	((composer)->offset + ((y) * (composer)->pitch) + ((x) * (composer)->cpp))

/*
 * packed_pixels_addr - Get the pointer to pixel of a given pair of coordinates
 *
 * @composer: Buffer metadata
 * @x: The x(width) coordinate of the 2D buffer
 * @y: The y(Heigth) coordinate of the 2D buffer
 *
 * Takes the information stored in the composer, a pair of coordinates, and
 * returns the address of the first color channel.
 * This function assumes the channels are packed together, i.e. a color channel
 * comes immediately after another. And therefore, this function doesn't work
 * for YUV with chroma subsampling (e.g. YUV420 and NV21).
 */
void *packed_pixels_addr(struct vkms_composer *composer, int x, int y)
{
	int offset = pixel_offset(composer, x, y);

	return (u8 *)composer->map[0].vaddr + offset;
}

u64 ARGB8888_to_ARGB16161616(struct vkms_composer *composer, int x, int y)
{
	u8 *pixel_addr = packed_pixels_addr(composer, x, y);

	/*
	 * Organizes the channels in their respective positions and converts
	 * the 8 bits channel to 16.
	 * The 257 is the "conversion ratio". This number is obtained by the
	 * (2^16 - 1) / (2^8 - 1) division. Which, in this case, tries to get
	 * the best color value in a color space with more possibilities.
	 * And a similar idea applies to others RGB color conversions.
	 */
	return ((u64)pixel_addr[3] * 257) << 48 |
	       ((u64)pixel_addr[2] * 257) << 32 |
	       ((u64)pixel_addr[1] * 257) << 16 |
	       ((u64)pixel_addr[0] * 257);
}

u64 XRGB8888_to_ARGB16161616(struct vkms_composer *composer, int x, int y)
{
	u8 *pixel_addr = packed_pixels_addr(composer, x, y);

	/*
	 * The same as the ARGB8888 but with the alpha channel as the
	 * maximum value as possible.
	 */
	return 0xffffllu << 48 |
	       ((u64)pixel_addr[2] * 257) << 32 |
	       ((u64)pixel_addr[1] * 257) << 16 |
	       ((u64)pixel_addr[0] * 257);
}

u64 get_ARGB16161616(struct vkms_composer *composer, int x, int y)
{
	__le64 *pixel_addr = packed_pixels_addr(composer, x, y);

	/*
	 * Because the format byte order is in little-endian and this code
	 * needs to run on big-endian machines too, we need modify
	 * the byte order from little-endian to the CPU native byte order.
	 */
	return le64_to_cpu(*pixel_addr);
}

/*
 * The following functions are used as blend operations. But unlike the
 * `alpha_blend`, these functions take an ARGB16161616 pixel from the
 * source, convert it to a specific format, and store it in the destination.
 *
 * They are used in the `compose_active_planes` and `write_wb_buffer` to
 * copy and convert one pixel from/to the output buffer to/from
 * another buffer (e.g. writeback buffer, primary plane buffer).
 */

void convert_to_ARGB8888(u64 argb_src1, u64 argb_src2, int x, int y,
			 struct vkms_composer *dst_composer)
{
	u8 *pixel_addr = packed_pixels_addr(dst_composer, x, y);

	/*
	 * This sequence below is important because the format's byte order is
	 * in little-endian. In the case of the ARGB8888 the memory is
	 * organized this way:
	 *
	 * | Addr     | = blue channel
	 * | Addr + 1 | = green channel
	 * | Addr + 2 | = Red channel
	 * | Addr + 3 | = Alpha channel
	 */
	pixel_addr[0] = DIV_ROUND_UP(argb_src1 & 0xffffllu, 257);
	pixel_addr[1] = DIV_ROUND_UP((argb_src1 & (0xffffllu << 16)) >> 16, 257);
	pixel_addr[2] = DIV_ROUND_UP((argb_src1 & (0xffffllu << 32)) >> 32, 257);
	pixel_addr[3] = DIV_ROUND_UP((argb_src1 & (0xffffllu << 48)) >> 48, 257);
}

void convert_to_XRGB8888(u64 argb_src1, u64 argb_src2, int x, int y,
			 struct vkms_composer *dst_composer)
{
	u8 *pixel_addr = packed_pixels_addr(dst_composer, x, y);

	pixel_addr[0] = DIV_ROUND_UP(argb_src1 & 0xffffllu, 257);
	pixel_addr[1] = DIV_ROUND_UP((argb_src1 & (0xffffllu << 16)) >> 16, 257);
	pixel_addr[2] = DIV_ROUND_UP((argb_src1 & (0xffffllu << 32)) >> 32, 257);
	pixel_addr[3] = 0xff;
}

void convert_to_ARGB16161616(u64 argb_src1, u64 argb_src2, int x, int y,
			     struct vkms_composer *dst_composer)
{
	__le64 *pixel_addr = packed_pixels_addr(dst_composer, x, y);

	*pixel_addr = cpu_to_le64(argb_src1);
}

#endif /* _VKMS_FORMATS_H_ */
