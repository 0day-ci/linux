// SPDX-License-Identifier: GPL-2.0+
/*
 * NVIDIA Tegra Video decoder driver
 *
 * Copyright (c) 2016 Dmitry Osipenko <digetx@gmail.com>
 *
 */

#include "vde.h"

struct bitstream_reader {
	const u8 *data_ptr;
	u32 bitstream_end;
	u32 data_offset;
	uint bit_shift;
	bool rbsp_mode;
	bool error;
};

static inline void bitstream_init(struct bitstream_reader *reader,
				  void *data, size_t size)
{
	reader->bitstream_end = size;
	reader->data_ptr = data;
	reader->data_offset = 0;
	reader->bit_shift = 0;
	reader->rbsp_mode = 1;
	reader->error = 0;
}

static inline int check_range(struct bitstream_reader *reader, u32 offset)
{
	if (reader->data_offset + offset >= reader->bitstream_end)
		return -ENOSPC;

	return 0;
}

static inline void bitstream_reader_inc_offset(struct bitstream_reader *reader,
					       u32 delta)
{
	reader->data_offset += delta;
}

static inline u8 emulation_escape(struct bitstream_reader *reader, u32 offset,
				  u8 data, bool inc_offset, bool *escaped)
{
	u32 seq;

	if (data != 0x03 || !reader->rbsp_mode)
		return data;

	if (offset < 2 || offset == reader->bitstream_end)
		return data;

	seq = *((u32 *)(reader->data_ptr + offset - 2));
	seq = be32_to_cpu(seq);

	switch (seq) {
	case 0x00000300:
	case 0x00000301:
	case 0x00000302:
	case 0x00000303:
		if (inc_offset)
			reader->data_offset++;

		if (escaped)
			*escaped = true;

		return seq & 0xFF;
	default:
		break;
	}

	return data;
}

static inline u32 bitstream_read_bits(struct bitstream_reader *reader,
				      u8 bits_nb, bool inc_offset)
{
	u8 rshift, bytes_to_read = (bits_nb + reader->bit_shift - 1) / 8;
	u32 data_offset = reader->data_offset;
	bool escape_inc_offset = false;
	u64 ret = 0;

	if (inc_offset && check_range(reader, bytes_to_read))
		return 0;

	rshift = 8 * (bytes_to_read + 1) - (reader->bit_shift + bits_nb);

	do {
		u8 byte = *(reader->data_ptr + data_offset);
		u8 lshift = bytes_to_read * 8;
		bool escaped = false;

		byte = emulation_escape(reader, data_offset++, byte,
					!escape_inc_offset || inc_offset,
					&escaped);
		if (escaped)
			data_offset++;

		escape_inc_offset = true;

		ret |= (u64)byte << lshift;
	} while (bytes_to_read--);

	ret >>= rshift;
	ret &= (1llu << bits_nb) - 1;

	return ret;
}

static inline void
bitstream_reader_inc_offset_b(struct bitstream_reader *reader, u8 bits_nb)
{
	u8 bit_shift = reader->bit_shift;

	reader->data_offset += (bit_shift + bits_nb) / 8;
	reader->bit_shift = (bit_shift + bits_nb) % 8;
}

static inline u8 bitstream_read_u8_no_inc(struct bitstream_reader *reader)
{
	u8 ret;

	if (reader->error)
		return 0;

	if (check_range(reader, 0))
		return 0;

	ret = *(reader->data_ptr + reader->data_offset);

	return emulation_escape(reader, reader->data_offset, ret, true, NULL);
}

static inline u32 bitstream_read_u(struct bitstream_reader *reader, u8 bits_nb)
{
	u32 ret;

	if (reader->bit_shift == 0 && bits_nb == 8) {
		ret = bitstream_read_u8_no_inc(reader);
		bitstream_reader_inc_offset(reader, 1);
	} else {
		ret = bitstream_read_bits(reader, bits_nb, true);
		bitstream_reader_inc_offset_b(reader, bits_nb);
	}

	return ret;
}

static inline unsigned int
bitstream_skip_leading_zeros(struct bitstream_reader *reader)
{
	const u8 bit_shift = reader->bit_shift;
	u8 leading_zeros_align = 0;
	u8 leading_zeros = 0;

	if (bit_shift && !reader->error) {
		uint byte = bitstream_read_bits(reader, 8 - bit_shift, false);

		if (byte)
			leading_zeros_align = __builtin_clz(byte) - 24 - bit_shift;
		else
			leading_zeros_align = 8 - bit_shift;

		if (byte) {
			reader->bit_shift += leading_zeros_align;

			bitstream_reader_inc_offset_b(reader, 1);

			return leading_zeros_align;
		}

		bitstream_reader_inc_offset_b(reader, leading_zeros_align);
	}

	while (!reader->error) {
		uint byte = bitstream_read_u8_no_inc(reader);

		leading_zeros += byte ? __builtin_clz(byte) - 24 : 8;

		if (byte) {
			reader->bit_shift += leading_zeros % 8;
			bitstream_reader_inc_offset_b(reader, 1);
			leading_zeros += leading_zeros_align;

			return leading_zeros;
		}

		bitstream_reader_inc_offset(reader, 1);
	}

	return 0;
}

static inline u32 exp_golomb_codenum(unsigned int exp, u32 val)
{
	return (1lu << exp) - 1 + val;
}

static u32 bitstream_read_ue(struct bitstream_reader *reader)
{
	unsigned int leading_zeros;
	u32 val = 0;

	leading_zeros = bitstream_skip_leading_zeros(reader);

	if (leading_zeros > 31) {
		reader->error = 1;
		return 0;
	}

	if (leading_zeros)
		val = bitstream_read_u(reader, leading_zeros);

	return exp_golomb_codenum(leading_zeros, val);
}

static inline int bitstream_start_offset(const char *nal)
{
	if (nal[0] || nal[1])
		return -EINVAL;

	if (nal[2] == 1)
		return 4;

	if (nal[2] == 0 && nal[3] == 1)
		return 5;

	return -EINVAL;
}

int tegra_h264_parse_slice_type(const void *bitstream, size_t bitstream_size)
{
	struct bitstream_reader reader;
	unsigned int slice_type;
	u8 bitstream_data[8];
	int start_offset;

	/* assuming that bitstream data is uncached, copy it to CPU cache */
	bitstream_size = min(bitstream_size, sizeof(bitstream_data));
	memcpy(bitstream_data, bitstream, bitstream_size);

	start_offset = bitstream_start_offset(bitstream_data);
	if (start_offset < 0)
		return start_offset;

	if (start_offset >= bitstream_size)
		return -EINVAL;

	bitstream_init(&reader, bitstream_data, bitstream_size);
	bitstream_reader_inc_offset(&reader, start_offset);

	bitstream_read_ue(&reader);
	if (reader.error)
		return -EINVAL;

	slice_type = bitstream_read_ue(&reader);
	if (reader.error)
		return -EINVAL;

	return slice_type;
}
