/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NETNS_TCP_AUTHOPT_H__
#define __NETNS_TCP_AUTHOPT_H__

#include <linux/mutex.h>

struct netns_tcp_authopt {
	struct hlist_head head;
	struct mutex mutex;
};

#endif /* __NETNS_TCP_AUTHOPT_H__ */
