/* SPDX-License-Identifier: GPL-2.0+ */
/*
 *  Anonymous tunnels for IPv6
 *
 *  Author:
 *  Justin Iurman <justin.iurman@uliege.be>
 */

#ifndef _NET_TUNNEL6_ANONYMOUS_H
#define _NET_TUNNEL6_ANONYMOUS_H

#include <linux/skbuff.h>

int tunnel6_anonymous_init(void);
void tunnel6_anonymous_exit(void);

int tunnel6_anonymous_register(void);
int tunnel6_anonymous_unregister(void);

bool anonymous66_enabled(struct sk_buff *skb);
int anonymous66_decap(struct sk_buff *skb);

#endif /* _NET_TUNNEL6_ANONYMOUS_H */
