/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright © 2019 Macronix
 * Author: Miquèl Raynal <miquel.raynal@bootlin.com>
 *
 * Header for the Macronix external ECC engine.
 */

#ifndef __MTD_NAND_ECC_MXIC_H__
#define __MTD_NAND_ECC_MXIC_H__

#include <linux/device.h>

struct mxic_ecc_engine;

#if IS_ENABLED(CONFIG_MTD_NAND_ECC_MXIC)

struct nand_ecc_engine_ops *mxic_ecc_get_pipelined_ops(void);
int mxic_ecc_process_data(struct mxic_ecc_engine *eng, dma_addr_t dirmap);

#else /* !CONFIG_MTD_NAND_ECC_MXIC */

struct nand_ecc_engine_ops *mxic_ecc_get_pipelined_ops(void)
{
	return NULL;
}

static inline
int mxic_ecc_process_data(struct mxic_ecc_engine *eng, dma_addr_t dirmap)
{
	return -ENOTSUPP;
}

#endif /* CONFIG_MTD_NAND_ECC_MXIC */

#endif /* __MTD_NAND_ECC_MXIC_H__ */
