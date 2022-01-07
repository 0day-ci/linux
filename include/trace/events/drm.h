/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM drm

#if !defined(_TRACE_DRM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_DRM_H

#include <linux/tracepoint.h>

/* drm_debug() was called, pass its args */
TRACE_EVENT(drm_debug,
	TP_PROTO(int drm_debug_category, struct va_format *vaf),

	TP_ARGS(drm_debug_category, vaf),

	TP_STRUCT__entry(
		__field(int, drm_debug_category)
		__dynamic_array(char, msg, 256)
	),

	TP_fast_assign(
		int len;

		__entry->drm_debug_category = drm_debug_category;
		vsnprintf(__get_str(msg), 256, vaf->fmt, *vaf->va);

		len = strlen(__get_str(msg));
		if ((len > 0) && (__get_str(msg)[len-1] == '\n'))
			len -= 1;
		__get_str(msg)[len] = 0;
	),

	TP_printk("%s", __get_str(msg))
);

/* drm_devdbg() was called, pass its args, preserving order */
TRACE_EVENT(drm_devdbg,
	TP_PROTO(const struct device *dev, int drm_debug_category, struct va_format *vaf),

	TP_ARGS(dev, drm_debug_category, vaf),

	TP_STRUCT__entry(
		__field(const struct device*, dev)
		__field(int, drm_debug_category)
		__dynamic_array(char, msg, 256)
	),

	TP_fast_assign(
		int len;

		__entry->drm_debug_category = drm_debug_category;
		__entry->dev = dev;
		vsnprintf(__get_str(msg), 256, vaf->fmt, *vaf->va);

		len = strlen(__get_str(msg));
		if ((len > 0) && (__get_str(msg)[len-1] == '\n'))
			len -= 1;
		__get_str(msg)[len] = 0;
	),

	TP_printk("cat:%d, %s %s", __entry->drm_debug_category,
		  dev_name(__entry->dev), __get_str(msg))
);

#endif /* _TRACE_DRM_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
