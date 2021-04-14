#!/bin/sh
# SPDX-License-Identifier: LGPL-2.1

if [ $# -ne 1 ] ; then
	linux_header_dir=tools/include/uapi/linux
else
	linux_header_dir=$1
fi

linux_mount=${linux_header_dir}/mount.h

printf "static const char *fsconfig_cmds[] = {\n"
regex='^[[:space:]]*FSCONFIG_([[:alnum:]_]+)[[:space:]]*=[[:space:]]*([[:digit:]]+)[[:space:]]*,.*'
sed -nr "s/$regex/\t[\2] = \"\1\",/p" ${linux_mount}
printf "};\n"
