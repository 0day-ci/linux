// SPDX-License-Identifier: GPL-2.0
/*
 * Landlock LSM - Network management and hooks
 *
 * Copyright (C) 2022 Huawei Tech. Co., Ltd.
 * Author: Konstantin Meskhidze <konstantin.meskhidze@huawei.com>
 *
 */

#include <linux/in.h>
#include <linux/net.h>
#include <linux/socket.h>
#include <net/ipv6.h>

#include "cred.h"
#include "limits.h"
#include "net.h"

int landlock_append_net_rule(struct landlock_ruleset *const ruleset,
			     u16 port, u32 access_rights)
{
	int err;

	/* Transforms relative access rights to absolute ones. */
	access_rights |= LANDLOCK_MASK_ACCESS_NET &
			 ~landlock_get_net_access_mask(ruleset, 0);

	mutex_lock(&ruleset->lock);
	err = landlock_insert_rule(ruleset, NULL, (uintptr_t)port, access_rights,
				   LANDLOCK_RULE_NET_SERVICE);
	mutex_unlock(&ruleset->lock);

	return err;
}

static int check_socket_access(const struct landlock_ruleset *const domain,
			       u16 port, u32 access_request)
{
	bool allowed = false;
	u64 layer_mask;
	size_t i;

	/* Make sure all layers can be checked. */
	BUILD_BUG_ON(BITS_PER_TYPE(layer_mask) < LANDLOCK_MAX_NUM_LAYERS);

	if (WARN_ON_ONCE(!domain))
		return 0;
	if (WARN_ON_ONCE(domain->num_layers < 1))
		return -EACCES;

	/*
	 * Saves all layers handling a subset of requested
	 * socket access rules.
	 */
	layer_mask = 0;
	for (i = 0; i < domain->num_layers; i++) {
		if (landlock_get_net_access_mask(domain, i) & access_request)
			layer_mask |= BIT_ULL(i);
	}
	/* An access request not handled by the domain is allowed. */
	if (layer_mask == 0)
		return 0;

	/*
	 * We need to walk through all the hierarchy to not miss any relevant
	 * restriction.
	 */
	layer_mask = landlock_unmask_layers(domain, NULL, port,
					    access_request, layer_mask,
					    LANDLOCK_RULE_NET_SERVICE);
	if (layer_mask == 0)
		allowed = true;

	return allowed ? 0 : -EACCES;
}

static int hook_socket_bind(struct socket *sock, struct sockaddr *address, int addrlen)
{
#if IS_ENABLED(CONFIG_INET)
	short socket_type;
	struct sockaddr_in *sockaddr;
	struct sockaddr_in6 *sockaddr_ip6;
	u16 port;
	const struct landlock_ruleset *const dom = landlock_get_current_domain();

	if (!dom)
		return 0;

	/* Check if the hook is AF_INET* socket's action */
	if ((address->sa_family != AF_INET) && (address->sa_family != AF_INET6))
		return 0;

	socket_type = sock->type;
	/* Check if it's a TCP socket */
	if (socket_type != SOCK_STREAM)
		return 0;

	/* Get port value in host byte order */
	switch (address->sa_family) {
	case AF_INET:
		sockaddr = (struct sockaddr_in *)address;
		port = ntohs(sockaddr->sin_port);
		break;
	case AF_INET6:
		sockaddr_ip6 = (struct sockaddr_in6 *)address;
		port = ntohs(sockaddr_ip6->sin6_port);
		break;
	}

	return check_socket_access(dom, port, LANDLOCK_ACCESS_NET_BIND_TCP);
#else
	return 0;
#endif
}

static int hook_socket_connect(struct socket *sock, struct sockaddr *address, int addrlen)
{
#if IS_ENABLED(CONFIG_INET)
	short socket_type;
	struct sockaddr_in *sockaddr;
	struct sockaddr_in6 *sockaddr_ip6;
	u16 port;
	const struct landlock_ruleset *const dom = landlock_get_current_domain();

	if (!dom)
		return 0;

	/* Check if the hook is AF_INET* socket's action */
	if ((address->sa_family != AF_INET) && (address->sa_family != AF_INET6)) {
		/* Check if the socket_connect() hook has AF_UNSPEC flag*/
		if (address->sa_family == AF_UNSPEC) {
			u16 i;
			/*
			 * If just in a layer a mask supports connect access,
			 * the socket_connect() hook with AF_UNSPEC family flag
			 * must be banned. This prevents from disconnecting already
			 * connected sockets.
			 */
			for (i = 0; i < dom->num_layers; i++) {
				if (landlock_get_net_access_mask(dom, i) &
							LANDLOCK_ACCESS_NET_CONNECT_TCP)
					return -EACCES;
			}
		}
		return 0;
	}

	socket_type = sock->type;
	/* Check if it's a TCP socket */
	if (socket_type != SOCK_STREAM)
		return 0;

	/* Get port value in host byte order */
	switch (address->sa_family) {
	case AF_INET:
		sockaddr = (struct sockaddr_in *)address;
		port = ntohs(sockaddr->sin_port);
		break;
	case AF_INET6:
		sockaddr_ip6 = (struct sockaddr_in6 *)address;
		port = ntohs(sockaddr_ip6->sin6_port);
		break;
	}

	return check_socket_access(dom, port, LANDLOCK_ACCESS_NET_CONNECT_TCP);
#else
	return 0;
#endif
}

static struct security_hook_list landlock_hooks[] __lsm_ro_after_init = {
	LSM_HOOK_INIT(socket_bind, hook_socket_bind),
	LSM_HOOK_INIT(socket_connect, hook_socket_connect),
};

__init void landlock_add_net_hooks(void)
{
	security_add_hooks(landlock_hooks, ARRAY_SIZE(landlock_hooks),
			LANDLOCK_NAME);
}
