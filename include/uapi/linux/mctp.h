/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Management Controller Transport Protocol (MCTP)
 *
 * Copyright (c) 2021 Code Construct
 * Copyright (c) 2021 Google
 */

#ifndef __UAPI_MCTP_H
#define __UAPI_MCTP_H

#include <linux/types.h>

typedef __u8			mctp_eid_t;

struct mctp_addr {
	mctp_eid_t		s_addr;
};

struct sockaddr_mctp {
	unsigned short int	smctp_family;
	int			smctp_network;
	struct mctp_addr	smctp_addr;
	__u8			smctp_type;
	__u8			smctp_tag;
};

#define MCTP_NET_ANY		0x0
#define MCTP_NET_DEFAULT	0x0

#define MCTP_ADDR_ANY		0xff

#endif /* __UAPI_MCTP_H */
