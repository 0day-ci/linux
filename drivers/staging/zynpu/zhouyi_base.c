// SPDX-License-Identifier: GPL-2.0+
/*
*
* Zhouyi AI Accelerator driver
*
* Copyright (C) 2020 Arm (China) Ltd.
* Copyright (C) 2021 Cai Huoqing
*/

/**
 * @file zhouyi.c
 * Implementations of the zhouyi ZYNPU hardware control and interrupt handle operations
 */

#include "zhouyi.h"

int zhouyi_read_status_reg(struct io_region *io)
{
	return zynpu_read32(io, ZHOUYI_STAT_REG_OFFSET);
}

void zhouyi_clear_qempty_interrupt(struct io_region *io)
{
	zynpu_write32(io, ZHOUYI_STAT_REG_OFFSET, ZHOUYI_IRQ_QEMPTY);
}

void zhouyi_clear_done_interrupt(struct io_region *io)
{
	zynpu_write32(io, ZHOUYI_STAT_REG_OFFSET, ZHOUYI_IRQ_DONE);
}

void zhouyi_clear_excep_interrupt(struct io_region *io)
{
	zynpu_write32(io, ZHOUYI_STAT_REG_OFFSET, ZHOUYI_IRQ_EXCEP);
}

int zhouyi_query_cap(struct io_region *io, struct zynpu_cap *cap)
{
	int ret = 0;

	if ((!io) || (!cap)) {
		ret = -EINVAL;
		goto finish;
	}

	cap->isa_version = zynpu_read32(io, ZHOUYI_ISA_VERSION_REG_OFFSET);
	cap->tpc_feature = zynpu_read32(io, ZHOUYI_TPC_FEATURE_REG_OFFSET);
	cap->aiff_feature = zynpu_read32(io, ZHOUYI_HWA_FEATURE_REG_OFFSET);

	/* success */
	cap->errcode = 0;

finish:
	return ret;
}

void zhouyi_io_rw(struct io_region *io, struct zynpu_io_req *io_req)
{
	if ((!io) || (!io_req)) {
		pr_err("invalid input args io/io_req to be NULL!");
		return;
	}

	/* TBD: offset r/w permission should be checked */

	if (io_req->rw == ZYNPU_IO_READ)
	    io_req->value = zynpu_read32(io, io_req->offset);
	else if (io_req->rw == ZYNPU_IO_WRITE)
	    zynpu_write32(io, io_req->offset, io_req->value);
	io_req->errcode = 0;
}
