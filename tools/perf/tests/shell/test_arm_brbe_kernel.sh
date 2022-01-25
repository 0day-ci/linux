#!/bin/bash
# Arm64 BRBE kernel branches

# SPDX-License-Identifier: GPL-2.0
# German Gomez <german.gomez@arm.com>, 2022

PERF_DATA=$(mktemp /tmp/__perf_test.perf.data.XXXXX)

cleanup_files() {
	rm -f $PERF_DATA*
}

trap cleanup_files exit term int

test_brbe_kernel() {
	lscpu | grep -q "aarch64" || return $?
	perf record -o $PERF_DATA --branch-filter any,k -- true > /dev/null 2>&1
}

test_brbe_kernel || exit 2

# example perf-script output:
#   0xffffffff9a80dd5e/0xffffffff9a87c5c0/P/-/-/1
#   0xffff8000080ac20c/0xffff800008e99720/P/-/-/2
#   0xffff8000080ac20c/0xffff800008e99720/P/-/-/3

# kernel addresses always have the upper 16 bits set (https://lwn.net/Articles/718895/)
KERNEL_ADDRESS_REGEX="0xffff[0-9a-f]{12}"

perf record -o $PERF_DATA --branch-filter any,k -a -- sleep 1
perf script -i $PERF_DATA --fields brstack | egrep "(0x0|$KERNEL_ADDRESS_REGEX)\/(0x0|$KERNEL_ADDRESS_REGEX)\/" > /dev/null
err=$?

echo -n "BRB kernel branches: "
if [ $err != 0 ]; then
	echo "FAIL"
	exit 1
else
	echo "PASS"
fi

exit 0
