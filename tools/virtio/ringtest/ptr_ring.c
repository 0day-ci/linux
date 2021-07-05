// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include "main.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <malloc.h>
#include <assert.h>
#include <errno.h>
#include <limits.h>

#include <linux/align.h>
#include <linux/cache.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "../../../include/linux/ptr_ring.h"

static unsigned long long headcnt, tailcnt;
static struct ptr_ring array ____cacheline_aligned_in_smp;

/* implemented by ring */
void alloc_ring(void)
{
	int ret = ptr_ring_init(&array, ring_size, 0);
	assert(!ret);
	/* Hacky way to poke at ring internals. Useful for testing though. */
	if (param)
		array.batch = param;
}

/* guest side */
int add_inbuf(unsigned len, void *buf, void *datap)
{
	int ret;

	ret = __ptr_ring_produce(&array, buf);
	if (ret >= 0) {
		ret = 0;
		headcnt++;
	}

	return ret;
}

/*
 * ptr_ring API provides no way for producer to find out whether a given
 * buffer was consumed.  Our tests merely require that a successful get_buf
 * implies that add_inbuf succeed in the past, and that add_inbuf will succeed,
 * fake it accordingly.
 */
void *get_buf(unsigned *lenp, void **bufp)
{
	void *datap;

	if (tailcnt == headcnt || __ptr_ring_full(&array))
		datap = NULL;
	else {
		datap = "Buffer\n";
		++tailcnt;
	}

	return datap;
}

bool used_empty()
{
	return (tailcnt == headcnt || __ptr_ring_full(&array));
}

void disable_call()
{
	assert(0);
}

bool enable_call()
{
	assert(0);
}

void kick_available(void)
{
	assert(0);
}

/* host side */
void disable_kick()
{
	assert(0);
}

bool enable_kick()
{
	assert(0);
}

bool avail_empty()
{
	return __ptr_ring_empty(&array);
}

bool use_buf(unsigned *lenp, void **bufp)
{
	void *ptr;

	ptr = __ptr_ring_consume(&array);

	return ptr;
}

void call_used(void)
{
	assert(0);
}
