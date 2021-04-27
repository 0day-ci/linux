/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2021 Xilinx, Inc.
 *
 * Authors:
 *	Cheng Zhen <maxz@xilinx.com>
 */

#ifndef _XRT_EVENTS_H_
#define _XRT_EVENTS_H_

#include "subdev_id.h"

/*
 * Event notification.
 */
enum xrt_events {
	XRT_EVENT_TEST = 0, /* for testing */
	/*
	 * Events related to specific subdev
	 * Callback arg: struct xrt_event_arg_subdev
	 */
	XRT_EVENT_POST_CREATION,
	XRT_EVENT_PRE_REMOVAL,
	/*
	 * Events related to change of the whole board
	 * Callback arg: <none>
	 */
	XRT_EVENT_PRE_HOT_RESET,
	XRT_EVENT_POST_HOT_RESET,
	XRT_EVENT_PRE_GATE_CLOSE,
	XRT_EVENT_POST_GATE_OPEN,
};

struct xrt_event_arg_subdev {
	enum xrt_subdev_id xevt_subdev_id;
	int xevt_subdev_instance;
};

struct xrt_event {
	enum xrt_events xe_evt;
	struct xrt_event_arg_subdev xe_subdev;
};

#endif	/* _XRT_EVENTS_H_ */
