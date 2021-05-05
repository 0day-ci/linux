#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

if [ $# -lt 2 ]; then
	echo -e "syntax:\n$0 <device> <attr> [<value>]"
	exit 0
fi

mount -t sysfs /sys 2>/dev/null
if [ $# -lt 3 ]; then
	cat /sys/class/net/$1/$2
	exit $?
fi

echo $3 > /sys/class/net/$1/$2
