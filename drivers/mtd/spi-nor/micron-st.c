// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#include <linux/mtd/spi-nor.h>

#include "core.h"

#define SPINOR_OP_MT_DTR_RD	0xfd	/* Fast Read opcode in DTR mode */
#define SPINOR_OP_MT_RD_ANY_REG	0x85	/* Read volatile register */
#define SPINOR_OP_MT_WR_ANY_REG	0x81	/* Write volatile register */
#define SPINOR_REG_MT_CFR0V	0x00	/* For setting octal DTR mode */
#define SPINOR_REG_MT_CFR1V	0x01	/* For setting dummy cycles */
#define SPINOR_MT_OCT_DTR	0xe7	/* Enable Octal DTR. */
#define SPINOR_MT_EXSPI		0xff	/* Enable Extended SPI (default) */

struct micron_drive_strength {
	u32 ohms;
	u8 val;
};

static int spi_nor_micron_octal_dtr_enable(struct spi_nor *nor, bool enable)
{
	struct spi_mem_op op;
	u8 *buf = nor->bouncebuf;
	int ret;

	if (enable) {
		/* Use 20 dummy cycles for memory array reads. */
		ret = spi_nor_write_enable(nor);
		if (ret)
			return ret;

		*buf = 20;
		op = (struct spi_mem_op)
			SPI_MEM_OP(SPI_MEM_OP_CMD(SPINOR_OP_MT_WR_ANY_REG, 1),
				   SPI_MEM_OP_ADDR(3, SPINOR_REG_MT_CFR1V, 1),
				   SPI_MEM_OP_NO_DUMMY,
				   SPI_MEM_OP_DATA_OUT(1, buf, 1));

		ret = spi_mem_exec_op(nor->spimem, &op);
		if (ret)
			return ret;

		ret = spi_nor_wait_till_ready(nor);
		if (ret)
			return ret;
	}

	ret = spi_nor_write_enable(nor);
	if (ret)
		return ret;

	if (enable)
		*buf = SPINOR_MT_OCT_DTR;
	else
		*buf = SPINOR_MT_EXSPI;

	op = (struct spi_mem_op)
		SPI_MEM_OP(SPI_MEM_OP_CMD(SPINOR_OP_MT_WR_ANY_REG, 1),
			   SPI_MEM_OP_ADDR(enable ? 3 : 4,
					   SPINOR_REG_MT_CFR0V, 1),
			   SPI_MEM_OP_NO_DUMMY,
			   SPI_MEM_OP_DATA_OUT(1, buf, 1));

	if (!enable)
		spi_nor_spimem_setup_op(nor, &op, SNOR_PROTO_8_8_8_DTR);

	ret = spi_mem_exec_op(nor->spimem, &op);
	if (ret)
		return ret;

	/* Read flash ID to make sure the switch was successful. */
	op = (struct spi_mem_op)
		SPI_MEM_OP(SPI_MEM_OP_CMD(SPINOR_OP_RDID, 1),
			   SPI_MEM_OP_NO_ADDR,
			   SPI_MEM_OP_DUMMY(enable ? 8 : 0, 1),
			   SPI_MEM_OP_DATA_IN(round_up(nor->info->id_len, 2),
					      buf, 1));

	if (enable)
		spi_nor_spimem_setup_op(nor, &op, SNOR_PROTO_8_8_8_DTR);

	ret = spi_mem_exec_op(nor->spimem, &op);
	if (ret)
		return ret;

	if (memcmp(buf, nor->info->id, nor->info->id_len))
		return -EINVAL;

	return 0;
}

static void mt35xu512aba_default_init(struct spi_nor *nor)
{
	nor->params->octal_dtr_enable = spi_nor_micron_octal_dtr_enable;
}

static void mt35xu512aba_post_sfdp_fixup(struct spi_nor *nor)
{
	/* Set the Fast Read settings. */
	nor->params->hwcaps.mask |= SNOR_HWCAPS_READ_8_8_8_DTR;
	spi_nor_set_read_settings(&nor->params->reads[SNOR_CMD_READ_8_8_8_DTR],
				  0, 20, SPINOR_OP_MT_DTR_RD,
				  SNOR_PROTO_8_8_8_DTR);

	nor->cmd_ext_type = SPI_NOR_EXT_REPEAT;
	nor->params->rdsr_dummy = 8;
	nor->params->rdsr_addr_nbytes = 0;

	/*
	 * The BFPT quad enable field is set to a reserved value so the quad
	 * enable function is ignored by spi_nor_parse_bfpt(). Make sure we
	 * disable it.
	 */
	nor->params->quad_enable = NULL;
}

static struct spi_nor_fixups mt35xu512aba_fixups = {
	.default_init = mt35xu512aba_default_init,
	.post_sfdp = mt35xu512aba_post_sfdp_fixup,
};

static const struct flash_info micron_parts[] = {
	{ "mt35xu512aba", INFO(0x2c5b1a, 0, 128 * 1024, 512,
			       SECT_4K | USE_FSR | SPI_NOR_OCTAL_READ |
			       SPI_NOR_4B_OPCODES | SPI_NOR_OCTAL_DTR_READ |
			       SPI_NOR_OCTAL_DTR_PP |
			       SPI_NOR_IO_MODE_EN_VOLATILE)
	  .fixups = &mt35xu512aba_fixups},
	{ "mt35xu02g", INFO(0x2c5b1c, 0, 128 * 1024, 2048,
			    SECT_4K | USE_FSR | SPI_NOR_OCTAL_READ |
			    SPI_NOR_4B_OPCODES) },
};

static const struct flash_info st_parts[] = {
	{ "n25q016a",	 INFO(0x20bb15, 0, 64 * 1024,   32,
			      SECT_4K | SPI_NOR_QUAD_READ) },
	{ "n25q032",	 INFO(0x20ba16, 0, 64 * 1024,   64,
			      SPI_NOR_QUAD_READ) },
	{ "n25q032a",	 INFO(0x20bb16, 0, 64 * 1024,   64,
			      SPI_NOR_QUAD_READ) },
	{ "n25q064",     INFO(0x20ba17, 0, 64 * 1024,  128,
			      SECT_4K | SPI_NOR_QUAD_READ) },
	{ "n25q064a",    INFO(0x20bb17, 0, 64 * 1024,  128,
			      SECT_4K | SPI_NOR_QUAD_READ) },
	{ "n25q128a11",  INFO(0x20bb18, 0, 64 * 1024,  256,
			      SECT_4K | USE_FSR | SPI_NOR_QUAD_READ |
			      SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB |
			      SPI_NOR_4BIT_BP | SPI_NOR_BP3_SR_BIT6) },
	{ "n25q128a13",  INFO(0x20ba18, 0, 64 * 1024,  256,
			      SECT_4K | USE_FSR | SPI_NOR_QUAD_READ) },
	{ "mt25ql256a",  INFO6(0x20ba19, 0x104400, 64 * 1024,  512,
			       SECT_4K | USE_FSR | SPI_NOR_DUAL_READ |
			       SPI_NOR_QUAD_READ | SPI_NOR_4B_OPCODES) },
	{ "n25q256a",    INFO(0x20ba19, 0, 64 * 1024,  512, SECT_4K |
			      USE_FSR | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ) },
	{ "mt25qu256a",  INFO6(0x20bb19, 0x104400, 64 * 1024,  512,
			       SECT_4K | USE_FSR | SPI_NOR_DUAL_READ |
			       SPI_NOR_QUAD_READ | SPI_NOR_4B_OPCODES) },
	{ "n25q256ax1",  INFO(0x20bb19, 0, 64 * 1024,  512,
			      SECT_4K | USE_FSR | SPI_NOR_QUAD_READ) },
	{ "mt25ql512a",  INFO6(0x20ba20, 0x104400, 64 * 1024, 1024,
			       SECT_4K | USE_FSR | SPI_NOR_DUAL_READ |
			       SPI_NOR_QUAD_READ | SPI_NOR_4B_OPCODES) },
	{ "n25q512ax3",  INFO(0x20ba20, 0, 64 * 1024, 1024,
			      SECT_4K | USE_FSR | SPI_NOR_QUAD_READ |
			      SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB |
			      SPI_NOR_4BIT_BP | SPI_NOR_BP3_SR_BIT6) },
	{ "mt25qu512a",  INFO6(0x20bb20, 0x104400, 64 * 1024, 1024,
			       SECT_4K | USE_FSR | SPI_NOR_DUAL_READ |
			       SPI_NOR_QUAD_READ | SPI_NOR_4B_OPCODES) },
	{ "n25q512a",    INFO(0x20bb20, 0, 64 * 1024, 1024,
			      SECT_4K | USE_FSR | SPI_NOR_QUAD_READ |
			      SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB |
			      SPI_NOR_4BIT_BP | SPI_NOR_BP3_SR_BIT6) },
	{ "n25q00",      INFO(0x20ba21, 0, 64 * 1024, 2048,
			      SECT_4K | USE_FSR | SPI_NOR_QUAD_READ |
			      SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB |
			      SPI_NOR_4BIT_BP | SPI_NOR_BP3_SR_BIT6 |
			      NO_CHIP_ERASE) },
	{ "n25q00a",     INFO(0x20bb21, 0, 64 * 1024, 2048,
			      SECT_4K | USE_FSR | SPI_NOR_QUAD_READ |
			      NO_CHIP_ERASE) },
	{ "mt25ql02g",   INFO(0x20ba22, 0, 64 * 1024, 4096,
			      SECT_4K | USE_FSR | SPI_NOR_QUAD_READ |
			      NO_CHIP_ERASE) },
	{ "mt25qu02g",   INFO(0x20bb22, 0, 64 * 1024, 4096,
			      SECT_4K | USE_FSR | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ | NO_CHIP_ERASE) },

	{ "m25p05",  INFO(0x202010,  0,  32 * 1024,   2, 0) },
	{ "m25p10",  INFO(0x202011,  0,  32 * 1024,   4, 0) },
	{ "m25p20",  INFO(0x202012,  0,  64 * 1024,   4, 0) },
	{ "m25p40",  INFO(0x202013,  0,  64 * 1024,   8, 0) },
	{ "m25p80",  INFO(0x202014,  0,  64 * 1024,  16, 0) },
	{ "m25p16",  INFO(0x202015,  0,  64 * 1024,  32, 0) },
	{ "m25p32",  INFO(0x202016,  0,  64 * 1024,  64, 0) },
	{ "m25p64",  INFO(0x202017,  0,  64 * 1024, 128, 0) },
	{ "m25p128", INFO(0x202018,  0, 256 * 1024,  64, 0) },

	{ "m25p05-nonjedec",  INFO(0, 0,  32 * 1024,   2, 0) },
	{ "m25p10-nonjedec",  INFO(0, 0,  32 * 1024,   4, 0) },
	{ "m25p20-nonjedec",  INFO(0, 0,  64 * 1024,   4, 0) },
	{ "m25p40-nonjedec",  INFO(0, 0,  64 * 1024,   8, 0) },
	{ "m25p80-nonjedec",  INFO(0, 0,  64 * 1024,  16, 0) },
	{ "m25p16-nonjedec",  INFO(0, 0,  64 * 1024,  32, 0) },
	{ "m25p32-nonjedec",  INFO(0, 0,  64 * 1024,  64, 0) },
	{ "m25p64-nonjedec",  INFO(0, 0,  64 * 1024, 128, 0) },
	{ "m25p128-nonjedec", INFO(0, 0, 256 * 1024,  64, 0) },

	{ "m45pe10", INFO(0x204011,  0, 64 * 1024,    2, 0) },
	{ "m45pe80", INFO(0x204014,  0, 64 * 1024,   16, 0) },
	{ "m45pe16", INFO(0x204015,  0, 64 * 1024,   32, 0) },

	{ "m25pe20", INFO(0x208012,  0, 64 * 1024,  4,       0) },
	{ "m25pe80", INFO(0x208014,  0, 64 * 1024, 16,       0) },
	{ "m25pe16", INFO(0x208015,  0, 64 * 1024, 32, SECT_4K) },

	{ "m25px16",    INFO(0x207115,  0, 64 * 1024, 32, SECT_4K) },
	{ "m25px32",    INFO(0x207116,  0, 64 * 1024, 64, SECT_4K) },
	{ "m25px32-s0", INFO(0x207316,  0, 64 * 1024, 64, SECT_4K) },
	{ "m25px32-s1", INFO(0x206316,  0, 64 * 1024, 64, SECT_4K) },
	{ "m25px64",    INFO(0x207117,  0, 64 * 1024, 128, 0) },
	{ "m25px80",    INFO(0x207114,  0, 64 * 1024, 16, 0) },
};

/**
 * st_micron_set_4byte_addr_mode() - Set 4-byte address mode for ST and Micron
 * flashes.
 * @nor:	pointer to 'struct spi_nor'.
 * @enable:	true to enter the 4-byte address mode, false to exit the 4-byte
 *		address mode.
 *
 * Return: 0 on success, -errno otherwise.
 */
static int st_micron_set_4byte_addr_mode(struct spi_nor *nor, bool enable)
{
	int ret;

	ret = spi_nor_write_enable(nor);
	if (ret)
		return ret;

	ret = spi_nor_set_4byte_addr_mode(nor, enable);
	if (ret)
		return ret;

	return spi_nor_write_disable(nor);
}

static void micron_st_default_init(struct spi_nor *nor)
{
	nor->flags |= SNOR_F_HAS_LOCK;
	nor->flags &= ~SNOR_F_HAS_16BIT_SR;
	nor->params->quad_enable = NULL;
	nor->params->set_4byte_addr_mode = st_micron_set_4byte_addr_mode;
}


/*
 * Read Micron enhanced volatile configuration register
 * Return negative if error occurred or configuration register value
 */
static int micron_read_evcr(struct spi_nor *nor)
{
	int ret;

	if (nor->spimem) {
		struct spi_mem_op op =
			SPI_MEM_OP(SPI_MEM_OP_CMD(SPINOR_OP_RD_EVCR, 1),
				   SPI_MEM_OP_NO_ADDR,
				   SPI_MEM_OP_NO_DUMMY,
				   SPI_MEM_OP_DATA_IN(1, nor->bouncebuf, 1));

		ret = spi_mem_exec_op(nor->spimem, &op);
	} else {
		ret = nor->controller_ops->read_reg(nor, SPINOR_OP_RD_EVCR, nor->bouncebuf, 1);
	}

	if (ret < 0) {
		dev_err(nor->dev, "error %d reading EVCR\n", ret);
		return ret;
	}

	return nor->bouncebuf[0];
}

/*
 * Write Micron enhanced volatile configuration register
 * Return negative if error occurred or configuration register value
 */
static int micron_write_evcr(struct spi_nor *nor, u8 evcr)
{
	nor->bouncebuf[0] = evcr;

	spi_nor_write_enable(nor);

	if (nor->spimem) {
		struct spi_mem_op op =
			SPI_MEM_OP(SPI_MEM_OP_CMD(SPINOR_OP_WD_EVCR, 1),
				   SPI_MEM_OP_NO_ADDR,
				   SPI_MEM_OP_NO_DUMMY,
				   SPI_MEM_OP_DATA_OUT(1, nor->bouncebuf, 1));

		return spi_mem_exec_op(nor->spimem, &op);
	}

	return nor->controller_ops->write_reg(nor, SPINOR_OP_WD_EVCR, nor->bouncebuf, 1);
}

/*
 * Supported values from Enahanced Volatile COnfiguration Register (Bits 2:0)
 */
static const struct micron_drive_strength drive_strength_data[] = {
	{ .ohms = 90, .val = 1 },
	{ .ohms = 45, .val = 3 },
	{ .ohms = 20, .val = 5 },
	{ .ohms = 30, .val = 7 },
};

static struct micron_drive_strength const *micron_st_find_drive_strength_entry(u32 ohms)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(drive_strength_data); i++) {
		if (ohms == drive_strength_data[i].ohms)
			return &drive_strength_data[i];
	}
	return NULL;
}

static void micron_st_post_sfdp(struct spi_nor *nor)
{
	struct device_node *np = spi_nor_get_flash_node(nor);
	u32 ohms;

	if (!np)
		return;

	if (!of_property_read_u32(np, "output-driver-strength", &ohms)) {
		struct micron_drive_strength const *entry =
			micron_st_find_drive_strength_entry(ohms);

		if (entry) {
			int evcrr = micron_read_evcr(nor);

			if (evcrr >= 0) {
				u8 evcr = (u8)(evcrr & 0xf8) | entry->val;

				micron_write_evcr(nor, evcr);
				dev_dbg(nor->dev, "%s: EVCR 0x%x\n", __func__,
					(u32)micron_read_evcr(nor));
			}
		} else {
			dev_warn(nor->dev,
				"Invalid output-driver-strength property specified: %u",
				ohms);
		}
	}
}

static const struct spi_nor_fixups micron_st_fixups = {
	.default_init = micron_st_default_init,
	.post_sfdp = micron_st_post_sfdp,
};

const struct spi_nor_manufacturer spi_nor_micron = {
	.name = "micron",
	.parts = micron_parts,
	.nparts = ARRAY_SIZE(micron_parts),
	.fixups = &micron_st_fixups,
};

const struct spi_nor_manufacturer spi_nor_st = {
	.name = "st",
	.parts = st_parts,
	.nparts = ARRAY_SIZE(st_parts),
	.fixups = &micron_st_fixups,
};
