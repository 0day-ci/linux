/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Sam Ravnborg
 */

#ifndef __LINUX_DRM_MEDIA_BUS_FORMAT
#define __LINUX_DRM_MEDIA_BUS_FORMAT

#include <linux/bug.h>
#include <linux/media-bus-format.h>
#include <linux/types.h>

/**
 * media_bus_format_to_bpc - The bits per color channel for the bus_format
 *
 * Based on the supplied bus_format return the maximum number of bits
 * per color channel.
 *
 * RETURNS
 * The number of bits per color channel, or -EINVAL if the bus_format
 * is unknown.
 */
static inline int media_bus_format_to_bpc(u32 bus_format)
{
	switch (bus_format) {
	/* DPI */
	case MEDIA_BUS_FMT_RGB565_1X16:
	case MEDIA_BUS_FMT_RGB666_1X18:
		return 6;

	/* DPI */
	case MEDIA_BUS_FMT_RGB888_1X24:
	case MEDIA_BUS_FMT_RGB888_3X8:
	case MEDIA_BUS_FMT_RGB888_3X8_DELTA:
	case MEDIA_BUS_FMT_Y8_1X8:
		return 8;

     	/* LVDS */
	case MEDIA_BUS_FMT_RGB666_1X7X3_SPWG:
		return 6;

     	/* LVDS */
	case MEDIA_BUS_FMT_RGB888_1X7X4_JEIDA:
	case MEDIA_BUS_FMT_RGB888_1X7X4_SPWG:
		return 8;

	default:
		WARN(1, "Unknown MEDIA_BUS format %d\n", bus_format);
		return -EINVAL;
	}
}

#endif
