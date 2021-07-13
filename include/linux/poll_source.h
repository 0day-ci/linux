/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * poll_source.h - cpuidle busy waiting API
 */
#ifndef __LINUX_POLLSOURCE_H__
#define __LINUX_POLLSOURCE_H__

#include <linux/list.h>

struct poll_source;

struct poll_source_ops {
	void (*start)(struct poll_source *src);
	void (*stop)(struct poll_source *src);
	void (*poll)(struct poll_source *src);
};

struct poll_source {
	const struct poll_source_ops *ops;
	struct list_head node;
	int cpu;
};

/**
 * poll_source_register - Add a poll_source for a CPU
 */
#if defined(CONFIG_CPU_IDLE) && defined(CONFIG_ARCH_HAS_CPU_RELAX)
int poll_source_register(struct poll_source *src);
#else
static inline int poll_source_register(struct poll_source *src)
{
	return 0;
}
#endif

/**
 * poll_source_unregister - Remove a previously registered poll_source
 */
#if defined(CONFIG_CPU_IDLE) && defined(CONFIG_ARCH_HAS_CPU_RELAX)
int poll_source_unregister(struct poll_source *src);
#else
static inline int poll_source_unregister(struct poll_source *src)
{
	return 0;
}
#endif

/* Used by the cpuidle driver */
void poll_source_start(void);
void poll_source_run_once(void);
void poll_source_stop(void);

#endif /* __LINUX_POLLSOURCE_H__ */
