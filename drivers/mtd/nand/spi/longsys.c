// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Daniel Palmer <daniel@thingy.jp>
 *
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/mtd/spinand.h>

#define SPINAND_MFR_LONGSYS			0xCD
#define FS35ND01G_S1Y2_STATUS_ECC_0_3_BITFLIPS	(0 << 4)
#define FS35ND01G_S1Y2_STATUS_ECC_4_BITFLIPS	(1 << 4)
#define FS35ND01G_S1Y2_STATUS_ECC_UNCORRECTABLE	(2 << 4)

static SPINAND_OP_VARIANTS(read_cache_variants,
		SPINAND_PAGE_READ_FROM_CACHE_QUADIO_OP(0, 2, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_X4_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_DUALIO_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_X2_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_OP(false, 0, 1, NULL, 0));

static SPINAND_OP_VARIANTS(write_cache_variants,
		SPINAND_PROG_LOAD(true, 0, NULL, 0));

static SPINAND_OP_VARIANTS(update_cache_variants,
		SPINAND_PROG_LOAD(false, 0, NULL, 0));

static int fs35nd01g_s1y2_ooblayout_ecc(struct mtd_info *mtd, int section,
					struct mtd_oob_region *region)
{
	if (section > 3)
		return -ERANGE;

	/* ECC is not user accessible */
	region->offset = 0;
	region->length = 0;

	return 0;
}

static int fs35nd01g_s1y2_ooblayout_free(struct mtd_info *mtd, int section,
				    struct mtd_oob_region *region)
{
	if (section > 3)
		return -ERANGE;

	/*
	 * No ECC data is stored in the accessible OOB so the full 16 bytes
	 * of each spare region is available to the user. Apparently also
	 * covered by the internal ECC.
	 */
	if (section) {
		region->offset = 16 * section;
		region->length = 16;
	} else {
		/* First byte in spare0 area is used for bad block marker */
		region->offset = 1;
		region->length = 15;
	}

	return 0;
}

static const struct mtd_ooblayout_ops fs35nd01g_s1y2_ooblayout = {
	.ecc = fs35nd01g_s1y2_ooblayout_ecc,
	.free = fs35nd01g_s1y2_ooblayout_free,
};

static int fs35nd01g_s1y2_ecc_get_status(struct spinand_device *spinand,
					u8 status)
{
	switch (status & STATUS_ECC_MASK) {
	case FS35ND01G_S1Y2_STATUS_ECC_0_3_BITFLIPS:
		return 3;
	/*
	 * The datasheet says *successful* with 4 bits flipped.
	 * nandbiterrs always complains that the read reported
	 * successful but the data is incorrect.
	 */
	case FS35ND01G_S1Y2_STATUS_ECC_4_BITFLIPS:
		return 4;
	case FS35ND01G_S1Y2_STATUS_ECC_UNCORRECTABLE:
		return -EBADMSG;
	default:
		break;
	}

	return -EINVAL;
}

static const struct spinand_info longsys_spinand_table[] = {
	SPINAND_INFO("FS35ND01G-S1Y2",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xEA, 0x11),
		     NAND_MEMORG(1, 2048, 64, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(4, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&fs35nd01g_s1y2_ooblayout,
				     fs35nd01g_s1y2_ecc_get_status)),
	SPINAND_INFO("FS35ND02G-S3Y2",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xEB, 0x11),
		     NAND_MEMORG(1, 2048, 64, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(4, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&fs35nd01g_s1y2_ooblayout,
				     fs35nd01g_s1y2_ecc_get_status)),
	SPINAND_INFO("FS35ND04G-S2Y2",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0xEC, 0x11),
		     NAND_MEMORG(1, 2048, 64, 64, 4096, 40, 1, 1, 1),
		     NAND_ECCREQ(4, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(&fs35nd01g_s1y2_ooblayout,
				     fs35nd01g_s1y2_ecc_get_status)),
};

static const struct spinand_manufacturer_ops longsys_spinand_manuf_ops = {
};

const struct spinand_manufacturer longsys_spinand_manufacturer = {
	.id = SPINAND_MFR_LONGSYS,
	.name = "Longsys",
	.chips = longsys_spinand_table,
	.nchips = ARRAY_SIZE(longsys_spinand_table),
	.ops = &longsys_spinand_manuf_ops,
};
