/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NETNS_NFTABLES_H_
#define _NETNS_NFTABLES_H_

#include <linux/list.h>

struct netns_nftables {
	u8			gencursor;
	atomic_t		count_hw;
};

#endif
