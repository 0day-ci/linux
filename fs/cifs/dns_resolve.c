/*
 *  fs/cifs/dns_resolve.c
 *
 *   Copyright (c) 2007 Igor Mammedov
 *   Author(s): Igor Mammedov (niallain@gmail.com)
 *              Steve French (sfrench@us.ibm.com)
 *              Wang Lei (wang840925@gmail.com)
 *		David Howells (dhowells@redhat.com)
 *
 *   Contains the CIFS DFS upcall routines used for hostname to
 *   IP address translation.
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published
 *   by the Free Software Foundation; either version 2.1 of the License, or
 *   (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/slab.h>
#include <linux/dns_resolver.h>
#include "dns_resolve.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_debug.h"

static int iplist_to_addrs(char *ips, struct sockaddr_storage *addrs, int maxaddrs)
{
	char *ip;
	int count = 0;

	while ((ip = strsep(&ips, ",")) && count < maxaddrs) {
		struct sockaddr_storage addr;

		if (!*ip)
			break;

		cifs_dbg(FYI, "%s: add \'%s\' to the list of ip addresses\n", __func__, ip);
		if (cifs_convert_address((struct sockaddr *)&addr, ip, strlen(ip)) > 0)
			addrs[count++] = addr;
	}
	return count;
}

/**
 * dns_resolve_server_name_to_addrs - Resolve UNC server name to a list of addresses.
 * @unc: UNC path specifying the server (with '/' as delimiter)
 * @addrs: Where to return the list of addresses.
 * @maxaddrs: Maximum number of addresses.
 *
 * Returns the number of resolved addresses, otherwise a negative error number.
 */
int dns_resolve_server_name_to_addrs(const char *unc, struct sockaddr_storage *addrs, int maxaddrs)
{
	struct sockaddr_storage ss;
	const char *hostname, *sep;
	char *ips;
	int len, rc;

	if (!addrs || !maxaddrs || !unc)
		return -EINVAL;

	len = strlen(unc);
	if (len < 3) {
		cifs_dbg(FYI, "%s: unc is too short: %s\n", __func__, unc);
		return -EINVAL;
	}

	/* Discount leading slashes for cifs */
	len -= 2;
	hostname = unc + 2;

	/* Search for server name delimiter */
	sep = memchr(hostname, '/', len);
	if (sep)
		len = sep - hostname;
	else
		cifs_dbg(FYI, "%s: probably server name is whole unc: %s\n",
			 __func__, unc);

	/* Try to interpret hostname as an IPv4 or IPv6 address */
	rc = cifs_convert_address((struct sockaddr *)&ss, hostname, len);
	if (rc > 0) {
		cifs_dbg(FYI, "%s: unc is IP, skipping dns upcall: %s\n", __func__, hostname);
		addrs[0] = ss;
		return 1;
	}

	/* Perform the upcall */
	rc = dns_query(current->nsproxy->net_ns, NULL, hostname, len, "list", &ips, NULL, false);
	if (rc < 0) {
		cifs_dbg(FYI, "%s: unable to resolve: %*.*s\n", __func__, len, len, hostname);
		return rc;
	}
	cifs_dbg(FYI, "%s: resolved: %*.*s to %s\n", __func__, len, len, hostname, ips);

	rc = iplist_to_addrs(ips, addrs, maxaddrs);
	cifs_dbg(FYI, "%s: num of resolved ips: %d\n", __func__, rc);
	kfree(ips);

	return rc;
}
