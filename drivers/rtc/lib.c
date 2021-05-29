// SPDX-License-Identifier: GPL-2.0
/*
 * rtc and date/time utility functions
 *
 * Copyright (C) 2005-06 Tower Technologies
 * Author: Alessandro Zummo <a.zummo@towertech.it>
 *
 * based on arch/arm/common/rtctime.c and other bits
 *
 * Author: Cassio Neri <cassio.neri@gmail.com> (rtc_time64_to_tm)
 */

#include <linux/export.h>
#include <linux/rtc.h>

static const unsigned char rtc_days_in_month[] = {
	31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

static const unsigned short rtc_ydays[2][13] = {
	/* Normal years */
	{ 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 },
	/* Leap years */
	{ 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366 }
};

/*
 * The number of days in the month.
 */
int rtc_month_days(unsigned int month, unsigned int year)
{
	return rtc_days_in_month[month] + (is_leap_year(year) && month == 1);
}
EXPORT_SYMBOL(rtc_month_days);

/*
 * The number of days since January 1. (0 to 365)
 */
int rtc_year_days(unsigned int day, unsigned int month, unsigned int year)
{
	return rtc_ydays[is_leap_year(year)][month] + day - 1;
}
EXPORT_SYMBOL(rtc_year_days);

/*
 * This function converts time64_t to rtc_time.
 *
 * @param[in]  time   The number of seconds since 01-01-1970 00:00:00.
 *                    (Must be positive.)
 * @param[out] tm     Pointer to the struct rtc_time.
 */
void rtc_time64_to_tm(time64_t time, struct rtc_time *tm)
{
	unsigned int secs;
	int days;

	u32 r0, n1, q1;
	u32 r1, n2, q2, r2;
	u64 u2;
	u32 n3, q3, r3;

	u32 j;
	u32 y;
	u32 m, d;

	/* time must be positive */
	days = div_s64_rem(time, 86400, &secs);

	/* day of the week, 1970-01-01 was a Thursday */
	tm->tm_wday = (days + 4) % 7;

	/*
	 * The following algorithm is Proposition 6.3 of Neri and Schneider,
	 * "Euclidean Affine Functions and Applications to Calendar Algorithms".
	 * https://arxiv.org/abs/2102.06959
	 */

	r0 = days + 719468;

	n1 = 4 * r0 + 3;
	q1 = n1 / 146097;
	r1 = n1 % 146097 / 4;

	n2 = 4 * r1 + 3;
	u2 = ((u64) 2939745) * n2;
	q2 = u2 >> 32;
	r2 = ((u32) u2) / 2939745 / 4;

	n3 = 2141 * r2 + 197913;
	q3 = n3 >> 16;
	r3 = ((u16) n3) / 2141;

	j = r2 >= 306;
	y = 100 * q1 + q2 + j;
	m = j ? q3 - 12 : q3;
	d = r3 + 1;

	tm->tm_year = y - 1900;
	tm->tm_mon  = m - 1;
	tm->tm_mday = d;

	/*
	 * r2 contains the number of days since previous Mar 1st and j == true
	 * if and only if month is Jan or Feb. The bellow is then a correction
	 * to get the numbers of days since previous Jan 1st.
	 */
	tm->tm_yday = j ? r2 - 305 : r2 + 60 + is_leap_year(y);

	tm->tm_hour = secs / 3600;
	secs -= tm->tm_hour * 3600;
	tm->tm_min = secs / 60;
	tm->tm_sec = secs - tm->tm_min * 60;

	tm->tm_isdst = 0;
}
EXPORT_SYMBOL(rtc_time64_to_tm);

/*
 * Does the rtc_time represent a valid date/time?
 */
int rtc_valid_tm(struct rtc_time *tm)
{
	if (tm->tm_year < 70 ||
	    tm->tm_year > (INT_MAX - 1900) ||
	    ((unsigned int)tm->tm_mon) >= 12 ||
	    tm->tm_mday < 1 ||
	    tm->tm_mday > rtc_month_days(tm->tm_mon,
					 ((unsigned int)tm->tm_year + 1900)) ||
	    ((unsigned int)tm->tm_hour) >= 24 ||
	    ((unsigned int)tm->tm_min) >= 60 ||
	    ((unsigned int)tm->tm_sec) >= 60)
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL(rtc_valid_tm);

/*
 * rtc_tm_to_time64 - Converts rtc_time to time64_t.
 * Convert Gregorian date to seconds since 01-01-1970 00:00:00.
 */
time64_t rtc_tm_to_time64(struct rtc_time *tm)
{
	return mktime64(((unsigned int)tm->tm_year + 1900), tm->tm_mon + 1,
			tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
}
EXPORT_SYMBOL(rtc_tm_to_time64);

/*
 * Convert rtc_time to ktime
 */
ktime_t rtc_tm_to_ktime(struct rtc_time tm)
{
	return ktime_set(rtc_tm_to_time64(&tm), 0);
}
EXPORT_SYMBOL_GPL(rtc_tm_to_ktime);

/*
 * Convert ktime to rtc_time
 */
struct rtc_time rtc_ktime_to_tm(ktime_t kt)
{
	struct timespec64 ts;
	struct rtc_time ret;

	ts = ktime_to_timespec64(kt);
	/* Round up any ns */
	if (ts.tv_nsec)
		ts.tv_sec++;
	rtc_time64_to_tm(ts.tv_sec, &ret);
	return ret;
}
EXPORT_SYMBOL_GPL(rtc_ktime_to_tm);
