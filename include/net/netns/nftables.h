/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NETNS_NFTABLES_H_
#define _NETNS_NFTABLES_H_

#include <linux/list.h>

struct netns_nftables {
	u8			gencursor;
	atomic_t		count_hw;
	int			max_hw;
	atomic_t		count_wq_add;
	int			max_wq_add;
};

#endif
