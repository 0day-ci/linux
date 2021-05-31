// SPDX-License-Identifier: LGPL-2.0+
/*
 * Copyright (C) 1993, 1994, 1995, 1996, 1997 Free Software Foundation, Inc.
 * This file is part of the GNU C Library.
 * Contributed by Paul Eggert (eggert@twinsun.com).
 *
 * The GNU C Library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * The GNU C Library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with the GNU C Library; see the file COPYING.LIB.  If not,
 * write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/*
 * Converts the calendar time to broken-down time representation
 *
 * 2009-7-14:
 *   Moved from glibc-2.6 to kernel by Zhaolei<zhaolei@cn.fujitsu.com>
 * 2021-5-22:
 *   Partially reimplemented by Cassio Neri <cassio.neri@gmail.com>
 */

#include <linux/time.h>
#include <linux/module.h>

/*
 * True if y is a leap year (every 4 years, except every 100th isn't, and
 * every 400th is).
 */
static bool is_leap(long year)
{
	/* This implementation is more branch-predictor friendly than the
	 * traditional:
	 *   return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
	 */
	return year % 100 != 0 ? year % 4 == 0 : year % 400 == 0;
}

#define SECS_PER_HOUR	(60 * 60)
#define SECS_PER_DAY	(SECS_PER_HOUR * 24)

/*
 * This function converts time64_t to rtc_time.
 *
 * @param[in]  totalsecs   The number of seconds since 01-01-1970 00:00:00.
 * @param[in]  offset      Seconds added to totalsecs.
 * @param[out] result      Pointer to struct tm variable to receive
 *                         broken-down time.
 */
void time64_to_tm(time64_t totalsecs, int offset, struct tm *result)
{
	long days, rem;
	int remainder;

	u64 r0, n1, q1, u64rem;
	u32 r1, n2, q2, r2;
	u64 u2;
	u32 n3, q3, r3;

	u32 j;
	u64 y;
	u32 m, d;

	days = div_s64_rem(totalsecs, SECS_PER_DAY, &remainder);
	rem = remainder;
	rem += offset;
	while (rem < 0) {
		rem += SECS_PER_DAY;
		--days;
	}
	while (rem >= SECS_PER_DAY) {
		rem -= SECS_PER_DAY;
		++days;
	}

	result->tm_hour = rem / SECS_PER_HOUR;
	rem %= SECS_PER_HOUR;
	result->tm_min = rem / 60;
	result->tm_sec = rem % 60;

	/* January 1, 1970 was a Thursday. */
	result->tm_wday = (4 + days) % 7;
	if (result->tm_wday < 0)
		result->tm_wday += 7;

	/*
	 * The following algorithm is Proposition 6.3 of Neri and Schneider,
	 * "Euclidean Affine Functions and Applications to Calendar Algorithms".
	 * https://arxiv.org/abs/2102.06959
	 */

	r0 = days + 2305843009213814918;

	n1 = 4 * r0 + 3;
	q1 = div64_u64_rem(n1, 146097, &u64rem);
	r1 = u64rem / 4;

	n2 = 4 * r1 + 3;
	u2 = ((u64) 2939745) * n2;
	q2 = u2 >> 32;
	r2 = ((u32) u2) / 2939745 / 4;

	n3 = 2141 * r2 + 197913;
	q3 = n3 >> 16;
	r3 = ((u16) n3) / 2141;

	j = r2 >= 306;
	y = 100 * q1 + q2 + j - 6313183731940000;
	m = j ? q3 - 12 : q3;
	d = r3 + 1;

	result->tm_year = y - 1900;
	result->tm_mon  = m - 1;
	result->tm_mday = d;

	/* r2 contains the number of days since previous Mar 1st and j == true
	 * if and only if month is Jan or Feb. The bellow is then a correction
	 * to get the numbers of days since previous Jan 1st.
	 */
	result->tm_yday = j ? r2 - 306 : r2 + 59 + is_leap(y);
}
EXPORT_SYMBOL(time64_to_tm);
