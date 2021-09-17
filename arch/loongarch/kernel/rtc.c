// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2021 Loongson Technology Corporation Limited
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <asm/loongson.h>

#define RTC_TOYREAD0    0x2C
#define RTC_YEAR        0x30

unsigned long loongson_get_rtc_time(void)
{
	unsigned int year, mon, day, hour, min, sec;
	unsigned int value;

	value = ls7a_readl(LS7A_RTC_REG_BASE + RTC_TOYREAD0);
	sec = (value >> 4) & 0x3f;
	min = (value >> 10) & 0x3f;
	hour = (value >> 16) & 0x1f;
	day = (value >> 21) & 0x1f;
	mon = (value >> 26) & 0x3f;
	year = ls7a_readl(LS7A_RTC_REG_BASE + RTC_YEAR);

	year = 1900 + year;

	return mktime64(year, mon, day, hour, min, sec);
}

void read_persistent_clock64(struct timespec64 *ts)
{
	ts->tv_sec = loongson_get_rtc_time();
	ts->tv_nsec = 0;
}
