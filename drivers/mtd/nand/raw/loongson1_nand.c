// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * NAND Flash Driver for Loongson 1 SoC
 *
 * Copyright (C) 2015-2021 Zhang, Keguang <keguang.zhang@gmail.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/iopoll.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/rawnand.h>
#include <linux/platform_device.h>
#include <linux/sizes.h>

#include <nand.h>

/* Loongson 1 NAND Register Definitions */
#define NAND_CMD		0x0
#define NAND_ADDR1		0x4
#define NAND_ADDR2		0x8
#define NAND_TIMING		0xc
#define NAND_IDL		0x10
#define NAND_IDH		0x14
#define NAND_STATUS		0x15
#define NAND_PARAM		0x18
#define NAND_OP_NUM		0x1c
#define NAND_CS_RDY		0x20

#define NAND_DMA_ADDR		0x40

/* NAND Command Register Bits */
#define OP_DONE			BIT(10)
#define OP_SPARE		BIT(9)
#define OP_MAIN			BIT(8)
#define CMD_STATUS		BIT(7)
#define CMD_RESET		BIT(6)
#define CMD_READID		BIT(5)
#define BLOCKS_ERASE		BIT(4)
#define CMD_ERASE		BIT(3)
#define CMD_WRITE		BIT(2)
#define CMD_READ		BIT(1)
#define CMD_VALID		BIT(0)

#define MAX_ADDR_CYC		5U
#define MAX_ID_SIZE		(NAND_STATUS - NAND_IDL)
#define SIZE_MASK		GENMASK(11, 8)

#define BITS_PER_WORD		32

/* macros for registers read/write */
#define nand_readl(nc, off)		\
	readl((nc)->reg_base + (off))

#define nand_writel(nc, off, val)	\
	writel((val), (nc)->reg_base + (off))

struct ls1x_nand_controller {
	void __iomem *reg_base;
	__le32 addr1_reg;
	__le32 addr2_reg;

	char *buf;
	unsigned int len;
	unsigned int rdy_timeout;

	/* DMA Engine stuff */
	struct dma_chan *dma_chan;
	dma_cookie_t dma_cookie;
	struct completion dma_complete;
};

struct ls1x_nand {
	struct device *dev;
	struct clk *clk;
	struct nand_chip chip;
	struct nand_controller controller;
	struct ls1x_nand_controller nc;
	struct plat_ls1x_nand *pdata;
};

static void ls1x_nand_dump_regs(struct nand_chip *chip)
{
	struct ls1x_nand *nand = nand_get_controller_data(chip);
	struct ls1x_nand_controller *nc = &nand->nc;

	print_hex_dump(KERN_INFO, "REG: ", DUMP_PREFIX_OFFSET, 16, 4,
		       nc->reg_base, 0x44, false);
}

static void ls1x_nand_dma_callback(void *data)
{
	struct ls1x_nand *nand = (struct ls1x_nand *)data;
	struct ls1x_nand_controller *nc = &nand->nc;
	enum dma_status status;

	status = dmaengine_tx_status(nc->dma_chan, nc->dma_cookie, NULL);
	if (likely(status == DMA_COMPLETE))
		dev_dbg(nand->dev, "DMA complete with cookie=%d\n",
			nc->dma_cookie);
	else
		dev_err(nand->dev, "DMA error with cookie=%d\n",
			nc->dma_cookie);

	complete(&nc->dma_complete);
}

static int ls1x_nand_dma_transfer(struct ls1x_nand *nand, bool is_write)
{
	struct ls1x_nand_controller *nc = &nand->nc;
	struct dma_chan *chan = nc->dma_chan;
	struct dma_async_tx_descriptor *desc;
	enum dma_data_direction data_dir =
	    is_write ? DMA_TO_DEVICE : DMA_FROM_DEVICE;
	enum dma_transfer_direction xfer_dir =
	    is_write ? DMA_MEM_TO_DEV : DMA_DEV_TO_MEM;
	dma_addr_t dma_addr;
	int ret;

	dma_addr = dma_map_single(chan->device->dev, nc->buf, nc->len,
				  data_dir);
	if (dma_mapping_error(chan->device->dev, dma_addr)) {
		dev_err(nand->dev, "failed to map DMA buffer!\n");
		return -ENXIO;
	}

	desc = dmaengine_prep_slave_single(chan, dma_addr, nc->len, xfer_dir,
					   DMA_PREP_INTERRUPT);
	if (!desc) {
		dev_err(nand->dev, "failed to prepare DMA descriptor!\n");
		ret = PTR_ERR(desc);
		goto err;
	}
	desc->callback = ls1x_nand_dma_callback;
	desc->callback_param = nand;

	nc->dma_cookie = dmaengine_submit(desc);
	ret = dma_submit_error(nc->dma_cookie);
	if (ret) {
		dev_err(nand->dev, "failed to submit DMA descriptor!\n");
		goto err;
	}

	dev_dbg(nand->dev, "issue DMA with cookie=%d\n", nc->dma_cookie);
	dma_async_issue_pending(chan);

	ret = wait_for_completion_timeout(&nc->dma_complete,
					  msecs_to_jiffies(nc->rdy_timeout));
	if (ret <= 0) {
		dev_err(nand->dev, "DMA timeout!\n");
		dmaengine_terminate_all(chan);
		ret = -EIO;
	}
	ret = 0;
err:
	dma_unmap_single(chan->device->dev, dma_addr, nc->len, data_dir);

	return ret;
}

static inline void ls1x_nand_parse_address(struct nand_chip *chip,
					   const u8 *addrs,
					   unsigned int naddrs, int cmd)
{
	struct ls1x_nand *nand = nand_get_controller_data(chip);
	struct ls1x_nand_controller *nc = &nand->nc;
#if defined(CONFIG_LOONGSON1_LS1B)
	unsigned int page_shift = chip->page_shift + 1;
#endif
	int i;

	nc->addr1_reg = 0;
	nc->addr2_reg = 0;
#if defined(CONFIG_LOONGSON1_LS1B)
	if (cmd == CMD_ERASE) {
		page_shift = chip->page_shift;

		for (i = 0; i < min(MAX_ADDR_CYC - 2, naddrs); i++)
			nc->addr1_reg |=
			    (u32)addrs[i] << (page_shift + BITS_PER_BYTE * i);
		if (i == MAX_ADDR_CYC - 2)
			nc->addr2_reg |=
			    (u32)addrs[i] >> (BITS_PER_WORD - page_shift -
					      BITS_PER_BYTE * (i - 1));

		return;
	}

	for (i = 0; i < min(2U, naddrs); i++)
		nc->addr1_reg |= (u32)addrs[i] << BITS_PER_BYTE * i;
	for (i = 2; i < min(MAX_ADDR_CYC, naddrs); i++)
		nc->addr1_reg |=
		    (u32)addrs[i] << (page_shift + BITS_PER_BYTE * (i - 2));
	if (i == MAX_ADDR_CYC)
		nc->addr2_reg |=
		    (u32)addrs[i] >> (BITS_PER_WORD - page_shift -
				      BITS_PER_BYTE * (i - 1));
#elif defined(CONFIG_LOONGSON1_LS1C)
	if (cmd == CMD_ERASE) {
		for (i = 0; i < min(MAX_ADDR_CYC, naddrs); i++)
			nc->addr2_reg |= (u32)addrs[i] << BITS_PER_BYTE * i;

		return;
	}

	for (i = 0; i < min(MAX_ADDR_CYC, naddrs); i++) {
		if (i < 2)
			nc->addr1_reg |= (u32)addrs[i] << BITS_PER_BYTE * i;
		else
			nc->addr2_reg |=
			    (u32)addrs[i] << BITS_PER_BYTE * (i - 2);
	}
#endif
}

static int ls1x_nand_set_controller(struct nand_chip *chip,
				    const struct nand_subop *subop, int cmd)
{
	struct ls1x_nand *nand = nand_get_controller_data(chip);
	struct ls1x_nand_controller *nc = &nand->nc;
	unsigned int op_id;

	nc->buf = NULL;
	nc->len = 0;
	nc->rdy_timeout = 0;

	for (op_id = 0; op_id < subop->ninstrs; op_id++) {
		const struct nand_op_instr *instr = &subop->instrs[op_id];
		unsigned int offset, naddrs;
		const u8 *addrs;

		switch (instr->type) {
		case NAND_OP_CMD_INSTR:
			break;
		case NAND_OP_ADDR_INSTR:
			offset = nand_subop_get_addr_start_off(subop, op_id);
			naddrs = nand_subop_get_num_addr_cyc(subop, op_id);
			addrs = &instr->ctx.addr.addrs[offset];

			ls1x_nand_parse_address(chip, addrs, naddrs, cmd);
			/* set NAND address */
			nand_writel(nc, NAND_ADDR1, nc->addr1_reg);
			nand_writel(nc, NAND_ADDR2, nc->addr2_reg);
			break;
		case NAND_OP_DATA_IN_INSTR:
			offset = nand_subop_get_data_start_off(subop, op_id);
			nc->len = nand_subop_get_data_len(subop, op_id);
			nc->buf = instr->ctx.data.buf.in + offset;

			if (!IS_ALIGNED(nc->len, chip->buf_align) ||
			    !IS_ALIGNED((unsigned int)nc->buf, chip->buf_align))
				return -ENOTSUPP;
			/* set NAND data length */
			nand_writel(nc, NAND_OP_NUM, nc->len);
			break;
		case NAND_OP_DATA_OUT_INSTR:
			offset = nand_subop_get_data_start_off(subop, op_id);
			nc->len = nand_subop_get_data_len(subop, op_id);
			nc->buf = (void *)instr->ctx.data.buf.out + offset;

			if (!IS_ALIGNED(nc->len, chip->buf_align) ||
			    !IS_ALIGNED((unsigned int)nc->buf, chip->buf_align))
				return -ENOTSUPP;
			/* set NAND data length */
			nand_writel(nc, NAND_OP_NUM, nc->len);
			break;
		case NAND_OP_WAITRDY_INSTR:
			nc->rdy_timeout = instr->ctx.waitrdy.timeout_ms;
			break;
		}
	}

	/*set NAND erase block count */
	if (cmd & CMD_ERASE)
		nand_writel(nc, NAND_OP_NUM, 1);
	/*set NAND operation region */
	if (nc->buf && nc->len) {
		if (nc->addr1_reg & BIT(chip->page_shift))
			cmd |= OP_SPARE;
		else
			cmd |= OP_SPARE | OP_MAIN;
	}

	/*set NAND command */
	nand_writel(nc, NAND_CMD, cmd);
	/* Trigger operation */
	nand_writel(nc, NAND_CMD, nand_readl(nc, NAND_CMD) | CMD_VALID);

	return 0;
}

static inline int ls1x_nand_wait_for_op_done(struct ls1x_nand_controller *nc)
{
	unsigned int val;
	int ret = 0;

	/* Wait for operation done */
	if (nc->rdy_timeout)
		ret = readl_relaxed_poll_timeout(nc->reg_base + NAND_CMD, val,
						 val & OP_DONE, 0,
						 nc->rdy_timeout * 1000);

	return ret;
}

static int ls1x_nand_reset_exec(struct nand_chip *chip,
				const struct nand_subop *subop)
{
	struct ls1x_nand *nand = nand_get_controller_data(chip);
	struct ls1x_nand_controller *nc = &nand->nc;
	int ret;

	ls1x_nand_set_controller(chip, subop, CMD_RESET);

	ret = ls1x_nand_wait_for_op_done(nc);
	if (ret)
		dev_err(nand->dev, "CMD_RESET failed! %d\n", ret);

	return ret;
}

static int ls1x_nand_read_id_exec(struct nand_chip *chip,
				  const struct nand_subop *subop)
{
	struct ls1x_nand *nand = nand_get_controller_data(chip);
	struct ls1x_nand_controller *nc = &nand->nc;
	int idl, i;
	int ret;

	ls1x_nand_set_controller(chip, subop, CMD_READID);

	ret = ls1x_nand_wait_for_op_done(nc);
	if (ret) {
		dev_err(nand->dev, "CMD_READID failed! %d\n", ret);
		return ret;
	}

	idl = (nand_readl(nc, NAND_IDL));
	for (i = 0; i < min_t(unsigned int, nc->len, MAX_ID_SIZE); i++)
		if (i > 0)
			nc->buf[i] = *((char *)&idl + 4 - i);
		else
			nc->buf[i] = (char)(nand_readl(nc, NAND_IDH));

	return ret;
}

static int ls1x_nand_erase_exec(struct nand_chip *chip,
				const struct nand_subop *subop)
{
	struct ls1x_nand *nand = nand_get_controller_data(chip);
	struct ls1x_nand_controller *nc = &nand->nc;
	int ret;

	ls1x_nand_set_controller(chip, subop, CMD_ERASE);

	ret = ls1x_nand_wait_for_op_done(nc);
	if (ret)
		dev_err(nand->dev, "CMD_ERASE failed! %d\n", ret);

	return ret;
}

static int ls1x_nand_read_exec(struct nand_chip *chip,
			       const struct nand_subop *subop)
{
	struct ls1x_nand *nand = nand_get_controller_data(chip);
	struct ls1x_nand_controller *nc = &nand->nc;
	int ret;

	ls1x_nand_set_controller(chip, subop, CMD_READ);

	ret = ls1x_nand_dma_transfer(nand, false);
	if (ret)
		return ret;

	ret = ls1x_nand_wait_for_op_done(nc);
	if (ret)
		dev_err(nand->dev, "CMD_READ failed! %d\n", ret);

	return ret;
}

static int ls1x_nand_write_exec(struct nand_chip *chip,
				const struct nand_subop *subop)
{
	struct ls1x_nand *nand = nand_get_controller_data(chip);
	struct ls1x_nand_controller *nc = &nand->nc;
	int ret;

	ls1x_nand_set_controller(chip, subop, CMD_WRITE);

	ret = ls1x_nand_dma_transfer(nand, true);
	if (ret)
		return ret;

	ret = ls1x_nand_wait_for_op_done(nc);
	if (ret)
		dev_err(nand->dev, "CMD_WRITE failed! %d\n", ret);

	return ret;
}

static int ls1x_nand_read_status_exec(struct nand_chip *chip,
				      const struct nand_subop *subop)
{
	struct ls1x_nand *nand = nand_get_controller_data(chip);
	struct ls1x_nand_controller *nc = &nand->nc;
	int ret;

	ls1x_nand_set_controller(chip, subop, CMD_STATUS);

	ret = ls1x_nand_wait_for_op_done(nc);
	if (ret) {
		dev_err(nand->dev, "CMD_STATUS failed! %d\n", ret);
		return ret;
	}

	nc->buf[0] = nand_readl(nc, NAND_IDH) >> BITS_PER_BYTE;

	return ret;
}

static const struct nand_op_parser ls1x_nand_op_parser = NAND_OP_PARSER(
	NAND_OP_PARSER_PATTERN(
		ls1x_nand_reset_exec,
		NAND_OP_PARSER_PAT_CMD_ELEM(false),
		NAND_OP_PARSER_PAT_WAITRDY_ELEM(false)),
	NAND_OP_PARSER_PATTERN(
		ls1x_nand_read_id_exec,
		NAND_OP_PARSER_PAT_CMD_ELEM(false),
		NAND_OP_PARSER_PAT_ADDR_ELEM(false, MAX_ADDR_CYC),
		NAND_OP_PARSER_PAT_DATA_IN_ELEM(false, 8)),
	NAND_OP_PARSER_PATTERN(
		ls1x_nand_erase_exec,
		NAND_OP_PARSER_PAT_CMD_ELEM(false),
		NAND_OP_PARSER_PAT_ADDR_ELEM(false, MAX_ADDR_CYC),
		NAND_OP_PARSER_PAT_CMD_ELEM(false),
		NAND_OP_PARSER_PAT_WAITRDY_ELEM(false)),
	NAND_OP_PARSER_PATTERN(
		ls1x_nand_read_exec,
		NAND_OP_PARSER_PAT_CMD_ELEM(false),
		NAND_OP_PARSER_PAT_ADDR_ELEM(false, MAX_ADDR_CYC),
		NAND_OP_PARSER_PAT_CMD_ELEM(false),
		NAND_OP_PARSER_PAT_WAITRDY_ELEM(true),
		NAND_OP_PARSER_PAT_DATA_IN_ELEM(false, 0)),
	NAND_OP_PARSER_PATTERN(
		ls1x_nand_write_exec,
		NAND_OP_PARSER_PAT_CMD_ELEM(false),
		NAND_OP_PARSER_PAT_ADDR_ELEM(false, MAX_ADDR_CYC),
		NAND_OP_PARSER_PAT_DATA_OUT_ELEM(false, 0),
		NAND_OP_PARSER_PAT_CMD_ELEM(false),
		NAND_OP_PARSER_PAT_WAITRDY_ELEM(true)),
	NAND_OP_PARSER_PATTERN(
		ls1x_nand_read_status_exec,
		NAND_OP_PARSER_PAT_CMD_ELEM(false),
		NAND_OP_PARSER_PAT_DATA_IN_ELEM(false, 1)),
	);

static int ls1x_nand_exec_op(struct nand_chip *chip,
			     const struct nand_operation *op, bool check_only)
{
	return nand_op_parser_exec_op(chip, &ls1x_nand_op_parser, op,
				      check_only);
}

static int ls1x_nand_read_subpage(struct nand_chip *chip,
				  unsigned int data_offs, unsigned int readlen,
				  unsigned char *bufpoi, int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	int start_step, end_step, num_steps, ret;
	char *p;
	int data_col_addr, i;
	int datafrag_len, eccfrag_len, aligned_len, aligned_pos;
	int busw = (chip->options & NAND_BUSWIDTH_16) ? 2 : 1;
	int index, section = 0;
	unsigned int max_bitflips = 0;
	struct mtd_oob_region oobregion = { };

	/* Read the whole page and OOB data */
	ret = chip->ecc.read_page_raw(chip, bufpoi, 1, page);
	if (ret)
		return ret;

	/* Column address within the page aligned to ECC size (256bytes) */
	start_step = data_offs / chip->ecc.size;
	end_step = (data_offs + readlen - 1) / chip->ecc.size;
	num_steps = end_step - start_step + 1;
	index = start_step * chip->ecc.bytes;

	/* Data size aligned to ECC ecc.size */
	datafrag_len = num_steps * chip->ecc.size;
	eccfrag_len = num_steps * chip->ecc.bytes;

	data_col_addr = start_step * chip->ecc.size;
	/* If we read not a page aligned data */
	p = bufpoi + data_col_addr;

	/* Calculate ECC */
	for (i = 0; i < eccfrag_len; i += chip->ecc.bytes, p += chip->ecc.size)
		chip->ecc.calculate(chip, p, &chip->ecc.calc_buf[i]);

	ret = mtd_ooblayout_find_eccregion(mtd, index, &section, &oobregion);
	if (ret)
		return ret;

	aligned_pos = oobregion.offset & ~(busw - 1);
	aligned_len = eccfrag_len;
	if (oobregion.offset & (busw - 1))
		aligned_len++;
	if ((oobregion.offset + (num_steps * chip->ecc.bytes)) & (busw - 1))
		aligned_len++;

	memcpy(&chip->oob_poi[aligned_pos],
	       bufpoi + mtd->writesize + aligned_pos, aligned_len);

	ret = mtd_ooblayout_get_eccbytes(mtd, chip->ecc.code_buf,
					 chip->oob_poi, index, eccfrag_len);
	if (ret)
		return ret;

	p = bufpoi + data_col_addr;
	for (i = 0; i < eccfrag_len; i += chip->ecc.bytes, p += chip->ecc.size) {
		int stat;

		stat = chip->ecc.correct(chip, p, &chip->ecc.code_buf[i],
					 &chip->ecc.calc_buf[i]);
		if (stat)
			ls1x_nand_dump_regs(chip);

		if (stat == -EBADMSG &&
		    (chip->ecc.options & NAND_ECC_GENERIC_ERASED_CHECK)) {
			/* check for empty pages with bitflips */
			stat = nand_check_erased_ecc_chunk(p, chip->ecc.size,
							&chip->ecc.code_buf[i],
							chip->ecc.bytes,
							NULL, 0,
							chip->ecc.strength);
		}

		if (stat < 0) {
			mtd->ecc_stats.failed++;
		} else {
			mtd->ecc_stats.corrected += stat;
			max_bitflips = max_t(unsigned int, max_bitflips, stat);
		}
	}
	return max_bitflips;
}

static int ls1x_nand_attach_chip(struct nand_chip *chip)
{
	struct ls1x_nand *nand = nand_get_controller_data(chip);
	struct ls1x_nand_controller *nc = &nand->nc;
	struct plat_ls1x_nand *pdata = nand->pdata;
	int hold_cycle = pdata->hold_cycle;
	int wait_cycle = pdata->wait_cycle;
	u64 chipsize = nanddev_target_size(&chip->base);
	int cell_size = 0;

	switch (chipsize) {
	case SZ_128M:
		cell_size = 0x0;
		break;
	case SZ_256M:
		cell_size = 0x1;
		break;
	case SZ_512M:
		cell_size = 0x2;
		break;
	case SZ_1G:
		cell_size = 0x3;
		break;
	case SZ_2G:
		cell_size = 0x4;
		break;
	case SZ_4G:
		cell_size = 0x5;
		break;
	case (SZ_2G * SZ_4G):	/*8G */
		cell_size = 0x6;
		break;
	case (SZ_4G * SZ_4G):	/*16G */
		cell_size = 0x7;
		break;
	default:
		dev_err(nand->dev, "unsupported chip size: %llu MB\n",
			chipsize);
		break;
	}

	if (hold_cycle && wait_cycle)
		nand_writel(nc, NAND_TIMING,
			    (hold_cycle << BITS_PER_BYTE) | wait_cycle);
	nand_writel(nc, NAND_PARAM,
		    (nand_readl(nc, NAND_PARAM) & ~SIZE_MASK) | cell_size <<
		    BITS_PER_BYTE);

	chip->ecc.read_page_raw = nand_monolithic_read_page_raw;
	chip->ecc.write_page_raw = nand_monolithic_write_page_raw;

	return 0;
}

static const struct nand_controller_ops ls1x_nc_ops = {
	.exec_op = ls1x_nand_exec_op,
	.attach_chip = ls1x_nand_attach_chip,
};

static void ls1x_nand_controller_cleanup(struct ls1x_nand *nand)
{
	if (nand->nc.dma_chan)
		dma_release_channel(nand->nc.dma_chan);
}

static int ls1x_nand_controller_init(struct ls1x_nand *nand,
				     struct platform_device *pdev)
{
	struct ls1x_nand_controller *nc = &nand->nc;
	struct device *dev = &pdev->dev;
	struct dma_slave_config cfg;
	struct resource *res;
	int ret;

	nc->reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(nc->reg_base))
		return PTR_ERR(nc->reg_base);

	res = platform_get_resource(pdev, IORESOURCE_DMA, 0);
	if (!res) {
		dev_err(dev, "failed to get DMA information!\n");
		return -ENXIO;
	}

	nc->dma_chan = dma_request_chan(dev, res->name);
	if (!nc->dma_chan) {
		dev_err(dev, "failed to request DMA channel!\n");
		return -EBUSY;
	}
	dev_info(dev, "got %s for %s access\n",
		 dma_chan_name(nc->dma_chan), dev_name(dev));

	cfg.src_addr = CPHYSADDR(nc->reg_base + NAND_DMA_ADDR);
	cfg.dst_addr = CPHYSADDR(nc->reg_base + NAND_DMA_ADDR);
	cfg.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	cfg.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	ret = dmaengine_slave_config(nc->dma_chan, &cfg);
	if (ret) {
		dev_err(dev, "failed to config DMA channel!\n");
		dma_release_channel(nc->dma_chan);
		return ret;
	}

	init_completion(&nc->dma_complete);

	return 0;
}

static int ls1x_nand_chip_init(struct ls1x_nand *nand)
{
	struct nand_chip *chip = &nand->chip;
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct plat_ls1x_nand *pdata = nand->pdata;
	int ret = 0;

	chip->controller = &nand->controller;
	chip->options = NAND_NO_SUBPAGE_WRITE | NAND_USES_DMA | NAND_BROKEN_XD;
	chip->buf_align = 16;
	chip->ecc.engine_type = NAND_ECC_ENGINE_TYPE_SOFT;
	chip->ecc.algo = NAND_ECC_ALGO_HAMMING;
	nand_set_controller_data(chip, nand);

	mtd->dev.parent = nand->dev;
	mtd->name = "ls1x-nand";
	mtd->owner = THIS_MODULE;

	ret = nand_scan(chip, 1);
	if (ret)
		return ret;

	chip->ecc.read_subpage = ls1x_nand_read_subpage;

	ret = mtd_device_register(mtd, pdata->parts, pdata->nr_parts);
	if (ret) {
		dev_err(nand->dev, "failed to register MTD device! %d\n", ret);
		nand_cleanup(chip);
	}

	return ret;
}

static int ls1x_nand_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct plat_ls1x_nand *pdata;
	struct ls1x_nand *nand;
	int ret;

	pdata = dev_get_platdata(dev);
	if (!pdata) {
		dev_err(dev, "platform data missing!\n");
		return -EINVAL;
	}

	nand = devm_kzalloc(dev, sizeof(*nand), GFP_KERNEL);
	if (!nand)
		return -ENOMEM;

	nand->pdata = pdata;
	nand->dev = dev;
	nand->controller.ops = &ls1x_nc_ops;
	nand_controller_init(&nand->controller);

	ret = ls1x_nand_controller_init(nand, pdev);
	if (ret)
		return ret;

	nand->clk = devm_clk_get(dev, pdev->name);
	if (IS_ERR(nand->clk)) {
		dev_err(dev, "failed to get %s clock!\n", pdev->name);
		return PTR_ERR(nand->clk);
	}
	clk_prepare_enable(nand->clk);

	ret = ls1x_nand_chip_init(nand);
	if (ret) {
		clk_disable_unprepare(nand->clk);
		goto err;
	}

	platform_set_drvdata(pdev, nand);
	dev_info(dev, "Loongson1 NAND driver registered\n");

	return 0;
err:
	ls1x_nand_controller_cleanup(nand);
	return ret;
}

static int ls1x_nand_remove(struct platform_device *pdev)
{
	struct ls1x_nand *nand = platform_get_drvdata(pdev);
	struct nand_chip *chip = &nand->chip;

	mtd_device_unregister(nand_to_mtd(chip));
	nand_cleanup(chip);
	clk_disable_unprepare(nand->clk);
	ls1x_nand_controller_cleanup(nand);

	return 0;
}

static struct platform_driver ls1x_nand_driver = {
	.probe	= ls1x_nand_probe,
	.remove	= ls1x_nand_remove,
	.driver	= {
		.name	= "ls1x-nand",
		.owner	= THIS_MODULE,
	},
};

module_platform_driver(ls1x_nand_driver);

MODULE_AUTHOR("Kelvin Cheung <keguang.zhang@gmail.com>");
MODULE_DESCRIPTION("Loongson1 NAND Flash driver");
MODULE_LICENSE("GPL");
