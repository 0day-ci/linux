// SPDX-License-Identifier: GPL-2.0+
/*
 * EC MPI common functions.
 *
 * Copyright (c) 2020, Alibaba Group.
 * Authors: Tianjia Zhang <tianjia.zhang@linux.alibaba.com>
 */

#include <linux/module.h>
#include <linux/mpi.h>
#include "ec_mpi.h"

int ec_mpi_ctx_init(struct mpi_ec_ctx *ec, const struct ecc_domain_parms *ecp)
{
	MPI p, a, b;
	MPI x, y;
	int rc = -EINVAL;

	p = mpi_scanval(ecp->p);
	a = mpi_scanval(ecp->a);
	b = mpi_scanval(ecp->b);
	if (!p || !a || !b)
		goto free_p;

	x = mpi_scanval(ecp->g_x);
	y = mpi_scanval(ecp->g_y);
	if (!x || !y)
		goto free;

	rc = -ENOMEM;

	ec->Q = mpi_point_new(0);
	if (!ec->Q)
		goto free;

	/* mpi_ec_setup_elliptic_curve */
	ec->G = mpi_point_new(0);
	if (!ec->G) {
		mpi_point_release(ec->Q);
		goto free;
	}

	mpi_set(ec->G->x, x);
	mpi_set(ec->G->y, y);
	mpi_set_ui(ec->G->z, 1);

	rc = -EINVAL;
	ec->n = mpi_scanval(ecp->n);
	if (!ec->n) {
		mpi_point_release(ec->Q);
		mpi_point_release(ec->G);
		goto free;
	}

	ec->h = ecp->h;
	ec->name = ecp->desc;
	mpi_ec_init(ec, ecp->model, ecp->dialect, 0, p, a, b);

	rc = 0;

free:
	mpi_free(x);
	mpi_free(y);
free_p:
	mpi_free(p);
	mpi_free(a);
	mpi_free(b);

	return rc;
}
EXPORT_SYMBOL(ec_mpi_ctx_init);

void ec_mpi_ctx_deinit(struct mpi_ec_ctx *ec)
{
	mpi_ec_deinit(ec);

	memset(ec, 0, sizeof(*ec));
}
EXPORT_SYMBOL(ec_mpi_ctx_deinit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tianjia Zhang <tianjia.zhang@linux.alibaba.com>");
