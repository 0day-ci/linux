/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * EC MPI common structs.
 *
 * Copyright (c) 2020, Alibaba Group.
 * Authors: Tianjia Zhang <tianjia.zhang@linux.alibaba.com>
 */

#include <linux/mpi.h>

struct ecc_domain_parms {
	const char *desc;           /* Description of the curve.  */
	unsigned int nbits;         /* Number of bits.  */
	unsigned int fips:1; /* True if this is a FIPS140-2 approved curve */

	/* The model describing this curve.  This is mainly used to select
	 * the group equation.
	 */
	enum gcry_mpi_ec_models model;

	/* The actual ECC dialect used.  This is used for curve specific
	 * optimizations and to select encodings etc.
	 */
	enum ecc_dialects dialect;

	const char *p;              /* The prime defining the field.  */
	const char *a, *b;          /* The coefficients.  For Twisted Edwards
				     * Curves b is used for d.  For Montgomery
				     * Curves (a,b) has ((A-2)/4,B^-1).
				     */
	const char *n;              /* The order of the base point.  */
	const char *g_x, *g_y;      /* Base point.  */
	unsigned int h;             /* Cofactor.  */
};

int ec_mpi_ctx_init(struct mpi_ec_ctx *ec, const struct ecc_domain_parms *ecp);
void ec_mpi_ctx_deinit(struct mpi_ec_ctx *ec);
