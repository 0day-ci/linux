#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (c) 2015 Oracle and/or its affiliates. All Rights Reserved.
#
# Author: Alexey Kodanev <alexey.kodanev@oracle.com>
# Modified: Naresh Kamboju <naresh.kamboju@linaro.org>

MODULE=0
dev_makeswap=-1
dev_mounted=-1
dev_start=-1
dev_end=-1

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

trap INT

check_prereqs()
{
	local msg="skip all tests:"
	local uid=$(id -u)

	if [ $uid -ne 0 ]; then
		echo $msg must be run as root >&2
		exit $ksft_skip
	fi
}

zram_cleanup()
{
	echo "zram cleanup"
	local i=
	for i in $(seq $dev_start $dev_makeswap); do
		swapoff /dev/zram$i
	done

	for i in $(seq $dev_start $dev_mounted); do
		umount /dev/zram$i
	done

	for i in $(seq $dev_start $dev_end); do
		echo 1 > /sys/block/zram${i}/reset
		rm -rf zram${i}
	done

	for i in $(seq $dev_start $dev_end); do
		echo $i > /sys/class/zram-control/hot_remove
	done
}

zram_unload()
{
	if [ $MODULE -ne 0 ] ; then
		echo "zram rmmod zram"
		rmmod zram > /dev/null 2>&1
	fi
}

zram_load()
{
	echo "create '$dev_num' zram device(s)"

	if [ ! -d "/sys/class/zram-control" ]; then
		modprobe zram num_devices=$dev_num
		if grep -q '^zram' /proc/modules; then
			echo "ERROR: No zram.ko module"
			echo "$TCID : CONFIG_ZRAM is not set"
			exit $ksft_skip
		fi
		MODULE=1
		dev_start=0
		dev_end=$(($dev_num - 1))
		echo "all zram devices(/dev/zram0~$dev_end) successfully created"
		return
	fi

	dev_start=$(ls /dev/zram* | wc -w)
	dev_end=$(($dev_start + $dev_num - 1))

	for i in $(seq $dev_start $dev_end); do
		cat /sys/class/zram-control/hot_add > /dev/null
	done

	echo "all zram devices(/dev/zram$dev_start~$dev_end) successfully created"
}

zram_compress_alg()
{
	echo "test that we can set compression algorithm"

	local algs=$(cat /sys/block/zram0/comp_algorithm)
	echo "supported algs: $algs"
	local i=$dev_start
	for alg in $zram_algs; do
		local sys_path="/sys/block/zram${i}/comp_algorithm"
		echo "$alg" >	$sys_path || \
			echo "FAIL can't set '$alg' to $sys_path"
		i=$(($i + 1))
		echo "$sys_path = '$alg'"
	done

	echo "zram set compression algorithm: OK"
}

zram_set_disksizes()
{
	echo "set disk size to zram device(s)"
	local i=$dev_start
	for ds in $zram_sizes; do
		local sys_path="/sys/block/zram${i}/disksize"
		echo "$ds" >	$sys_path || \
			echo "FAIL can't set '$ds' to $sys_path"

		i=$(($i + 1))
		echo "$sys_path = '$ds'"
	done

	echo "zram set disksizes: OK"
}

zram_set_memlimit()
{
	echo "set memory limit to zram device(s)"

	local i=$dev_start
	for ds in $zram_mem_limits; do
		local sys_path="/sys/block/zram${i}/mem_limit"
		echo "$ds" >	$sys_path || \
			echo "FAIL can't set '$ds' to $sys_path"

		i=$(($i + 1))
		echo "$sys_path = '$ds'"
	done

	echo "zram set memory limit: OK"
}

zram_makeswap()
{
	echo "make swap with zram device(s)"
	local i=0
	for i in $(seq $dev_start $dev_end); do
		mkswap /dev/zram$i > err.log 2>&1
		if [ $? -ne 0 ]; then
			cat err.log
			echo "FAIL mkswap /dev/zram$1 failed"
		fi

		swapon /dev/zram$i > err.log 2>&1
		if [ $? -ne 0 ]; then
			cat err.log
			echo "FAIL swapon /dev/zram$1 failed"
		fi

		echo "done with /dev/zram$i"
		dev_makeswap=$i
	done

	echo "zram making zram mkswap and swapon: OK"
}

zram_swapoff()
{
	local i=
	for i in $(seq $dev_start $dev_makeswap); do
		swapoff /dev/zram$i > err.log 2>&1
		if [ $? -ne 0 ]; then
			cat err.log
			echo "FAIL swapoff /dev/zram$i failed"
		fi
	done
	dev_makeswap=-1

	echo "zram swapoff: OK"
}

zram_makefs()
{
	local i=$dev_start
	for fs in $zram_filesystems; do
		# if requested fs not supported default it to ext2
		which mkfs.$fs > /dev/null 2>&1 || fs=ext2

		echo "make $fs filesystem on /dev/zram$i"
		mkfs.$fs /dev/zram$i > err.log 2>&1
		if [ $? -ne 0 ]; then
			cat err.log
			echo "FAIL failed to make $fs on /dev/zram$i"
		fi
		i=$(($i + 1))
		echo "zram mkfs.$fs: OK"
	done
}

zram_mount()
{
	local i=0
	for i in $(seq $dev_start $dev_end); do
		echo "mount /dev/zram$i"
		mkdir zram$i
		mount /dev/zram$i zram$i > /dev/null || \
			echo "FAIL mount /dev/zram$i failed"
		dev_mounted=$i
	done

	echo "zram mount of zram device(s): OK"
}
