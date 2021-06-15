// SPDX-License-Identifier: GPL-2.0-only
/*
 * String functions optimized for hardware which doesn't
 * handle unaligned memory accesses efficiently.
 *
 * Copyright (C) 2021 Matteo Croce
 */

#include <linux/types.h>
#include <linux/module.h>

/* size below a classic byte at time copy is done */
#define MIN_THRESHOLD 64

/* convenience types to avoid cast between different pointer types */
union types {
	u8 *u8;
	unsigned long *ulong;
	uintptr_t uptr;
};

union const_types {
	const u8 *u8;
	unsigned long *ulong;
};

void *memcpy(void *dest, const void *src, size_t count)
{
	const int bytes_long = BITS_PER_LONG / 8;
#ifndef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
	const int mask = bytes_long - 1;
	const int distance = (src - dest) & mask;
#endif
	union const_types s = { .u8 = src };
	union types d = { .u8 = dest };

#ifndef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
	if (count <= MIN_THRESHOLD)
		goto copy_remainder;

	/* copy a byte at time until destination is aligned */
	for (; count && d.uptr & mask; count--)
		*d.u8++ = *s.u8++;

	if (distance) {
		unsigned long last, next;

		/* move s backward to the previous alignment boundary */
		s.u8 -= distance;

		/* 32/64 bit wide copy from s to d.
		 * d is aligned now but s is not, so read s alignment wise,
		 * and do proper shift to get the right value.
		 * Works only on Little Endian machines.
		 */
		for (next = s.ulong[0]; count >= bytes_long + mask; count -= bytes_long) {
			last = next;
			next = s.ulong[1];

			d.ulong[0] = last >> (distance * 8) |
				     next << ((bytes_long - distance) * 8);

			d.ulong++;
			s.ulong++;
		}

		/* restore s with the original offset */
		s.u8 += distance;
	} else
#endif
	{
		/* if the source and dest lower bits are the same, do a simple
		 * 32/64 bit wide copy.
		 */
		for (; count >= bytes_long; count -= bytes_long)
			*d.ulong++ = *s.ulong++;
	}

	/* suppress warning when CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS=y */
	goto copy_remainder;

copy_remainder:
	while (count--)
		*d.u8++ = *s.u8++;

	return dest;
}
EXPORT_SYMBOL(memcpy);

void *__memcpy(void *dest, const void *src, size_t count)
{
	return memcpy(dest, src, count);
}
EXPORT_SYMBOL(__memcpy);
