#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2021 Luis Chamberlain <mcgrof@kernel.org>
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 2 of the License, or at your option any
# later version; or, when distributed separately from the Linux kernel or
# when incorporated into other software packages, subject to the following
# license:
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of copyleft-next (version 0.3.1 or later) as published
# at http://copyleft-next.org/.

# This performs a series tests against the sysfs filesystem.

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

TEST_NAME="sysfs"
TEST_DRIVER="test_${TEST_NAME}"
TEST_DIR=$(dirname $0)
TEST_FILE=$(mktemp)

# This represents
#
# TEST_ID:TEST_COUNT:ENABLED:TARGET
#
# TEST_ID: is the test id number
# TEST_COUNT: number of times we should run the test
# ENABLED: 1 if enabled, 0 otherwise
# TARGET: test target file required on the test_sysfs module
#
# Once these are enabled please leave them as-is. Write your own test,
# we have tons of space.
ALL_TESTS="0001:3:1:test_dev_x:misc"
ALL_TESTS="$ALL_TESTS 0002:3:1:test_dev_y:misc"
ALL_TESTS="$ALL_TESTS 0003:3:1:test_dev_x:misc"
ALL_TESTS="$ALL_TESTS 0004:3:1:test_dev_y:misc"
ALL_TESTS="$ALL_TESTS 0005:1:1:test_dev_x:misc"
ALL_TESTS="$ALL_TESTS 0006:1:1:test_dev_y:misc"
ALL_TESTS="$ALL_TESTS 0007:1:1:test_dev_y:misc"
ALL_TESTS="$ALL_TESTS 0008:1:1:test_dev_x:misc"
ALL_TESTS="$ALL_TESTS 0009:1:1:test_dev_y:misc"
ALL_TESTS="$ALL_TESTS 0010:1:1:test_dev_y:misc"
ALL_TESTS="$ALL_TESTS 0011:1:1:test_dev_x:misc"
ALL_TESTS="$ALL_TESTS 0012:1:1:test_dev_y:misc"
ALL_TESTS="$ALL_TESTS 0013:1:1:test_dev_y:misc"
ALL_TESTS="$ALL_TESTS 0014:3:1:test_dev_x:block" # block equivalent set
ALL_TESTS="$ALL_TESTS 0015:3:1:test_dev_x:block"
ALL_TESTS="$ALL_TESTS 0016:3:1:test_dev_x:block"
ALL_TESTS="$ALL_TESTS 0017:3:1:test_dev_y:block"
ALL_TESTS="$ALL_TESTS 0018:1:1:test_dev_x:block"
ALL_TESTS="$ALL_TESTS 0019:1:1:test_dev_y:block"
ALL_TESTS="$ALL_TESTS 0020:1:1:test_dev_y:block"
ALL_TESTS="$ALL_TESTS 0021:1:1:test_dev_x:block"
ALL_TESTS="$ALL_TESTS 0022:1:1:test_dev_y:block"
ALL_TESTS="$ALL_TESTS 0023:1:1:test_dev_y:block"
ALL_TESTS="$ALL_TESTS 0024:1:1:test_dev_x:block"
ALL_TESTS="$ALL_TESTS 0025:1:1:test_dev_y:block"
ALL_TESTS="$ALL_TESTS 0026:1:1:test_dev_y:block"
ALL_TESTS="$ALL_TESTS 0027:1:1:test_dev_x:block" # deadlock test
ALL_TESTS="$ALL_TESTS 0028:1:1:test_dev_x:block" # deadlock test with rntl_lock
ALL_TESTS="$ALL_TESTS 0029:1:1:test_dev_x:block" # kernfs race removal of store
ALL_TESTS="$ALL_TESTS 0030:1:1:test_dev_x:block" # kernfs race removal before mutex
ALL_TESTS="$ALL_TESTS 0031:1:1:test_dev_x:block" # kernfs race removal after mutex
ALL_TESTS="$ALL_TESTS 0032:1:1:test_dev_x:block" # kernfs race removal after active

allow_user_defaults()
{
	if [ -z $DIR ]; then
		case $TEST_DEV_TYPE in
		misc)
			DIR="/sys/devices/virtual/misc/${TEST_DRIVER}0"
			;;
		block)
			DIR="/sys/devices/virtual/block/${TEST_DRIVER}0"
			;;
		*)
			DIR="/sys/devices/virtual/misc/${TEST_DRIVER}0"
			;;
		esac
	fi
	case $TEST_DEV_TYPE in
		misc)
			MODPROBE_TESTDEV_TYPE=""
			;;
		block)
			MODPROBE_TESTDEV_TYPE="devtype=1"
			;;
		*)
			MODPROBE_TESTDEV_TYPE=""
			;;
	esac
	if [ -z $SYSFS_DEBUGFS_DIR ]; then
		SYSFS_DEBUGFS_DIR="/sys/kernel/debug/test_sysfs"
	fi
	if [ -z $KERNFS_DEBUGFS_DIR ]; then
		KERNFS_DEBUGFS_DIR="/sys/kernel/debug/kernfs"
	fi
	if [ -z $PAGE_SIZE ]; then
		PAGE_SIZE=$(getconf PAGESIZE)
	fi
	if [ -z $MAX_DIGITS ]; then
		MAX_DIGITS=$(($PAGE_SIZE/8))
	fi
	if [ -z $INT_MAX ]; then
		INT_MAX=$(getconf INT_MAX)
	fi
	if [ -z $UINT_MAX ]; then
		UINT_MAX=$(getconf UINT_MAX)
	fi
}

test_reqs()
{
	uid=$(id -u)
	if [ $uid -ne 0 ]; then
		echo $msg must be run as root >&2
		exit $ksft_skip
	fi

	if ! which modprobe 2> /dev/null > /dev/null; then
		echo "$0: You need modprobe installed" >&2
		exit $ksft_skip
	fi
	if ! which getconf 2> /dev/null > /dev/null; then
		echo "$0: You need getconf installed"
		exit $ksft_skip
	fi
	if ! which diff 2> /dev/null > /dev/null; then
		echo "$0: You need diff installed"
		exit $ksft_skip
	fi
	if ! which perl 2> /dev/null > /dev/null; then
		echo "$0: You need perl installed"
		exit $ksft_skip
	fi
}

call_modprobe()
{
	modprobe $TEST_DRIVER $MODPROBE_TESTDEV_TYPE $FIRST_MODPROBE_ARGS $MODPROBE_ARGS
	return $?
}

modprobe_reset()
{
	modprobe -q -r $TEST_DRIVER
	call_modprobe
	return $?
}

modprobe_reset_enable_debugfs()
{
	FIRST_MODPROBE_ARGS="enable_debugfs=1"
	modprobe_reset
	unset FIRST_MODPROBE_ARGS
}

modprobe_reset_enable_lock_on_rmmod()
{
	FIRST_MODPROBE_ARGS="enable_lock=1 enable_lock_on_rmmod=1 enable_verbose_writes=1"
	modprobe_reset
	unset FIRST_MODPROBE_ARGS
}

modprobe_reset_enable_rtnl_lock_on_rmmod()
{
	FIRST_MODPROBE_ARGS="enable_lock=1 use_rtnl_lock=1 enable_lock_on_rmmod=1"
	FIRST_MODPROBE_ARGS="$FIRST_MODPROBE_ARGS enable_verbose_writes=1"
	modprobe_reset
	unset FIRST_MODPROBE_ARGS
}

modprobe_reset_enable_completion()
{
	FIRST_MODPROBE_ARGS="enable_completion_on_rmmod=1 enable_verbose_writes=1"
	FIRST_MODPROBE_ARGS="$FIRST_MODPROBE_ARGS enable_verbose_rmmod=1 delay_rmmod_ms=0"
	modprobe_reset
	unset FIRST_MODPROBE_ARGS
}

load_req_mod()
{
	modprobe_reset
	if [ ! -d $DIR ]; then
		if ! modprobe -q -n $TEST_DRIVER; then
			echo "$0: module $TEST_DRIVER not found [SKIP]"
			echo "You must set CONFIG_TEST_SYSFS=m in your kernel" >&2
			exit $ksft_skip
		fi
		call_modprobe
		if [ $? -ne 0 ]; then
			echo "$0: modprobe $TEST_DRIVER failed."
			exit
		fi
	fi
}

config_reset()
{
	if ! echo -n "1" >"$DIR"/reset; then
		echo "$0: reset should have worked" >&2
		exit 1
	fi
}

debugfs_reset_first_test_dev_ignore_errors()
{
	echo -n "1" >"$SYSFS_DEBUGFS_DIR"/reset_first_test_dev
}

debugfs_kernfs_kernfs_fop_write_iter_exists()
{
	KNOB_DIR="${KERNFS_DEBUGFS_DIR}/config_fail_kernfs_fop_write_iter"
	if [[ ! -d $KNOB_DIR ]]; then
		echo "kernfs debugfs does not exist $KNOB_DIR"
		return 0;
	fi
	KNOB_DEBUGFS="${KERNFS_DEBUGFS_DIR}/fail_kernfs_fop_write_iter"
	if [[ ! -d $KNOB_DEBUGFS ]]; then
		echo -n "kernfs debugfs for coniguring fail_kernfs_fop_write_iter "
		echo "does not exist $KNOB_DIR"
		return 0;
	fi
	return 1
}

debugfs_kernfs_kernfs_fop_write_iter_set_fail_once()
{
	KNOB_DEBUGFS="${KERNFS_DEBUGFS_DIR}/fail_kernfs_fop_write_iter"
	echo 1 > $KNOB_DEBUGFS/interval
	echo 100 > $KNOB_DEBUGFS/probability
	echo 0 > $KNOB_DEBUGFS/space
	# Disable verbose messages on the kernel ring buffer which may
	# confuse developers with a kernel panic.
	echo 0 > $KNOB_DEBUGFS/verbose

	# Fail only once
	echo 1 > $KNOB_DEBUGFS/times
}

debugfs_kernfs_kernfs_fop_write_iter_set_fail_never()
{
	KNOB_DEBUGFS="${KERNFS_DEBUGFS_DIR}/fail_kernfs_fop_write_iter"
	echo 0 > $KNOB_DEBUGFS/times
}

debugfs_kernfs_set_wait_ms()
{
	SLEEP_AFTER_WAIT_MS="${KERNFS_DEBUGFS_DIR}/sleep_after_wait_ms"
	echo $1 > $SLEEP_AFTER_WAIT_MS
}

debugfs_kernfs_disable_wait_kernfs_fop_write_iter()
{
	ENABLE_WAIT_KNOB="${KERNFS_DEBUGFS_DIR}/config_fail_kernfs_fop_write_iter/wait_"
	for KNOB in ${ENABLE_WAIT_KNOB}*; do
		echo 0 > $KNOB
	done
}

debugfs_kernfs_enable_wait_kernfs_fop_write_iter()
{
	ENABLE_WAIT_KNOB="${KERNFS_DEBUGFS_DIR}/config_fail_kernfs_fop_write_iter/wait_$1"
	echo -n "1" > $ENABLE_WAIT_KNOB
	return $?
}

set_orig()
{
	if [[ ! -z $TARGET ]] && [[ ! -z $ORIG ]]; then
		if [ -f ${TARGET} ]; then
			echo "${ORIG}" > "${TARGET}"
		fi
	fi
}

set_test()
{
	echo "${TEST_STR}" > "${TARGET}"
}

set_test_ignore_errors()
{
	echo "${TEST_STR}" > "${TARGET}" 2> /dev/null
}

verify()
{
	local seen
	seen=$(cat "$1")
	target_short=$(basename $TARGET)
	case $target_short in
	test_dev_x)
		if [ "${seen}" != "${TEST_STR}" ]; then
			return 1
		fi
		;;
	test_dev_y)
		DIRNAME=$(dirname $1)
		EXPECTED_RESULT=""
		# If our target was the test file then what we write to it
		# is the same as what that we expect when we read from it.
		# When we write to test_dev_y directly though we expect
		# a computed value which is driver specific.
		if [[ "$DIRNAME" == "/tmp" ]]; then
			let EXPECTED_RESULT="${TEST_STR}"
		else
			x=$(cat ${DIR}/test_dev_x)
			let EXPECTED_RESULT="$x+${TEST_STR}+7"
		fi

		if [[ "${seen}" != "${EXPECTED_RESULT}" ]]; then
			return 1
		fi
		;;
	*)
		echo "Unsupported target type update test script: $target_short"
		exit 1
	esac
	return 0
}

verify_diff_w()
{
	echo "$TEST_STR" | diff -q -w -u - $1 > /dev/null
	return $?
}

test_rc()
{
	if [[ $rc != 0 ]]; then
		echo "Failed test, return value: $rc" >&2
		exit $rc
	fi
}

test_finish()
{
	set_orig
	rm -f "${TEST_FILE}"

	if [ ! -z ${old_strict} ]; then
		echo ${old_strict} > ${WRITES_STRICT}
	fi
	exit $rc
}

# kernfs requires us to write everything we want in one shot because
# There is no easy way for us to know if userspace is only doing a partial
# write, so we don't support them. We expect the entire buffer to come on
# the first write.  If you're writing a value, first read the file,
# modify only the value you're changing, then write entire buffer back.
# Since we are only testing digits we just full single writes and old stuff.
# For more details, refer to kernfs_fop_write_iter().
run_numerictests_single_write()
{
	echo "== Testing sysfs behavior against ${TARGET} =="

	rc=0

	echo -n "Writing test file ... "
	echo "${TEST_STR}" > "${TEST_FILE}"
	if ! verify "${TEST_FILE}"; then
		echo "FAIL" >&2
		exit 1
	else
		echo "ok"
	fi

	echo -n "Checking the sysfs file is not set to test value ... "
	if verify "${TARGET}"; then
		echo "FAIL" >&2
		exit 1
	else
		echo "ok"
	fi

	echo -n "Writing to sysfs file from shell ... "
	set_test
	if ! verify "${TARGET}"; then
		echo "FAIL" >&2
		exit 1
	else
		echo "ok"
	fi

	echo -n "Resetting sysfs file to original value ... "
	set_orig
	if verify "${TARGET}"; then
		echo "FAIL" >&2
		exit 1
	else
		echo "ok"
	fi

	# Now that we've validated the sanity of "set_test" and "set_orig",
	# we can use those functions to set starting states before running
	# specific behavioral tests.

	echo -n "Writing to the entire sysfs file in a single write ... "
	set_orig
	dd if="${TEST_FILE}" of="${TARGET}" bs=4096 2>/dev/null
	if ! verify "${TARGET}"; then
		echo "FAIL" >&2
		rc=1
	else
		echo "ok"
	fi

	echo -n "Writing to the sysfs file with multiple long writes ... "
	set_orig
	(perl -e 'print "A" x 50;'; echo "${TEST_STR}") | \
		dd of="${TARGET}" bs=50 2>/dev/null
	if verify "${TARGET}"; then
		echo "FAIL" >&2
		rc=1
	else
		echo "ok"
	fi
	test_rc
}

reset_vals()
{
	echo -n 3 > $DIR/test_dev_x
	echo -n 4 > $DIR/test_dev_x
}

check_failure()
{
	echo -n "Testing that $1 fails as expected..."
	reset_vals
	TEST_STR="$1"
	orig="$(cat $TARGET)"
	echo -n "$TEST_STR" > $TARGET 2> /dev/null

	# write should fail and $TARGET should retain its original value
	if [ $? = 0 ] || [ "$(cat $TARGET)" != "$orig" ]; then
		echo "FAIL" >&2
		rc=1
	else
		echo "ok"
	fi
	test_rc
}

load_modreqs()
{
	export TEST_DEV_TYPE=$(get_test_type $1)
	unset DIR
	allow_user_defaults
	load_req_mod
}

target_exists()
{
	TARGET="${DIR}/$1"
	TEST_ID="$2"

	if [ ! -f ${TARGET} ] ; then
		echo "Target for test $TEST_ID: $TARGET does not exist, skipping test ..."
		return 0
	fi
	return 1
}

config_enable_lock()
{
	if ! echo -n 1 > $DIR/config_enable_lock; then
		echo "$0: Unable to enable locks" >&2
		exit 1
	fi
}

config_write_delay_msec_y()
{
	if ! echo -n $1 > $DIR/config_write_delay_msec_y ; then
		echo "$0: Unable to set write_delay_msec_y to $1" >&2
		exit 1
	fi
}

# Default filter for dmesg scanning.
# Ignore lockdep complaining about its own bugginess when scanning dmesg
# output, because we shouldn't be failing filesystem tests on account of
# lockdep.
_check_dmesg_filter()
{
	egrep -v -e "BUG: MAX_LOCKDEP_CHAIN_HLOCKS too low" \
		-e "BUG: MAX_STACK_TRACE_ENTRIES too low"
}

check_dmesg()
{
	# filter out intentional WARNINGs or Oopses
	local filter=${1:-_check_dmesg_filter}

	_dmesg_since_test_start | $filter >$seqres.dmesg
	egrep -q -e "kernel BUG at" \
	     -e "WARNING:" \
	     -e "\bBUG:" \
	     -e "Oops:" \
	     -e "possible recursive locking detected" \
	     -e "Internal error" \
	     -e "(INFO|ERR): suspicious RCU usage" \
	     -e "INFO: possible circular locking dependency detected" \
	     -e "general protection fault:" \
	     -e "BUG .* remaining" \
	     -e "UBSAN:" \
	     $seqres.dmesg
	if [ $? -eq 0 ]; then
		echo "something found in dmesg (see $seqres.dmesg)"
		return 1
	else
		if [ "$KEEP_DMESG" != "yes" ]; then
			rm -f $seqres.dmesg
		fi
		return 0
	fi
}

log_kernel_fstest_dmesg()
{
	export FSTYP="$1"
	export seqnum="$FSTYP/$2"
	export date_time=$(date +"%F %T")
	echo "run fstests $seqnum at $date_time" > /dev/kmsg
}

modprobe_loop()
{
	while true; do
		call_modprobe > /dev/null 2>&1
		modprobe -r $TEST_DRIVER > /dev/null 2>&1
	done > /dev/null 2>&1
}

write_loop()
{
	while true; do
		set_test_ignore_errors > /dev/null 2>&1
		TEST_STR=$(( $TEST_STR + 1 ))
	done > /dev/null 2>&1
}

write_loop_reset()
{
	while true; do
		set_test_ignore_errors > /dev/null 2>&1
		debugfs_reset_first_test_dev_ignore_errors > /dev/null 2>&1
	done > /dev/null 2>&1
}

write_loop_bg()
{
	BG_WRITES=1000 > /dev/null 2>&1
	while true; do
		for i in $(seq 1 $BG_WRITES); do
			set_test_ignore_errors > /dev/null 2>&1 &
			TEST_STR=$(( $TEST_STR + 1 ))
		done > /dev/null 2>&1
		wait
	done > /dev/null 2>&1
	wait
}

reset_loop()
{
	while true; do
		debugfs_reset_first_test_dev_ignore_errors > /dev/null 2>&1
	done > /dev/null 2>&1
}

kill_trigger_loop()
{

	local my_first_loop_pid=$1
	local my_second_loop_pid=$2
	local my_sleep_max=$3
	local my_loop=0

	while true; do
		sleep 1
		if [[ $my_loop -ge $my_sleep_max ]]; then
			break
		fi
		let my_loop=$my_loop+1
	done

	kill -s TERM $my_first_loop_pid 2>&1 > /dev/null
	kill -s TERM $my_second_loop_pid 2>&1 > /dev/null
}

_dmesg_since_test_start()
{
	# search the dmesg log of last run of $seqnum for possible failures
	# use sed \cregexpc address type, since $seqnum contains "/"
	dmesg | tac | sed -ne "0,\#run fstests $seqnum at $date_time#p" | tac
}

sysfs_test_0001()
{
	TARGET="${DIR}/$(get_test_target 0001)"
	config_reset
	reset_vals
	ORIG=$(cat "${TARGET}")
	TEST_STR=$(( $ORIG + 1 ))

	run_numerictests_single_write
}

sysfs_test_0002()
{
	TARGET="${DIR}/$(get_test_target 0002)"
	config_reset
	ORIG=$(cat "${TARGET}")
	TEST_STR=$(( $ORIG + 1 ))

	run_numerictests_single_write
}

sysfs_test_0003()
{
	TARGET="${DIR}/$(get_test_target 0003)"
	config_reset
	ORIG=$(cat "${TARGET}")
	TEST_STR=$(( $ORIG + 1 ))

	config_enable_lock

	run_numerictests_single_write
}

sysfs_test_0004()
{
	TARGET="${DIR}/$(get_test_target 0004)"
	config_reset
	ORIG=$(cat "${TARGET}")
	TEST_STR=$(( $ORIG + 1 ))

	config_enable_lock

	run_numerictests_single_write
}

sysfs_test_0005()
{
	TARGET="${DIR}/$(get_test_target 0005)"
	modprobe_reset
	config_reset
	ORIG=$(cat "${TARGET}")
	TEST_STR=$(( $ORIG + 1 ))
	WAIT_TIME=2

	echo -n "Loop writing x while loading/unloading the module... "

	modprobe_loop &
	modprobe_pid=$!

	write_loop &
	write_pid=$!

	kill_trigger_loop $modprobe_pid $write_pid $WAIT_TIME > /dev/null 2>&1 &
	kill_pid=$!

	wait $kill_pid > /dev/null 2>&1

	if [[ $? -eq 0 ]]; then
		echo "ok"
	else
		echo "FAIL" >&2
	fi
}

sysfs_test_0006()
{
	TARGET="${DIR}/$(get_test_target 0006)"
	modprobe_reset
	config_reset
	ORIG=$(cat "${TARGET}")
	TEST_STR=$(( $ORIG + 1 ))
	WAIT_TIME=2

	echo -n "Loop writing y while loading/unloading the module... "
	modprobe_loop &
	modprobe_pid=$!

	write_loop &
	write_pid=$!

	kill_trigger_loop $modprobe_pid $write_pid $WAIT_TIME > /dev/null 2>&1 &
	kill_pid=$!

	wait $kill_pid > /dev/null 2>&1

	if [[ $? -eq 0 ]]; then
		echo "ok"
	else
		echo "FAIL" >&2
	fi
}

sysfs_test_0007()
{
	TARGET="${DIR}/$(get_test_target 0007)"
	modprobe_reset
	config_reset
	ORIG=$(cat "${TARGET}")
	TEST_STR=$(( $ORIG + 1 ))
	WAIT_TIME=2

	echo -n "Loop writing y with a larger delay while loading/unloading the module... "

	MODPROBE_ARGS="write_delay_msec_y=1500"
	modprobe_loop > /dev/null 2>&1 &
	modprobe_pid=$!
	unset MODPROBE_ARGS

	write_loop &
	write_pid=$!

	kill_trigger_loop $modprobe_pid $write_pid $WAIT_TIME > /dev/null 2>&1 &
	kill_pid=$!

	wait $kill_pid > /dev/null 2>&1

	if [[ $? -eq 0 ]]; then
		echo "ok"
	else
		echo "FAIL" >&2
	fi
}

sysfs_test_0008()
{
	TARGET="${DIR}/$(get_test_target 0008)"
	modprobe_reset
	config_reset
	reset_vals
	ORIG=$(cat "${TARGET}")
	TEST_STR=$(( $ORIG + 1 ))
	WAIT_TIME=2

	echo -n "Loop busy writing x while loading/unloading the module... "

	modprobe_loop > /dev/null 2>&1 &
	modprobe_pid=$!

	write_loop_bg > /dev/null 2>&1 &
	write_pid=$!

	kill_trigger_loop $modprobe_pid $write_pid $WAIT_TIME > /dev/null 2>&1 &
	kill_pid=$!

	wait $kill_pid > /dev/null 2>&1

	if [[ $? -eq 0 ]]; then
		echo "ok"
	else
		echo "FAIL" >&2
	fi
}

sysfs_test_0009()
{
	TARGET="${DIR}/$(get_test_target 0009)"
	modprobe_reset
	config_reset
	reset_vals
	ORIG=$(cat "${TARGET}")
	TEST_STR=$(( $ORIG + 1 ))
	WAIT_TIME=2

	echo -n "Loop busy writing y while loading/unloading the module... "

	modprobe_loop > /dev/null 2>&1 &
	modprobe_pid=$!

	write_loop_bg > /dev/null 2>&1 &
	write_pid=$!

	kill_trigger_loop $modprobe_pid $write_pid $WAIT_TIME > /dev/null 2>&1 &
	kill_pid=$!

	wait $kill_pid > /dev/null 2>&1

	if [[ $? -eq 0 ]]; then
		echo "ok"
	else
		echo "FAIL" >&2
	fi
}

sysfs_test_0010()
{
	TARGET="${DIR}/$(get_test_target 0010)"
	modprobe_reset
	config_reset
	reset_vals
	ORIG=$(cat "${TARGET}")
	TEST_STR=$(( $ORIG + 1 ))
	WAIT_TIME=2

	echo -n "Loop busy writing y with a larger delay while loading/unloading the module... "
	modprobe -q -r $TEST_DRIVER > /dev/null 2>&1

	MODPROBE_ARGS="write_delay_msec_y=1500"
	modprobe_loop > /dev/null 2>&1 &
	modprobe_pid=$!
	unset MODPROBE_ARGS

	write_loop_bg > /dev/null 2>&1 &
	write_pid=$!

	kill_trigger_loop $modprobe_pid $write_pid $WAIT_TIME > /dev/null 2>&1 &
	kill_pid=$!

	wait $kill_pid > /dev/null 2>&1

	if [[ $? -eq 0 ]]; then
		echo "ok"
	else
		echo "FAIL" >&2
	fi
}

sysfs_test_0011()
{
	TARGET="${DIR}/$(get_test_target 0011)"
	modprobe_reset_enable_debugfs
	config_reset
	reset_vals
	ORIG=$(cat "${TARGET}")
	TEST_STR=$(( $ORIG + 1 ))
	WAIT_TIME=2

	echo -n "Loop writing x and resetting ... "

	write_loop > /dev/null 2>&1 &
	write_pid=$!

	reset_loop > /dev/null 2>&1 &
	reset_pid=$!

	kill_trigger_loop $write_pid $reset_pid $WAIT_TIME > /dev/null 2>&1 &
	kill_pid=$!

	wait $kill_pid > /dev/null 2>&1

	if [[ $? -eq 0 ]]; then
		echo "ok"
	else
		echo "FAIL" >&2
	fi
}

sysfs_test_0012()
{
	TARGET="${DIR}/$(get_test_target 0012)"
	modprobe_reset_enable_debugfs
	config_reset
	reset_vals
	ORIG=$(cat "${TARGET}")
	TEST_STR=$(( $ORIG + 1 ))
	WAIT_TIME=2

	echo -n "Loop writing y and resetting ... "

	write_loop > /dev/null 2>&1 &
	write_pid=$!

	reset_loop > /dev/null 2>&1 &
	reset_pid=$!

	kill_trigger_loop $write_pid $reset_pid $WAIT_TIME > /dev/null 2>&1 &
	kill_pid=$!

	wait $kill_pid > /dev/null 2>&1

	if [[ $? -eq 0 ]]; then
		echo "ok"
	else
		echo "FAIL" >&2
	fi
}

sysfs_test_0013()
{
	TARGET="${DIR}/$(get_test_target 0013)"
	modprobe_reset_enable_debugfs
	config_reset
	reset_vals
	config_write_delay_msec_y 1500
	ORIG=$(cat "${TARGET}")
	TEST_STR=$(( $ORIG + 1 ))
	WAIT_TIME=2

	echo -n "Loop writing y with a larger delay and resetting ... "

	write_loop > /dev/null 2>&1 &
	write_pid=$!

	reset_loop > /dev/null 2>&1 &
	reset_pid=$!

	kill_trigger_loop $write_pid $reset_pid $WAIT_TIME > /dev/null 2>&1 &
	kill_pid=$!

	wait $kill_pid > /dev/null 2>&1

	if [[ $? -eq 0 ]]; then
		echo "ok"
	else
		echo "FAIL" >&2
	fi
}

sysfs_test_0014()
{
	sysfs_test_0001
}

sysfs_test_0015()
{
	sysfs_test_0002
}

sysfs_test_0016()
{
	sysfs_test_0003
}

sysfs_test_0017()
{
	sysfs_test_0004
}

sysfs_test_0018()
{
	sysfs_test_0005
}

sysfs_test_0019()
{
	sysfs_test_0006
}

sysfs_test_0020()
{
	sysfs_test_0007
}

sysfs_test_0021()
{
	sysfs_test_0008
}

sysfs_test_0022()
{
	sysfs_test_0009
}

sysfs_test_0023()
{
	sysfs_test_0010
}

sysfs_test_0024()
{
	sysfs_test_0011
}

sysfs_test_0025()
{
	sysfs_test_0012
}

sysfs_test_0026()
{
	sysfs_test_0013
}

sysfs_test_0027()
{
	TARGET="${DIR}/$(get_test_target 0027)"
	modprobe_reset_enable_lock_on_rmmod
	ORIG=$(cat "${TARGET}")
	TEST_STR=$(( $ORIG + 1 ))
	WAIT_TIME=2

	echo -n "Test for possible rmmod deadlock while writing x ... "

	write_loop > /dev/null 2>&1 &
	write_pid=$!

	MODPROBE_ARGS="enable_lock=1 enable_lock_on_rmmod=1 enable_verbose_writes=1"
	modprobe_loop > /dev/null 2>&1 &
	modprobe_pid=$!
	unset MODPROBE_ARGS

	kill_trigger_loop $modprobe_pid $write_pid $WAIT_TIME > /dev/null 2>&1 &
	kill_pid=$!

	wait $kill_pid > /dev/null 2>&1

	if [[ $? -eq 0 ]]; then
		echo "ok"
	else
		echo "FAIL" >&2
	fi
}

sysfs_test_0028()
{
	TARGET="${DIR}/$(get_test_target 0028)"
	modprobe_reset_enable_lock_on_rmmod
	ORIG=$(cat "${TARGET}")
	TEST_STR=$(( $ORIG + 1 ))
	WAIT_TIME=2

	echo -n "Test for possible rmmod deadlock using rtnl_lock while writing x ... "

	write_loop > /dev/null 2>&1 &
	write_pid=$!

	MODPROBE_ARGS="enable_lock=1 enable_lock_on_rmmod=1 use_rtnl_lock=1 enable_verbose_writes=1"
	modprobe_loop > /dev/null 2>&1 &
	modprobe_pid=$!
	unset MODPROBE_ARGS

	kill_trigger_loop $modprobe_pid $write_pid $WAIT_TIME > /dev/null 2>&1 &
	kill_pid=$!

	wait $kill_pid > /dev/null 2>&1

	if [[ $? -eq 0 ]]; then
		echo "ok"
	else
		echo "FAIL" >&2
	fi
}

sysfs_race_kernfs_kernfs_fop_write_iter()
{
	TARGET="${DIR}/$(get_test_target $1)"
	WAIT_AT=$2
	EXPECT_WRITE_RETURNS=$3
	MSDELAY=$4

	modprobe_reset_enable_completion
	ORIG=$(cat "${TARGET}")
	TEST_STR=$(( $ORIG + 1 ))

	echo -n "Test racing removal of sysfs store op with kernfs $WAIT_AT ... "

	if debugfs_kernfs_kernfs_fop_write_iter_exists; then
		echo -n "skipping test as CONFIG_FAIL_KERNFS_KNOBS "
		echo " or CONFIG_FAULT_INJECTION_DEBUG_FS is disabled"
		return $ksft_skip
	fi

	# Allow for failing the kernfs_kernfs_fop_write_iter call once,
	# we'll provide exact context shortly afterwards.
	debugfs_kernfs_kernfs_fop_write_iter_set_fail_once

	# First disable all waits
	debugfs_kernfs_disable_wait_kernfs_fop_write_iter

	# Enable a wait_for_completion(&kernfs_debug_wait_completion) at the
	# specified location inside the kernfs_fop_write_iter() routine
	debugfs_kernfs_enable_wait_kernfs_fop_write_iter $WAIT_AT

	# Configure kernfs so that after its wait_for_completion() it
	# will msleep() this amount of time and schedule(). We figure this
	# will be sufficient time to allow for our module removal to complete.
	debugfs_kernfs_set_wait_ms $MSDELAY

	# Now we trigger a kernfs write op, which will run kernfs_fop_write_iter,
	# but will wait until our driver sends a respective completion
	set_test_ignore_errors &
	write_pid=$!

	# At this point kernfs_fop_write_iter() hasn't run our op, its
	# waiting for our completion at the specified time $WAIT_AT.
	# We now remove our module which will send a
	# complete(&kernfs_debug_wait_completion) right before we deregister
	# our device and the sysfs device attributes are removed.
	#
	# After the completion is sent, the test_sysfs driver races with
	# kernfs to do the device deregistration with the kernfs msleep
	# and schedule(). This should mean we've forced trying to remove the
	# module prior to allowing kernfs to run our store operation. If the
	# race did happen we'll panic with a null dereference on the store op.
	#
	# If no race happens we should see no write operation triggered.
	modprobe -r $TEST_DRIVER > /dev/null 2>&1

	debugfs_kernfs_kernfs_fop_write_iter_set_fail_never

	wait $write_pid
	if [[ $? -eq $EXPECT_WRITE_RETURNS ]]; then
		echo "ok"
	else
		echo "FAIL" >&2
	fi
}

sysfs_test_0029()
{
	for delay in 0 2 4 8 16 32 64 128 246 512 1024; do
		echo "Using delay-after-completion: $delay"
		sysfs_race_kernfs_kernfs_fop_write_iter 0029 at_start 1 $delay
	done
}

sysfs_test_0030()
{
	for delay in 0 2 4 8 16 32 64 128 246 512 1024; do
		echo "Using delay-after-completion: $delay"
		sysfs_race_kernfs_kernfs_fop_write_iter 0030 before_mutex 1 $delay
	done
}

sysfs_test_0031()
{
	for delay in 0 2 4 8 16 32 64 128 246 512 1024; do
		echo "Using delay-after-completion: $delay"
		sysfs_race_kernfs_kernfs_fop_write_iter 0031 after_mutex 1 $delay
	done
}

# Even if we get the active reference the write fails
sysfs_test_0032()
{
	for delay in 0 2 4 8 16 32 64 128 246 512 1024; do
		echo "Using delay-after-completion: $delay"
		sysfs_race_kernfs_kernfs_fop_write_iter 0032 after_active 1 $delay
	done
}

test_gen_desc()
{
	echo -n "$1 x $(get_test_count $1)"
}

list_tests()
{
	echo "Test ID list:"
	echo
	echo "TEST_ID x NUM_TEST"
	echo "TEST_ID:   Test ID"
	echo "NUM_TESTS: Number of recommended times to run the test"
	echo
	echo "$(test_gen_desc 0001) - misc test writing x in different ways"
	echo "$(test_gen_desc 0002) - misc test writing y in different ways"
	echo "$(test_gen_desc 0003) - misc test writing x in different ways using a mutex lock"
	echo "$(test_gen_desc 0004) - misc test writing y in different ways using a mutex lock"
	echo "$(test_gen_desc 0005) - misc test writing x load and remove the test_sysfs module"
	echo "$(test_gen_desc 0006) - misc writing y load and remove the test_sysfs module"
	echo "$(test_gen_desc 0007) - misc test writing y larger delay, load, remove test_sysfs"
	echo "$(test_gen_desc 0008) - misc test busy writing x remove test_sysfs module"
	echo "$(test_gen_desc 0009) - misc test busy writing y remove the test_sysfs module"
	echo "$(test_gen_desc 0010) - misc test busy writing y larger delay, remove test_sysfs"
	echo "$(test_gen_desc 0011) - misc test writing x and resetting device"
	echo "$(test_gen_desc 0012) - misc test writing y and resetting device"
	echo "$(test_gen_desc 0013) - misc test writing y with a larger delay and resetting device"
	echo "$(test_gen_desc 0014) - block test writing x in different ways"
	echo "$(test_gen_desc 0015) - block test writing y in different ways"
	echo "$(test_gen_desc 0016) - block test writing x in different ways using a mutex lock"
	echo "$(test_gen_desc 0017) - block test writing y in different ways using a mutex lock"
	echo "$(test_gen_desc 0018) - block test writing x load and remove the test_sysfs module"
	echo "$(test_gen_desc 0019) - block test writing y load and remove the test_sysfs module"
	echo "$(test_gen_desc 0020) - block test writing y larger delay, load, remove test_sysfs"
	echo "$(test_gen_desc 0021) - block test busy writing x remove the test_sysfs module"
	echo "$(test_gen_desc 0022) - block test busy writing y remove the test_sysfs module"
	echo "$(test_gen_desc 0023) - block test busy writing y larger delay, remove test_sysfs"
	echo "$(test_gen_desc 0024) - block test writing x and resetting device"
	echo "$(test_gen_desc 0025) - block test writing y and resetting device"
	echo "$(test_gen_desc 0026) - block test writing y larger delay and resetting device"
	echo "$(test_gen_desc 0027) - test rmmod deadlock while writing x ... "
	echo "$(test_gen_desc 0028) - test rmmod deadlock using rtnl_lock while writing x ..."
	echo "$(test_gen_desc 0029) - racing removal of store op with kernfs at start"
	echo "$(test_gen_desc 0030) - racing removal of store op with kernfs before mutex"
	echo "$(test_gen_desc 0031) - racing removal of store op with kernfs after mutex"
	echo "$(test_gen_desc 0032) - racing removal of store op with kernfs after active"
}

usage()
{
	NUM_TESTS=$(grep -o ' ' <<<"$ALL_TESTS" | grep -c .)
	let NUM_TESTS=$NUM_TESTS+1
	MAX_TEST=$(printf "%04d\n" $NUM_TESTS)
	echo "Usage: $0 [ -t <4-number-digit> ] | [ -w <4-number-digit> ] |"
	echo "		 [ -s <4-number-digit> ] | [ -c <4-number-digit> <test- count>"
	echo "           [ all ] [ -h | --help ] [ -l ]"
	echo ""
	echo "Valid tests: 0001-$MAX_TEST"
	echo ""
	echo "    all     Runs all tests (default)"
	echo "    -t      Run test ID the number amount of times is recommended"
	echo "    -w      Watch test ID run until it runs into an error"
	echo "    -c      Run test ID once"
	echo "    -s      Run test ID x test-count number of times"
	echo "    -l      List all test ID list"
	echo " -h|--help  Help"
	echo
	echo "If an error every occurs execution will immediately terminate."
	echo "If you are adding a new test try using -w <test-ID> first to"
	echo "make sure the test passes a series of tests."
	echo
	echo Example uses:
	echo
	echo "$TEST_NAME.sh            -- executes all tests"
	echo "$TEST_NAME.sh -t 0002    -- Executes test ID 0002 number of times is recomended"
	echo "$TEST_NAME.sh -w 0002    -- Watch test ID 0002 run until an error occurs"
	echo "$TEST_NAME.sh -s 0002    -- Run test ID 0002 once"
	echo "$TEST_NAME.sh -c 0002 3  -- Run test ID 0002 three times"
	echo
	list_tests
	exit 1
}

test_num()
{
	re='^[0-9]+$'
	if ! [[ $1 =~ $re ]]; then
		usage
	fi
}

get_test_count()
{
	test_num $1
	TEST_NUM=$(echo $1 | sed 's/^0*//')
	TEST_DATA=$(echo $ALL_TESTS | awk '{print $'$TEST_NUM'}')
	echo ${TEST_DATA} | awk -F":" '{print $2}'
}

get_test_enabled()
{
	test_num $1
	TEST_NUM=$(echo $1 | sed 's/^0*//')
	TEST_DATA=$(echo $ALL_TESTS | awk '{print $'$TEST_NUM'}')
	echo ${TEST_DATA} | awk -F":" '{print $3}'
}

get_test_target()
{
	test_num $1
	TEST_NUM=$(echo $1 | sed 's/^0*//')
	TEST_DATA=$(echo $ALL_TESTS | awk '{print $'$TEST_NUM'}')
	echo ${TEST_DATA} | awk -F":" '{print $4}'
}

get_test_type()
{
	test_num $1
	TEST_NUM=$(echo $1 | sed 's/^0*//')
	TEST_DATA=$(echo $ALL_TESTS | awk '{print $'$TEST_NUM'}')
	echo ${TEST_DATA} | awk -F":" '{print $5}'
}

run_all_tests()
{
	for i in $ALL_TESTS ; do
		TEST_ID=$(echo $i | awk -F":" '{print $1}')
		ENABLED=$(get_test_enabled $TEST_ID)
		TEST_COUNT=$(get_test_count $TEST_ID)
		TEST_TARGET=$(get_test_target $TEST_ID)
		if [[ $ENABLED -eq "1" ]]; then
			test_case $TEST_ID $TEST_COUNT $TEST_TARGET
		else
			echo -n "Skipping test $TEST_ID as its disabled, likely "
			echo "could crash your system ..."
		fi
	done
}

watch_log()
{
	if [ $# -ne 3 ]; then
		clear
	fi
	echo "Running test: $2 - run #$1"
}

watch_case()
{
	i=0
	while [ 1 ]; do

		if [ $# -eq 1 ]; then
			test_num $1
			watch_log $i ${TEST_NAME}_test_$1
			${TEST_NAME}_test_$1
			check_dmesg
			if [[ $? -eq 0 ]]; then
				exit 1
			fi
		else
			watch_log $i all
			run_all_tests
		fi
		let i=$i+1
	done
}

test_case()
{
	NUM_TESTS=$2

	i=0

	load_modreqs $1
	if target_exists $3 $1; then
		return
	fi

	while [[ $i -lt $NUM_TESTS ]]; do
		test_num $1
		watch_log $i ${TEST_NAME}_test_$1 noclear
		log_kernel_fstest_dmesg sysfs $1
		RUN_TEST=${TEST_NAME}_test_$1
		$RUN_TEST
		let i=$i+1
	done
	check_dmesg
	if [[ $? -ne 0 ]]; then
		exit 1
	fi
}

parse_args()
{
	if [ $# -eq 0 ]; then
		run_all_tests
	else
		if [[ "$1" = "all" ]]; then
			run_all_tests
		elif [[ "$1" = "-w" ]]; then
			shift
			watch_case $@
		elif [[ "$1" = "-t" ]]; then
			shift
			test_num $1
			test_case $1 $(get_test_count $1) $(get_test_target $1)
		elif [[ "$1" = "-c" ]]; then
			shift
			test_num $1
			test_num $2
			test_case $1 $2 $(get_test_target $1)
		elif [[ "$1" = "-s" ]]; then
			shift
			test_case $1 1 $(get_test_target $1)
		elif [[ "$1" = "-l" ]]; then
			list_tests
		elif [[ "$1" = "-h" || "$1" = "--help" ]]; then
			usage
		else
			usage
		fi
	fi
}

test_reqs
allow_user_defaults

trap "test_finish" EXIT

parse_args $@

exit 0
