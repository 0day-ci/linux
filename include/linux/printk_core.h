/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __KERNEL_PRINTK_CORE__
#define __KERNEL_PRINTK_CORE__

#include <linux/stdarg.h>
#include <linux/kern_levels.h>
#include <linux/linkage.h>

/* Low level printk API. Use carefully! */

#ifdef CONFIG_PRINTK

struct dev_printk_info;

asmlinkage __printf(4, 0)
int vprintk_emit(int facility, int level,
		 const struct dev_printk_info *dev_info,
		 const char *fmt, va_list args);

asmlinkage __printf(1, 0)
int vprintk(const char *fmt, va_list args);

asmlinkage __printf(1, 2) __cold
int _printk(const char *fmt, ...);

/*
 * Special printk facility for scheduler/timekeeping use only, _DO_NOT_USE_ !
 */
__printf(1, 2) __cold int _printk_deferred(const char *fmt, ...);

extern void __printk_safe_enter(void);
extern void __printk_safe_exit(void);
/*
 * The printk_deferred_enter/exit macros are available only as a hack for
 * some code paths that need to defer all printk console printing. Interrupts
 * must be disabled for the deferred duration.
 */
#define printk_deferred_enter __printk_safe_enter
#define printk_deferred_exit __printk_safe_exit

char *log_buf_addr_get(void);

#else /* CONFIG_PRINTK */

static inline __printf(1, 0)
int vprintk(const char *s, va_list args)
{
	return 0;
}
static inline __printf(1, 2) __cold
int _printk(const char *s, ...)
{
	return 0;
}
static inline __printf(1, 2) __cold
int _printk_deferred(const char *s, ...)
{
	return 0;
}

static inline void printk_deferred_enter(void)
{
}

static inline void printk_deferred_exit(void)
{
}

static inline char *log_buf_addr_get(void)
{
	return NULL;
}

#endif /* CONFING_PRINTK */

#endif
