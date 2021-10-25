/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_MESSAGES_H
#define BTRFS_MESSAGES_H

#include <linux/printk.h>

static inline __printf(2, 3) __cold
void btrfs_no_printk(const struct btrfs_fs_info *fs_info, const char *fmt, ...)
{
}

#ifdef CONFIG_PRINTK
__printf(2, 3)
__cold
void btrfs_printk(const struct btrfs_fs_info *fs_info, const char *fmt, ...);
#else
#define btrfs_printk(fs_info, fmt, args...) \
	btrfs_no_printk(fs_info, fmt, ##args)
#endif

#define btrfs_emerg(fs_info, fmt, args...) \
	btrfs_printk(fs_info, KERN_EMERG fmt, ##args)
#define btrfs_alert(fs_info, fmt, args...) \
	btrfs_printk(fs_info, KERN_ALERT fmt, ##args)
#define btrfs_crit(fs_info, fmt, args...) \
	btrfs_printk(fs_info, KERN_CRIT fmt, ##args)
#define btrfs_err(fs_info, fmt, args...) \
	btrfs_printk(fs_info, KERN_ERR fmt, ##args)
#define btrfs_warn(fs_info, fmt, args...) \
	btrfs_printk(fs_info, KERN_WARNING fmt, ##args)
#define btrfs_notice(fs_info, fmt, args...) \
	btrfs_printk(fs_info, KERN_NOTICE fmt, ##args)
#define btrfs_info(fs_info, fmt, args...) \
	btrfs_printk(fs_info, KERN_INFO fmt, ##args)

/*
 * Wrappers that use printk_in_rcu
 */
#define btrfs_emerg_in_rcu(fs_info, fmt, args...) \
	btrfs_printk_in_rcu(fs_info, KERN_EMERG fmt, ##args)
#define btrfs_alert_in_rcu(fs_info, fmt, args...) \
	btrfs_printk_in_rcu(fs_info, KERN_ALERT fmt, ##args)
#define btrfs_crit_in_rcu(fs_info, fmt, args...) \
	btrfs_printk_in_rcu(fs_info, KERN_CRIT fmt, ##args)
#define btrfs_err_in_rcu(fs_info, fmt, args...) \
	btrfs_printk_in_rcu(fs_info, KERN_ERR fmt, ##args)
#define btrfs_warn_in_rcu(fs_info, fmt, args...) \
	btrfs_printk_in_rcu(fs_info, KERN_WARNING fmt, ##args)
#define btrfs_notice_in_rcu(fs_info, fmt, args...) \
	btrfs_printk_in_rcu(fs_info, KERN_NOTICE fmt, ##args)
#define btrfs_info_in_rcu(fs_info, fmt, args...) \
	btrfs_printk_in_rcu(fs_info, KERN_INFO fmt, ##args)

/*
 * Wrappers that use a ratelimited printk_in_rcu
 */
#define btrfs_emerg_rl_in_rcu(fs_info, fmt, args...) \
	btrfs_printk_rl_in_rcu(fs_info, KERN_EMERG fmt, ##args)
#define btrfs_alert_rl_in_rcu(fs_info, fmt, args...) \
	btrfs_printk_rl_in_rcu(fs_info, KERN_ALERT fmt, ##args)
#define btrfs_crit_rl_in_rcu(fs_info, fmt, args...) \
	btrfs_printk_rl_in_rcu(fs_info, KERN_CRIT fmt, ##args)
#define btrfs_err_rl_in_rcu(fs_info, fmt, args...) \
	btrfs_printk_rl_in_rcu(fs_info, KERN_ERR fmt, ##args)
#define btrfs_warn_rl_in_rcu(fs_info, fmt, args...) \
	btrfs_printk_rl_in_rcu(fs_info, KERN_WARNING fmt, ##args)
#define btrfs_notice_rl_in_rcu(fs_info, fmt, args...) \
	btrfs_printk_rl_in_rcu(fs_info, KERN_NOTICE fmt, ##args)
#define btrfs_info_rl_in_rcu(fs_info, fmt, args...) \
	btrfs_printk_rl_in_rcu(fs_info, KERN_INFO fmt, ##args)

/*
 * Wrappers that use a ratelimited printk
 */
#define btrfs_emerg_rl(fs_info, fmt, args...) \
	btrfs_printk_ratelimited(fs_info, KERN_EMERG fmt, ##args)
#define btrfs_alert_rl(fs_info, fmt, args...) \
	btrfs_printk_ratelimited(fs_info, KERN_ALERT fmt, ##args)
#define btrfs_crit_rl(fs_info, fmt, args...) \
	btrfs_printk_ratelimited(fs_info, KERN_CRIT fmt, ##args)
#define btrfs_err_rl(fs_info, fmt, args...) \
	btrfs_printk_ratelimited(fs_info, KERN_ERR fmt, ##args)
#define btrfs_warn_rl(fs_info, fmt, args...) \
	btrfs_printk_ratelimited(fs_info, KERN_WARNING fmt, ##args)
#define btrfs_notice_rl(fs_info, fmt, args...) \
	btrfs_printk_ratelimited(fs_info, KERN_NOTICE fmt, ##args)
#define btrfs_info_rl(fs_info, fmt, args...) \
	btrfs_printk_ratelimited(fs_info, KERN_INFO fmt, ##args)

#if defined(CONFIG_DYNAMIC_DEBUG)
#define btrfs_debug(fs_info, fmt, args...)				\
	_dynamic_func_call_no_desc(fmt, btrfs_printk,			\
				   fs_info, KERN_DEBUG fmt, ##args)
#define btrfs_debug_in_rcu(fs_info, fmt, args...)			\
	_dynamic_func_call_no_desc(fmt, btrfs_printk_in_rcu,		\
				   fs_info, KERN_DEBUG fmt, ##args)
#define btrfs_debug_rl_in_rcu(fs_info, fmt, args...)			\
	_dynamic_func_call_no_desc(fmt, btrfs_printk_rl_in_rcu,		\
				   fs_info, KERN_DEBUG fmt, ##args)
#define btrfs_debug_rl(fs_info, fmt, args...)				\
	_dynamic_func_call_no_desc(fmt, btrfs_printk_ratelimited,	\
				   fs_info, KERN_DEBUG fmt, ##args)
#elif defined(DEBUG)
#define btrfs_debug(fs_info, fmt, args...) \
	btrfs_printk(fs_info, KERN_DEBUG fmt, ##args)
#define btrfs_debug_in_rcu(fs_info, fmt, args...) \
	btrfs_printk_in_rcu(fs_info, KERN_DEBUG fmt, ##args)
#define btrfs_debug_rl_in_rcu(fs_info, fmt, args...) \
	btrfs_printk_rl_in_rcu(fs_info, KERN_DEBUG fmt, ##args)
#define btrfs_debug_rl(fs_info, fmt, args...) \
	btrfs_printk_ratelimited(fs_info, KERN_DEBUG fmt, ##args)
#else
#define btrfs_debug(fs_info, fmt, args...) \
	btrfs_no_printk(fs_info, KERN_DEBUG fmt, ##args)
#define btrfs_debug_in_rcu(fs_info, fmt, args...) \
	btrfs_no_printk_in_rcu(fs_info, KERN_DEBUG fmt, ##args)
#define btrfs_debug_rl_in_rcu(fs_info, fmt, args...) \
	btrfs_no_printk_in_rcu(fs_info, KERN_DEBUG fmt, ##args)
#define btrfs_debug_rl(fs_info, fmt, args...) \
	btrfs_no_printk(fs_info, KERN_DEBUG fmt, ##args)
#endif

#define btrfs_printk_in_rcu(fs_info, fmt, args...)	\
do {							\
	rcu_read_lock();				\
	btrfs_printk(fs_info, fmt, ##args);		\
	rcu_read_unlock();				\
} while (0)

#define btrfs_no_printk_in_rcu(fs_info, fmt, args...)	\
do {							\
	rcu_read_lock();				\
	btrfs_no_printk(fs_info, fmt, ##args);		\
	rcu_read_unlock();				\
} while (0)

#define btrfs_printk_ratelimited(fs_info, fmt, args...)		\
do {								\
	static DEFINE_RATELIMIT_STATE(_rs,			\
		DEFAULT_RATELIMIT_INTERVAL,			\
		DEFAULT_RATELIMIT_BURST);       		\
	if (__ratelimit(&_rs))					\
		btrfs_printk(fs_info, fmt, ##args);		\
} while (0)

#define btrfs_printk_rl_in_rcu(fs_info, fmt, args...)		\
do {								\
	rcu_read_lock();					\
	btrfs_printk_ratelimited(fs_info, fmt, ##args);		\
	rcu_read_unlock();					\
} while (0)

#ifdef CONFIG_BTRFS_ASSERT
__cold __noreturn
static inline void assertfail(const char *expr, const char *file, int line)
{
	pr_err("assertion failed: %s, in %s:%d\n", expr, file, line);
	BUG();
}

#define ASSERT(expr)						\
	(likely(expr) ? (void)0 : assertfail(#expr, __FILE__, __LINE__))

#else
static inline void assertfail(const char *expr, const char* file, int line) { }
#define ASSERT(expr)	(void)(expr)
#endif

#endif
