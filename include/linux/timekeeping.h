/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_TIMEKEEPING_H
#define _LINUX_TIMEKEEPING_H

#include <linux/errno.h>
#include <linux/clocksource_ids.h>

/* Included from linux/ktime.h */

void timekeeping_init(void);
extern int timekeeping_suspended;

/* Architecture timer tick functions: */
extern void legacy_timer_tick(unsigned long ticks);

/*
 * Get and set timeofday
 */
extern int do_settimeofday64(const struct timespec64 *ts);
extern int do_sys_settimeofday64(const struct timespec64 *tv,
				 const struct timezone *tz);
/*
 * RTC specific
 */
extern bool timekeeping_rtc_skipsuspend(void);
extern bool timekeeping_rtc_skipresume(void);

extern void timekeeping_inject_sleeptime64(const struct timespec64 *delta);

/*
 * struct ktime_timestanps - Simultaneous mono/boot/real timestamps
 * @mono:	Monotonic timestamp
 * @boot:	Boottime timestamp
 * @real:	Realtime timestamp
 */
struct ktime_timestamps {
	u64		mono;
	u64		boot;
	u64		real;
};

/**
 * struct system_time_snapshot - simultaneous raw/real time capture with
 *				 counter value
 * @cycles:	Clocksource counter value to produce the system times
 * @real:	Realtime system time
 * @raw:	Monotonic raw system time
 * @clock_was_set_seq:	The sequence number of clock was set events
 * @cs_was_changed_seq:	The sequence number of clocksource change events
 */
struct system_time_snapshot {
	u64			cycles;
	ktime_t			real;
	ktime_t			raw;
	enum clocksource_ids	cs_id;
	unsigned int		clock_was_set_seq;
	u8			cs_was_changed_seq;
};

/**
 * struct system_device_crosststamp - system/device cross-timestamp
 *				      (synchronized capture)
 * @device:		Device time
 * @sys_realtime:	Realtime simultaneous with device time
 * @sys_monoraw:	Monotonic raw simultaneous with device time
 */
struct system_device_crosststamp {
	ktime_t device;
	ktime_t sys_realtime;
	ktime_t sys_monoraw;
};

/**
 * struct system_counterval_t - system counter value with the pointer to the
 *				corresponding clocksource
 * @cycles:	System counter value
 * @cs:		Clocksource corresponding to system counter value. Used by
 *		timekeeping code to verify comparibility of two cycle values
 */
struct system_counterval_t {
	u64			cycles;
	struct clocksource	*cs;
};

/*
 * Get cross timestamp between system clock and device clock
 */
extern int get_device_system_crosststamp(
			int (*get_time_fn)(ktime_t *device_time,
				struct system_counterval_t *system_counterval,
				void *ctx),
			void *ctx,
			struct system_time_snapshot *history,
			struct system_device_crosststamp *xtstamp);

/*
 * Simultaneously snapshot realtime and monotonic raw clocks
 */
extern void ktime_get_snapshot(struct system_time_snapshot *systime_snapshot);

/* NMI safe mono/boot/realtime timestamps */
extern void ktime_get_fast_timestamps(struct ktime_timestamps *snap);

/*
 * Persistent clock related interfaces
 */
extern int persistent_clock_is_local;

extern void read_persistent_clock64(struct timespec64 *ts);
void read_persistent_wall_and_boot_offset(struct timespec64 *wall_clock,
					  struct timespec64 *boot_offset);
#ifdef CONFIG_GENERIC_CMOS_UPDATE
extern int update_persistent_clock64(struct timespec64 now);
#endif

#endif /* _LINUX_TIMEKEEPING_H */
