/*
 *  include/linux/ktime.h
 *
 *  ktime_t - nanosecond-resolution time format.
 *
 *   Copyright(C) 2005, Thomas Gleixner <tglx@linutronix.de>
 *   Copyright(C) 2005, Red Hat, Inc., Ingo Molnar
 *
 *  data type definitions, declarations, prototypes and macros.
 *
 *  Started by: Thomas Gleixner and Ingo Molnar
 *
 *  Credits:
 *
 *  	Roman Zippel provided the ideas and primary code snippets of
 *  	the ktime_t union and further simplifications of the original
 *  	code.
 *
 *  For licencing details see kernel-base/COPYING
 */
#ifndef _LINUX_KTIME_H
#define _LINUX_KTIME_H

#include <linux/time.h>
#include <linux/jiffies.h>
#include <asm/bug.h>

/* Nanosecond scalar representation for kernel time values */
typedef s64	ktime_t;

/**
 * ktime_set - Set a ktime_t variable from a seconds/nanoseconds value
 * @secs:	seconds to set
 * @nsecs:	nanoseconds to set
 *
 * Return: The ktime_t representation of the value.
 */
static inline ktime_t ktime_set(const s64 secs, const unsigned long nsecs)
{
	if (unlikely(secs >= KTIME_SEC_MAX))
		return KTIME_MAX;

	return secs * NSEC_PER_SEC + (s64)nsecs;
}

/* Subtract two ktime_t variables. rem = lhs -rhs: */
#define ktime_sub(lhs, rhs)	((lhs) - (rhs))

/* Add two ktime_t variables. res = lhs + rhs: */
#define ktime_add(lhs, rhs)	((lhs) + (rhs))

/*
 * Same as ktime_add(), but avoids undefined behaviour on overflow; however,
 * this means that you must check the result for overflow yourself.
 */
#define ktime_add_unsafe(lhs, rhs)	((u64) (lhs) + (rhs))

/*
 * Add a ktime_t variable and a scalar nanosecond value.
 * res = kt + nsval:
 */
#define ktime_add_ns(kt, nsval)		((kt) + (nsval))

/*
 * Subtract a scalar nanosecod from a ktime_t variable
 * res = kt - nsval:
 */
#define ktime_sub_ns(kt, nsval)		((kt) - (nsval))

/* convert a timespec64 to ktime_t format: */
static inline ktime_t timespec64_to_ktime(struct timespec64 ts)
{
	return ktime_set(ts.tv_sec, ts.tv_nsec);
}

/* Map the ktime_t to timespec conversion to ns_to_timespec function */
#define ktime_to_timespec64(kt)		ns_to_timespec64((kt))

/* Convert ktime_t to nanoseconds */
static inline s64 ktime_to_ns(const ktime_t kt)
{
	return kt;
}

/**
 * ktime_compare - Compares two ktime_t variables for less, greater or equal
 * @cmp1:	comparable1
 * @cmp2:	comparable2
 *
 * Return: ...
 *   cmp1  < cmp2: return <0
 *   cmp1 == cmp2: return 0
 *   cmp1  > cmp2: return >0
 */
static inline int ktime_compare(const ktime_t cmp1, const ktime_t cmp2)
{
	if (cmp1 < cmp2)
		return -1;
	if (cmp1 > cmp2)
		return 1;
	return 0;
}

/**
 * ktime_after - Compare if a ktime_t value is bigger than another one.
 * @cmp1:	comparable1
 * @cmp2:	comparable2
 *
 * Return: true if cmp1 happened after cmp2.
 */
static inline bool ktime_after(const ktime_t cmp1, const ktime_t cmp2)
{
	return ktime_compare(cmp1, cmp2) > 0;
}

/**
 * ktime_before - Compare if a ktime_t value is smaller than another one.
 * @cmp1:	comparable1
 * @cmp2:	comparable2
 *
 * Return: true if cmp1 happened before cmp2.
 */
static inline bool ktime_before(const ktime_t cmp1, const ktime_t cmp2)
{
	return ktime_compare(cmp1, cmp2) < 0;
}

#if BITS_PER_LONG < 64
extern s64 __ktime_divns(const ktime_t kt, s64 div);
static inline s64 ktime_divns(const ktime_t kt, s64 div)
{
	/*
	 * Negative divisors could cause an inf loop,
	 * so bug out here.
	 */
	BUG_ON(div < 0);
	if (__builtin_constant_p(div) && !(div >> 32)) {
		s64 ns = kt;
		u64 tmp = ns < 0 ? -ns : ns;

		do_div(tmp, div);
		return ns < 0 ? -tmp : tmp;
	} else {
		return __ktime_divns(kt, div);
	}
}
#else /* BITS_PER_LONG < 64 */
static inline s64 ktime_divns(const ktime_t kt, s64 div)
{
	/*
	 * 32-bit implementation cannot handle negative divisors,
	 * so catch them on 64bit as well.
	 */
	WARN_ON(div < 0);
	return kt / div;
}
#endif

static inline s64 ktime_to_us(const ktime_t kt)
{
	return ktime_divns(kt, NSEC_PER_USEC);
}

static inline s64 ktime_to_ms(const ktime_t kt)
{
	return ktime_divns(kt, NSEC_PER_MSEC);
}

static inline s64 ktime_us_delta(const ktime_t later, const ktime_t earlier)
{
       return ktime_to_us(ktime_sub(later, earlier));
}

static inline s64 ktime_ms_delta(const ktime_t later, const ktime_t earlier)
{
	return ktime_to_ms(ktime_sub(later, earlier));
}

static inline ktime_t ktime_add_us(const ktime_t kt, const u64 usec)
{
	return ktime_add_ns(kt, usec * NSEC_PER_USEC);
}

static inline ktime_t ktime_add_ms(const ktime_t kt, const u64 msec)
{
	return ktime_add_ns(kt, msec * NSEC_PER_MSEC);
}

static inline ktime_t ktime_sub_us(const ktime_t kt, const u64 usec)
{
	return ktime_sub_ns(kt, usec * NSEC_PER_USEC);
}

static inline ktime_t ktime_sub_ms(const ktime_t kt, const u64 msec)
{
	return ktime_sub_ns(kt, msec * NSEC_PER_MSEC);
}

extern ktime_t ktime_add_safe(const ktime_t lhs, const ktime_t rhs);

/**
 * ktime_to_timespec64_cond - convert a ktime_t variable to timespec64
 *			    format only if the variable contains data
 * @kt:		the ktime_t variable to convert
 * @ts:		the timespec variable to store the result in
 *
 * Return: %true if there was a successful conversion, %false if kt was 0.
 */
static inline __must_check bool ktime_to_timespec64_cond(const ktime_t kt,
						       struct timespec64 *ts)
{
	if (kt) {
		*ts = ktime_to_timespec64(kt);
		return true;
	} else {
		return false;
	}
}

#include <vdso/ktime.h>

static inline ktime_t ns_to_ktime(u64 ns)
{
	return ns;
}

static inline ktime_t ms_to_ktime(u64 ms)
{
	return ms * NSEC_PER_MSEC;
}

/*
 * ktime_get() family: read the current time in a multitude of ways,
 *
 * The default time reference is CLOCK_MONOTONIC, starting at
 * boot time but not counting the time spent in suspend.
 * For other references, use the functions with "real", "clocktai",
 * "boottime" and "raw" suffixes.
 *
 * To get the time in a different format, use the ones wit
 * "ns", "ts64" and "seconds" suffix.
 *
 * See Documentation/core-api/timekeeping.rst for more details.
 */


/*
 * timespec64 based interfaces
 */
extern void ktime_get_raw_ts64(struct timespec64 *ts);
extern void ktime_get_ts64(struct timespec64 *ts);
extern void ktime_get_real_ts64(struct timespec64 *tv);
extern void ktime_get_coarse_ts64(struct timespec64 *ts);
extern void ktime_get_coarse_real_ts64(struct timespec64 *ts);

void getboottime64(struct timespec64 *ts);

/*
 * time64_t base interfaces
 */
extern time64_t ktime_get_seconds(void);
extern time64_t __ktime_get_real_seconds(void);
extern time64_t ktime_get_real_seconds(void);

/*
 * ktime_t based interfaces
 */

enum tk_offsets {
	TK_OFFS_REAL,
	TK_OFFS_BOOT,
	TK_OFFS_TAI,
	TK_OFFS_MAX,
};

extern ktime_t ktime_get(void);
extern ktime_t ktime_get_with_offset(enum tk_offsets offs);
extern ktime_t ktime_get_coarse_with_offset(enum tk_offsets offs);
extern ktime_t ktime_mono_to_any(ktime_t tmono, enum tk_offsets offs);
extern ktime_t ktime_get_raw(void);
extern u32 ktime_get_resolution_ns(void);

/**
 * ktime_get_real - get the real (wall-) time in ktime_t format
 */
static inline ktime_t ktime_get_real(void)
{
	return ktime_get_with_offset(TK_OFFS_REAL);
}

static inline ktime_t ktime_get_coarse_real(void)
{
	return ktime_get_coarse_with_offset(TK_OFFS_REAL);
}

/**
 * ktime_get_boottime - Returns monotonic time since boot in ktime_t format
 *
 * This is similar to CLOCK_MONTONIC/ktime_get, but also includes the
 * time spent in suspend.
 */
static inline ktime_t ktime_get_boottime(void)
{
	return ktime_get_with_offset(TK_OFFS_BOOT);
}

static inline ktime_t ktime_get_coarse_boottime(void)
{
	return ktime_get_coarse_with_offset(TK_OFFS_BOOT);
}

/**
 * ktime_get_clocktai - Returns the TAI time of day in ktime_t format
 */
static inline ktime_t ktime_get_clocktai(void)
{
	return ktime_get_with_offset(TK_OFFS_TAI);
}

static inline ktime_t ktime_get_coarse_clocktai(void)
{
	return ktime_get_coarse_with_offset(TK_OFFS_TAI);
}

static inline ktime_t ktime_get_coarse(void)
{
	struct timespec64 ts;

	ktime_get_coarse_ts64(&ts);
	return timespec64_to_ktime(ts);
}

static inline u64 ktime_get_coarse_ns(void)
{
	return ktime_to_ns(ktime_get_coarse());
}

static inline u64 ktime_get_coarse_real_ns(void)
{
	return ktime_to_ns(ktime_get_coarse_real());
}

static inline u64 ktime_get_coarse_boottime_ns(void)
{
	return ktime_to_ns(ktime_get_coarse_boottime());
}

static inline u64 ktime_get_coarse_clocktai_ns(void)
{
	return ktime_to_ns(ktime_get_coarse_clocktai());
}

/**
 * ktime_mono_to_real - Convert monotonic time to clock realtime
 */
static inline ktime_t ktime_mono_to_real(ktime_t mono)
{
	return ktime_mono_to_any(mono, TK_OFFS_REAL);
}

static inline u64 ktime_get_ns(void)
{
	return ktime_to_ns(ktime_get());
}

static inline u64 ktime_get_real_ns(void)
{
	return ktime_to_ns(ktime_get_real());
}

static inline u64 ktime_get_boottime_ns(void)
{
	return ktime_to_ns(ktime_get_boottime());
}

static inline u64 ktime_get_clocktai_ns(void)
{
	return ktime_to_ns(ktime_get_clocktai());
}

static inline u64 ktime_get_raw_ns(void)
{
	return ktime_to_ns(ktime_get_raw());
}

extern u64 ktime_get_mono_fast_ns(void);
extern u64 ktime_get_raw_fast_ns(void);
extern u64 ktime_get_boot_fast_ns(void);
extern u64 ktime_get_real_fast_ns(void);

/*
 * timespec64/time64_t interfaces utilizing the ktime based ones
 * for API completeness, these could be implemented more efficiently
 * if needed.
 */
static inline void ktime_get_boottime_ts64(struct timespec64 *ts)
{
	*ts = ktime_to_timespec64(ktime_get_boottime());
}

static inline void ktime_get_coarse_boottime_ts64(struct timespec64 *ts)
{
	*ts = ktime_to_timespec64(ktime_get_coarse_boottime());
}

static inline time64_t ktime_get_boottime_seconds(void)
{
	return ktime_divns(ktime_get_coarse_boottime(), NSEC_PER_SEC);
}

static inline void ktime_get_clocktai_ts64(struct timespec64 *ts)
{
	*ts = ktime_to_timespec64(ktime_get_clocktai());
}

static inline void ktime_get_coarse_clocktai_ts64(struct timespec64 *ts)
{
	*ts = ktime_to_timespec64(ktime_get_coarse_clocktai());
}

static inline time64_t ktime_get_clocktai_seconds(void)
{
	return ktime_divns(ktime_get_coarse_clocktai(), NSEC_PER_SEC);
}

#endif /* _LINUX_KTIME_H */
