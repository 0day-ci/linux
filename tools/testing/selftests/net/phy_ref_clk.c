// SPDX-License-Identifier: GPL-2.0-only
/*
 * This program allows to test behavior of netdev after sending
 * SyncE related ioctl: SIOCGSYNCE and SIOCSSYNCE.
 * SIOCGSYNCE - was designed to check how output pin on PHY port
 * was configured.
 * SIOCSSYNCE - was designed to configure (enable or disable)
 * one of the pins, that PHY can propagate its recovered clock
 * signal onto.
 *
 * Copyright (C) 2021 Intel Corporation.
 * Author: Arkadiusz Kubalewski <arkadiusz.kubalewski@intel.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <net/if.h>

#include <asm/types.h>
#include <linux/sockios.h>
#include <linux/net_synce.h>

static void usage(const char *error)
{
	if (error)
		printf("invalid: %s\n\n", error);
	printf("phy_ref_clk <interface> <pin_id> [enable]\n\n"
		"Enable or disable phy-recovered reference clock signal on given output pin.\n"
		"Depending on HW configuration, phy recovered clock may be enabled\n"
		"or disabled on one of output pins which are at hardware's disposal\n\n"
		"Params:\n"
		" <interface> - name of netdev implementing SIOCGSYNCE and SIOCSSYNCE\n"
		" <pin_id> - pin on which clock recovered from PHY shall be propagated\n"
		"    (0-X), X - number of output pins at HW disposal\n"
		" In case no other arguments are given, ask the driver\n"
		" for the current config of recovered clock on the interface.\n\n"
		" [enable] - if pin shal be enabled or disabled (0/1)\n\n");
	exit(1);
}

static int get_ref_clk(const char *ifname, __u8 pin)
{
	struct synce_ref_clk_cfg ref_clk;
	struct ifreq ifdata;
	int sd, rc;

	if (!ifname || *ifname == '\0')
		return -1;

	memset(&ifdata, 0, sizeof(ifdata));

	strncpy(ifdata.ifr_name, ifname, IFNAMSIZ);

	sd = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
	if (sd < 0) {
		printf("socket failed\n");
		return -1;
	}

	ref_clk.pin_id = pin;
	ifdata.ifr_data = (void *)&ref_clk;

	rc = ioctl(sd, SIOCGSYNCE, (char *)&ifdata);
	close(sd);
	if (rc != 0) {
		printf("ioctl(SIOCGSYNCE) failed\n");
		return rc;
	}
	printf("GET: pin %u is %s\n",
		ref_clk.pin_id, ref_clk.enable ? "enabled" : "disabled");

	return 0;
}

static int set_ref_clk(const char *ifname, __u8 pin, _Bool enable)
{
	struct synce_ref_clk_cfg ref_clk;
	struct ifreq ifdata;
	int sd, rc;

	if (!ifname || *ifname == '\0')
		return -1;

	memset(&ifdata, 0, sizeof(ifdata));

	strcpy(ifdata.ifr_name, ifname);

	sd = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
	if (sd < 0) {
		printf("socket failed\n");
		return -1;
	}

	ref_clk.pin_id = pin;
	ref_clk.enable = enable;
	ifdata.ifr_data = (void *)&ref_clk;

	rc = ioctl(sd, SIOCSSYNCE, (char *)&ifdata);
	close(sd);
	if (rc != 0) {
		printf("ioctl(SIOCSSYNCE) failed\n");
		return rc;
	}
	printf("SET: pin %u is %s",
	       ref_clk.pin_id, ref_clk.enable ? "enabled" : "disabled");

	return 0;
}

int main(int argc, char **argv)
{
	_Bool enable;
	__u8 pin;
	int ret;

	if (argc > 4 || argc < 3)
		usage("argument count");

	ret = sscanf(argv[2], "%u", &pin);
	if (ret != 1)
		usage(argv[2]);

	if (argc == 3) {
		ret = get_ref_clk(argv[1], pin);
	} else if (argc == 4) {
		ret = sscanf(argv[3], "%u", &enable);
		if (ret != 1)
			usage(argv[3]);
		ret = set_ref_clk(argv[1], pin, enable);
	}
	return ret;
}
