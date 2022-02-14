/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Runtime Verification.
 *
 * For futher information, see: kernel/trace/rv/rv.c.
 *
 * Copyright (C) 2019-2022 Daniel Bristot de Oliveira <bristot@kernel.org>
 */
struct rv_reactor {
	char			*name;
	char			*description;
	void			(*react)(char *);
};

struct rv_monitor {
	const char		*name;
	const char		*description;
	bool			enabled;
	int			(*start)(void);
	void			(*stop)(void);
	void			(*reset)(void);
	void			(*react)(char *);
};

extern bool monitoring_on;
int rv_unregister_monitor(struct rv_monitor *monitor);
int rv_register_monitor(struct rv_monitor *monitor);

extern bool reacting_on;
int rv_unregister_reactor(struct rv_reactor *reactor);
int rv_register_reactor(struct rv_reactor *reactor);
