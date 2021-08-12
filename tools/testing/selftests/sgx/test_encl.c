// SPDX-License-Identifier: GPL-2.0
/*  Copyright(c) 2016-20 Intel Corporation. */

#include <stddef.h>
#include "defines.h"

static uint8_t encl_buffer[8192] = { 1 };

static void *memcpy(void *dest, const void *src, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++)
		((char *)dest)[i] = ((char *)src)[i];

	return dest;
}

void do_encl_op_put(void *op)
{
	struct encl_op_put *op2 = op;

	memcpy(&encl_buffer[0], &op2->value, 8);
}

void do_encl_op_get(void *op)
{
	struct encl_op_get *op2 = op;

	memcpy(&op2->value, &encl_buffer[0], 8);
}

void encl_body(void *rdi,  void *rsi)
{
	struct encl_op_header *op = (struct encl_op_header *)rdi;

	switch (op->type) {
	case ENCL_OP_PUT:
		do_encl_op_put(op);
		break;

	case ENCL_OP_GET:
		do_encl_op_get(op);
		break;

	default:
		break;
	}
}
