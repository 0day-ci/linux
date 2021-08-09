/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2021, Oracle. All rights reserved.
 */

#ifndef _NET_SUNRPC_FAIL_H_
#define _NET_SUNRPC_FAIL_H_

#include <linux/fault-inject.h>

struct fail_sunrpc_attr {
	struct fault_attr	attr;

	bool			ignore_server_disconnect;
	bool			ignore_client_disconnect;
};

extern struct fail_sunrpc_attr fail_sunrpc;

#endif /* _NET_SUNRPC_FAIL_H_ */
