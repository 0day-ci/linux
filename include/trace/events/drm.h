/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM drm

#if !defined(_TRACE_DRM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_DRM_H

#include <linux/tracepoint.h>

/* drm_debug() was called, pass its args */
TRACE_EVENT(drm_debug,
	TP_PROTO(struct _ddebug *desc, struct va_format *vaf),

	TP_ARGS(desc, vaf),

	TP_STRUCT__entry(
		__field(struct _ddebug *, desc)
		__dynamic_array(char, msg, 256)
	),

	TP_fast_assign(
		int len;
		char *p = __get_str(msg);

		__entry->desc = desc;
		len = vsnprintf(p, 256, vaf->fmt, *vaf->va);

		if ((len > 0) && (len < 256) && p[len-1] == '\n')
			len -= 1;
		p[len] = 0;
	),

	TP_printk("%s", __get_str(msg))
);

/* drm_devdbg() was called, pass its args, preserving order */
TRACE_EVENT(drm_devdbg,
	TP_PROTO(const struct device *dev, struct _ddebug *desc, struct va_format *vaf),

	TP_ARGS(dev, desc, vaf),

	TP_STRUCT__entry(
		__field(const struct device *, dev)
		__field(struct _ddebug *, desc)
		__dynamic_array(char, msg, 256)
	),

	TP_fast_assign(
		int len;
		char *p = __get_str(msg);

		__entry->desc = desc;
		__entry->dev = dev;
		len = vsnprintf(p, 256, vaf->fmt, *vaf->va);

		if ((len > 0) && (len < 256) && p[len-1] == '\n')
			len -= 1;
		p[len] = 0;
	),

	TP_printk("cat:%d, %s %s", __entry->desc->class_id,
		  dev_name(__entry->dev), __get_str(msg))
);

#endif /* _TRACE_DRM_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
