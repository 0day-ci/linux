/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  V4L2 sub-device legacy support header.
 *
 *  Copyright (C) 2022  Tomi Valkeinen <tomi.valkeinen@ideasonboard.com>
 */

#ifndef _V4L2_SUBDEV_LEGACY_H
#define _V4L2_SUBDEV_LEGACY_H

/**
 * v4l2_subdev_call_state_active - call an operation of a v4l2_subdev which
 *				   takes state as a parameter, passing the
 *				   subdev its active state.
 *
 * @sd: pointer to the &struct v4l2_subdev
 * @o: name of the element at &struct v4l2_subdev_ops that contains @f.
 *     Each element there groups a set of callbacks functions.
 * @f: callback function to be called.
 *     The callback functions are defined in groups, according to
 *     each element at &struct v4l2_subdev_ops.
 * @args: arguments for @f.
 *
 * This is similar to v4l2_subdev_call(), except that this version can only be
 * used for ops that take a subdev state as a parameter. The macro will get the
 * active state and lock it before calling the op, and unlock it after the
 * call.
 */
#define v4l2_subdev_call_state_active(sd, o, f, args...)		\
	({								\
		int __result;						\
		struct v4l2_subdev_state *state;			\
		state = v4l2_subdev_get_active_state(sd);		\
		if (state)						\
			v4l2_subdev_lock_state(state);			\
		__result = v4l2_subdev_call(sd, o, f, state, ##args);	\
		if (state)						\
			v4l2_subdev_unlock_state(state);		\
		__result;						\
	})

#endif
