// SPDX-License-Identifier: GPL-2.0
/*
 * SPI NOR Advanced Sector Protection and Security Features
 *
 * Copyright (C) 2021 Micron Technology, Inc.
 */

#include <linux/mtd/mtd.h>
#include <linux/mtd/spi-nor.h>

#include "core.h"

static int spi_nor_secure_read(struct mtd_info *mtd, size_t len, u8 *buf)
{
	struct spi_nor *nor = mtd_to_spi_nor(mtd);
	int ret;

	ret = spi_nor_lock_and_prep(nor);
	if (ret)
		return ret;

	ret = nor->params->sec_ops->secure_read(nor, len, buf);

	spi_nor_unlock_and_unprep(nor);
	return ret;
}

static int spi_nor_secure_write(struct mtd_info *mtd, size_t len, u8 *buf)
{
	struct spi_nor *nor = mtd_to_spi_nor(mtd);
	int ret;

	ret = spi_nor_lock_and_prep(nor);
	if (ret)
		return ret;

	ret = nor->params->sec_ops->secure_write(nor, len, buf);

	spi_nor_unlock_and_unprep(nor);
	return ret;
}

static int spi_nor_read_vlock_bits(struct mtd_info *mtd, u32 addr, size_t len,
				   u8 *buf)
{
	struct spi_nor *nor = mtd_to_spi_nor(mtd);
	int ret;

	ret = spi_nor_lock_and_prep(nor);
	if (ret)
		return ret;

	ret = nor->params->sec_ops->read_vlock_bits(nor, addr, len, buf);

	spi_nor_unlock_and_unprep(nor);
	return ret;
}

static int spi_nor_write_vlock_bits(struct mtd_info *mtd, u32 addr, size_t len,
				    u8 *buf)
{
	struct spi_nor *nor = mtd_to_spi_nor(mtd);
	int ret;

	ret = spi_nor_lock_and_prep(nor);
	if (ret)
		return ret;

	ret = spi_nor_write_enable(nor);
	if (ret)
		return ret;

	ret = nor->params->sec_ops->write_vlock_bits(nor, addr, len, buf);
	if (ret)
		return ret;

	ret = spi_nor_write_disable(nor);

	spi_nor_unlock_and_unprep(nor);
	return ret;
}

static int spi_nor_read_nvlock_bits(struct mtd_info *mtd, u32 addr, size_t len,
				    u8 *buf)
{
	struct spi_nor *nor = mtd_to_spi_nor(mtd);
	int ret;

	ret = spi_nor_lock_and_prep(nor);
	if (ret)
		return ret;

	ret = nor->params->sec_ops->read_nvlock_bits(nor, addr, len, buf);

	spi_nor_unlock_and_unprep(nor);
	return ret;
}

static int spi_nor_write_nvlock_bits(struct mtd_info *mtd, u32 addr)
{
	struct spi_nor *nor = mtd_to_spi_nor(mtd);
	int ret;

	ret = spi_nor_lock_and_prep(nor);
	if (ret)
		return ret;

	ret = spi_nor_write_enable(nor);
	if (ret)
		return ret;

	ret = nor->params->sec_ops->write_nvlock_bits(nor, addr);
	if (ret)
		return ret;

	ret = spi_nor_write_disable(nor);

	spi_nor_unlock_and_unprep(nor);
	return ret;
}

static int spi_nor_erase_nvlock_bits(struct mtd_info *mtd)
{
	struct spi_nor *nor = mtd_to_spi_nor(mtd);
	int ret;

	ret = spi_nor_lock_and_prep(nor);
	if (ret)
		return ret;

	ret = spi_nor_write_enable(nor);
	if (ret)
		return ret;

	ret = nor->params->sec_ops->erase_nvlock_bits(nor);
	if (ret)
		return ret;

	ret = spi_nor_write_disable(nor);

	spi_nor_unlock_and_unprep(nor);
	return ret;
}

static int spi_nor_read_global_freeze_bits(struct mtd_info *mtd, size_t len,
					   u8 *buf)
{
	struct spi_nor *nor = mtd_to_spi_nor(mtd);
	int ret;

	ret = spi_nor_lock_and_prep(nor);
	if (ret)
		return ret;

	ret = nor->params->sec_ops->read_global_freeze_bits(nor, len, buf);

	spi_nor_unlock_and_unprep(nor);
	return ret;
}

static int spi_nor_write_global_freeze_bits(struct mtd_info *mtd, size_t len,
					    u8 *buf)
{
	struct spi_nor *nor = mtd_to_spi_nor(mtd);
	int ret;

	ret = spi_nor_lock_and_prep(nor);
	if (ret)
		return ret;

	ret = nor->params->sec_ops->write_global_freeze_bits(nor, len, buf);

	spi_nor_unlock_and_unprep(nor);
	return ret;
}

static int spi_nor_read_password(struct mtd_info *mtd, size_t len, u8 *buf)
{
	struct spi_nor *nor = mtd_to_spi_nor(mtd);
	int ret;

	ret = spi_nor_lock_and_prep(nor);
	if (ret)
		return ret;

	ret = nor->params->sec_ops->read_password(nor, len, buf);

	spi_nor_unlock_and_unprep(nor);
	return ret;
}

void spi_nor_register_security_ops(struct spi_nor *nor)
{
	struct mtd_info *mtd = &nor->mtd;

	if (!nor->params->sec_ops)
		return;

	mtd->_secure_packet_read = spi_nor_secure_read;
	mtd->_secure_packet_write = spi_nor_secure_write;
	mtd->_read_vlock_bits = spi_nor_read_vlock_bits;
	mtd->_write_vlock_bits = spi_nor_write_vlock_bits;
	mtd->_read_nvlock_bits = spi_nor_read_nvlock_bits;
	mtd->_write_nvlock_bits = spi_nor_write_nvlock_bits;
	mtd->_erase_nvlock_bits = spi_nor_erase_nvlock_bits;
	mtd->_read_global_freeze_bits = spi_nor_read_global_freeze_bits;
	mtd->_write_global_freeze_bits = spi_nor_write_global_freeze_bits;
	mtd->_read_password = spi_nor_read_password;
}
