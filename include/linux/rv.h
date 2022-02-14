/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Runtime Verification.
 *
 * For futher information, see: kernel/trace/rv/rv.c.
 *
 * Copyright (C) 2019-2022 Daniel Bristot de Oliveira <bristot@kernel.org>
 */

struct rv_monitor {
	const char		*name;
	const char		*description;
	bool			enabled;
	int			(*start)(void);
	void			(*stop)(void);
	void			(*reset)(void);
};

extern bool monitoring_on;
int rv_unregister_monitor(struct rv_monitor *monitor);
int rv_register_monitor(struct rv_monitor *monitor);
