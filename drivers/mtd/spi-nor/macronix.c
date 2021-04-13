// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#include <linux/mtd/spi-nor.h>

#include "core.h"

/*
 * mx25u6435f/mx25u6432f common protection table:
 *
 * mx25u6432f has T/B bit, but mx25u6435f doesn't.
 * while both chips have the same JEDEC ID,
 * Also BP bits are slightly different with generic swp.
 * So here we only use common part of the BPs definitions.
 *
 * - Upper 2^(Prot Level - 1) blocks are protected.
 * - Block size is hardcoded as 64Kib.
 * - Assume T/B is always 0 (top protected, factory default).
 *
 *   BP3| BP2 | BP1 | BP0 | Prot Level
 *  -----------------------------------
 *    0 |  0  |  0  |  0  |  NONE
 *    0 |  0  |  0  |  1  |  1
 *    0 |  0  |  1  |  0  |  2
 *    0 |  0  |  1  |  1  |  3
 *    0 |  1  |  0  |  0  |  4
 *    0 |  1  |  0  |  1  |  5
 *    0 |  1  |  1  |  0  |  6
 *    0 |  1  |  1  |  1  |  7
 *   .....................|  differ by 35f/32f, not used
 *    1 |  1  |  1  |  1  |  ALL
 */

#define MX_BP_MASK	(SR_BP0 | SR_BP1 | SR_BP2 | SR_BP3)
#define MX_BP_SHIFT	(SR_BP_SHIFT)

static int mx_get_locked_len(struct spi_nor *nor, u8 sr, uint64_t *lock_len)
{
	struct mtd_info *mtd = &nor->mtd;
	u8 bp;

	bp = (sr & MX_BP_MASK) >> MX_BP_SHIFT;

	if (bp == 0xf) {
		/* protected all */
		*lock_len = mtd->size;
		return 0;
	}

	/* sorry, not yet supported */
	if (bp > 0x7)
		return -EOPNOTSUPP;

	/* block size = 64Kib */
	*lock_len = bp ? (0x8000 << bp) : 0;
	return 0;
}

static int mx_set_prot_level(struct spi_nor *nor, uint64_t lock_len, u8 *sr)
{
	uint64_t new_len;
	u8 new_lvl;

	if (lock_len) {
		/* 64Kib block size harcoded */
		new_lvl = ilog2(lock_len) - 15;
		new_len = 1ULL << (15 + new_lvl);

		if (new_len != lock_len)
			return -EINVAL;
	} else {
		new_lvl = 0;
	}

	*sr &= ~MX_BP_MASK;
	*sr |= (new_lvl) << MX_BP_SHIFT;

	return 0;
}

static int mx_lock(struct spi_nor *nor, loff_t ofs, uint64_t len)
{
	struct mtd_info *mtd = &nor->mtd;
	int ret;
	uint64_t lock_len;
	u8 sr;

	ret = spi_nor_read_sr(nor, nor->bouncebuf);
	if (ret)
		return ret;

	sr = nor->bouncebuf[0];

	/* always 'top' protection */
	if ((ofs + len) != mtd->size)
		return -EINVAL;

	ret = mx_get_locked_len(nor, sr, &lock_len);
	if (ret)
		return ret;

	/* already locked? */
	if (len <= lock_len)
		return 0;

	ret = mx_set_prot_level(nor, len, &sr);
	if (ret)
		return ret;

	/* Disallow further writes if WP pin is asserted */
	sr |= SR_SRWD;

	return spi_nor_write_sr_and_check(nor, sr);
}

static int mx_unlock(struct spi_nor *nor, loff_t ofs, uint64_t len)
{
	struct mtd_info *mtd = &nor->mtd;
	int ret;
	uint64_t lock_len;
	u8 sr;

	if ((ofs + len) > mtd->size)
		return -EINVAL;

	ret = spi_nor_read_sr(nor, nor->bouncebuf);
	if (ret)
		return ret;

	sr = nor->bouncebuf[0];

	ret = mx_get_locked_len(nor, sr, &lock_len);
	if (ret)
		return ret;

	/* already unlocked? */
	if ((ofs + len) <= (mtd->size - lock_len))
		return 0;

	/* can't make a hole in a locked region */
	if (ofs > (mtd->size - lock_len))
		return -EINVAL;

	lock_len = mtd->size - ofs - len;
	ret = mx_set_prot_level(nor, lock_len, &sr);
	if (ret)
		return ret;

	/* Don't protect status register if we're fully unlocked */
	if (lock_len == 0)
		sr &= ~SR_SRWD;

	return spi_nor_write_sr_and_check(nor, sr);
}

static int mx_is_locked(struct spi_nor *nor, loff_t ofs, uint64_t len)
{
	struct mtd_info *mtd = &nor->mtd;
	int ret;
	uint64_t lock_len;
	u8 sr;

	if ((ofs + len) > mtd->size)
		return -EINVAL;

	if (!len)
		return 0;

	ret = spi_nor_read_sr(nor, nor->bouncebuf);
	if (ret)
		return ret;

	sr = nor->bouncebuf[0];

	ret = mx_get_locked_len(nor, sr, &lock_len);
	if (ret)
		return ret;

	return (ofs >= (mtd->size - lock_len)) ? 1 : 0;
}

static const struct spi_nor_locking_ops mx_locking_ops = {
	.lock		= mx_lock,
	.unlock		= mx_unlock,
	.is_locked	= mx_is_locked,
};

static void mx_default_init(struct spi_nor *nor)
{
	nor->params->locking_ops = &mx_locking_ops;
}

static const struct spi_nor_fixups mx_locking_fixups = {
	.default_init = mx_default_init,
};

static int
mx25l25635_post_bfpt_fixups(struct spi_nor *nor,
			    const struct sfdp_parameter_header *bfpt_header,
			    const struct sfdp_bfpt *bfpt)
{
	/*
	 * MX25L25635F supports 4B opcodes but MX25L25635E does not.
	 * Unfortunately, Macronix has re-used the same JEDEC ID for both
	 * variants which prevents us from defining a new entry in the parts
	 * table.
	 * We need a way to differentiate MX25L25635E and MX25L25635F, and it
	 * seems that the F version advertises support for Fast Read 4-4-4 in
	 * its BFPT table.
	 */
	if (bfpt->dwords[BFPT_DWORD(5)] & BFPT_DWORD5_FAST_READ_4_4_4)
		nor->flags |= SNOR_F_4B_OPCODES;

	return 0;
}

static struct spi_nor_fixups mx25l25635_fixups = {
	.post_bfpt = mx25l25635_post_bfpt_fixups,
};

static const struct flash_info macronix_parts[] = {
	/* Macronix */
	{ "mx25l512e",   INFO(0xc22010, 0, 64 * 1024,   1, SECT_4K) },
	{ "mx25l2005a",  INFO(0xc22012, 0, 64 * 1024,   4, SECT_4K) },
	{ "mx25l4005a",  INFO(0xc22013, 0, 64 * 1024,   8, SECT_4K) },
	{ "mx25l8005",   INFO(0xc22014, 0, 64 * 1024,  16, 0) },
	{ "mx25l1606e",  INFO(0xc22015, 0, 64 * 1024,  32, SECT_4K) },
	{ "mx25l3205d",  INFO(0xc22016, 0, 64 * 1024,  64, SECT_4K) },
	{ "mx25l3255e",  INFO(0xc29e16, 0, 64 * 1024,  64, SECT_4K) },
	{ "mx25l6405d",  INFO(0xc22017, 0, 64 * 1024, 128, SECT_4K) },
	{ "mx25u2033e",  INFO(0xc22532, 0, 64 * 1024,   4, SECT_4K) },
	{ "mx25u3235f",	 INFO(0xc22536, 0, 64 * 1024,  64,
			      SECT_4K | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ) },
	{ "mx25u4035",   INFO(0xc22533, 0, 64 * 1024,   8, SECT_4K) },
	{ "mx25u8035",   INFO(0xc22534, 0, 64 * 1024,  16, SECT_4K) },
	{ "mx25u6435f",  INFO(0xc22537, 0, 64 * 1024, 128,
			      SECT_4K | SPI_NOR_HAS_LOCK)
		.fixups = &mx_locking_fixups },
	{ "mx25l12805d", INFO(0xc22018, 0, 64 * 1024, 256, SECT_4K) },
	{ "mx25l12855e", INFO(0xc22618, 0, 64 * 1024, 256, 0) },
	{ "mx25r1635f",  INFO(0xc22815, 0, 64 * 1024,  32,
			      SECT_4K | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ) },
	{ "mx25r3235f",  INFO(0xc22816, 0, 64 * 1024,  64,
			      SECT_4K | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ) },
	{ "mx25u12835f", INFO(0xc22538, 0, 64 * 1024, 256,
			      SECT_4K | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ) },
	{ "mx25l25635e", INFO(0xc22019, 0, 64 * 1024, 512,
			      SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ)
		.fixups = &mx25l25635_fixups },
	{ "mx25u25635f", INFO(0xc22539, 0, 64 * 1024, 512,
			      SECT_4K | SPI_NOR_4B_OPCODES) },
	{ "mx25u51245g", INFO(0xc2253a, 0, 64 * 1024, 1024,
			      SECT_4K | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ | SPI_NOR_4B_OPCODES) },
	{ "mx25v8035f",  INFO(0xc22314, 0, 64 * 1024,  16,
			      SECT_4K | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ) },
	{ "mx25l25655e", INFO(0xc22619, 0, 64 * 1024, 512, 0) },
	{ "mx66l51235l", INFO(0xc2201a, 0, 64 * 1024, 1024,
			      SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ |
			      SPI_NOR_4B_OPCODES) },
	{ "mx66u51235f", INFO(0xc2253a, 0, 64 * 1024, 1024,
			      SECT_4K | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ | SPI_NOR_4B_OPCODES) },
	{ "mx66l1g45g",  INFO(0xc2201b, 0, 64 * 1024, 2048,
			      SECT_4K | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ) },
	{ "mx66l1g55g",  INFO(0xc2261b, 0, 64 * 1024, 2048,
			      SPI_NOR_QUAD_READ) },
	{ "mx66u2g45g",	 INFO(0xc2253c, 0, 64 * 1024, 4096,
			      SECT_4K | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ | SPI_NOR_4B_OPCODES) },
};

static void macronix_default_init(struct spi_nor *nor)
{
	nor->params->quad_enable = spi_nor_sr1_bit6_quad_enable;
	nor->params->set_4byte_addr_mode = spi_nor_set_4byte_addr_mode;
}

static const struct spi_nor_fixups macronix_fixups = {
	.default_init = macronix_default_init,
};

const struct spi_nor_manufacturer spi_nor_macronix = {
	.name = "macronix",
	.parts = macronix_parts,
	.nparts = ARRAY_SIZE(macronix_parts),
	.fixups = &macronix_fixups,
};
