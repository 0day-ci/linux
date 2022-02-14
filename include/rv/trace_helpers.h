/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Helper functions to facilitate the instrumentation of auto-generated
 * RV monitors create by dot2k.
 *
 * The dot2k tool is available at tools/tracing/rv/dot2/
 *
 * Copyright (C) 2019-2022 Daniel Bristot de Oliveira <bristot@kernel.org>
 */

#include <linux/ftrace.h>

struct tracepoint_hook_helper {
	struct tracepoint *tp;
	void *probe;
	int registered;
	char *name;
};

static inline void thh_compare_name(struct tracepoint *tp, void *priv)
{
	struct tracepoint_hook_helper *thh  = priv;

	if (!strcmp(thh->name, tp->name))
		thh->tp = tp;
}

static inline bool thh_fill_struct_tracepoint(struct tracepoint_hook_helper *thh)
{
	for_each_kernel_tracepoint(thh_compare_name, thh);

	return !!thh->tp;
}

static inline void thh_unhook_probes(struct tracepoint_hook_helper *thh, int helpers_count)
{
	int i;

	for (i = 0; i < helpers_count; i++) {
		if (!thh[i].registered)
			continue;

		tracepoint_probe_unregister(thh[i].tp, thh[i].probe, NULL);
	}
}

static inline int thh_hook_probes(struct tracepoint_hook_helper *thh, int helpers_count)
{
	int retval;
	int i;

	for (i = 0; i < helpers_count; i++) {
		retval = thh_fill_struct_tracepoint(&thh[i]);
		if (!retval)
			goto out_err;

		retval = tracepoint_probe_register(thh[i].tp, thh[i].probe, NULL);

		if (retval)
			goto out_err;

		thh[i].registered = 1;
	}
	return 0;

out_err:
	thh_unhook_probes(thh, helpers_count);
	return -EINVAL;
}
