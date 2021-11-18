#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2021 Bartosz Golaszewski <brgl@bgdev.pl>

BASE_DIR=`dirname $0`
CONFIGFS_DIR="/sys/kernel/config/gpio-sim"
MODULE="gpio-sim"

fail() {
	echo "$*" >&2
	echo "GPIO $MODULE test FAIL"
	exit 1
}

skip() {
	echo "$*" >&2
	echo "GPIO $MODULE test SKIP"
	exit 4
}

remove_chip() {
	local CHIP=$1

	LINES=`ls $CONFIGFS_DIR/$CHIP/ | egrep ^line`
	if [ "$?" == 0 ]; then
		for LINE in $LINES; do
			rmdir $CONFIGFS_DIR/$CHIP/$LINE || fail "Unable to remove the line"
		done
	fi

	rmdir $CONFIGFS_DIR/$CHIP || fail "Unable to remove the chip"
}

configfs_cleanup() {
	for CHIP in `ls $CONFIGFS_DIR/`; do
		remove_chip $CHIP
	done
}

create_chip() {
	local CHIP=$1
	local CHIP_DIR="$CONFIGFS_DIR/$CHIP"

	mkdir $CHIP_DIR
}

set_label() {
	local CHIP=$1
	local LABEL=$2

	echo $LABEL > $CONFIGFS_DIR/$CHIP/label || fail "Unable to set the chip label"
}

set_num_lines() {
	local CHIP=$1
	local NUM_LINES=$2

	echo $NUM_LINES > $CONFIGFS_DIR/$CHIP/num_lines || fail "Unable to set the number of lines"
}

set_line_name() {
	local CHIP=$1
	local OFFSET=$2
	local NAME=$3
	local LINE_DIR=$CONFIGFS_DIR/$CHIP/line$OFFSET

	test -d $LINE_DIR || mkdir $LINE_DIR
	echo $NAME > $LINE_DIR/name || fail "Unable to set the line name"
}

enable_chip() {
	local CHIP=$1

	echo 1 > $CONFIGFS_DIR/$CHIP/live || fail "Unable to enable the chip"
}

disable_chip() {
	local CHIP=$1

	echo 0 > $CONFIGFS_DIR/$CHIP/live || fail "Unable to disable the chip"
}

configfs_chip_name() {
	local CHIP="$1"

	cat $CONFIGFS_DIR/$CHIP/chip_name 2> /dev/null || return 1
}

configfs_dev_name() {
	local CHIP="$1"

	cat $CONFIGFS_DIR/$CHIP/dev_name 2> /dev/null || return 1
}

get_chip_num_lines() {
	local CHIP="$1"

	$BASE_DIR/gpio-chip-info /dev/`configfs_chip_name $CHIP` num-lines
}

get_chip_label() {
	local CHIP="$1"

	$BASE_DIR/gpio-chip-info /dev/`configfs_chip_name $CHIP` label
}

get_line_name() {
	local CHIP="$1"
	local OFFSET="$2"

	$BASE_DIR/gpio-line-name /dev/`configfs_chip_name $CHIP` $OFFSET
}

sysfs_set_pull() {
	local CHIP="$1"
	local OFFSET="$2"
	local PULL="$3"
	local SYSFSPATH="/sys/devices/platform/`configfs_dev_name $CHIP`/control/gpio$OFFSET"

	echo $PULL > $SYSFSPATH || fail "Unable to set line pull in sysfs"
}

# Load the gpio-sim module. This will pull in configfs if needed too.
modprobe gpio-sim || skip "unable to load the gpio-sim module"
# Make sure configfs is mounted at /sys/kernel/config. Wait a bit if needed.
for IDX in `seq 5`; do
	if [ "$IDX" -eq "5" ]; then
		skip "configfs not mounted at /sys/kernel/config"
	fi

	mountpoint -q /sys/kernel/config && break
	sleep 0.1
done
# If the module was already loaded: remove all previous chips
configfs_cleanup

trap "exit 1" SIGTERM SIGINT
trap configfs_cleanup EXIT

echo "1. chip_name and dev_name attributes"

echo "1.1. Chip name is communicated to user"
create_chip chip
enable_chip chip
test -n `cat $CONFIGFS_DIR/chip/chip_name` || fail "chip_name doesn't work"
remove_chip chip

echo "1.2. chip_name returns 'none' if the chip is still pending"
create_chip chip
test "`cat $CONFIGFS_DIR/chip/chip_name`" = "none" || fail "chip_name doesn't return 'none' for a pending chip"
remove_chip chip

echo "1.3. Device name is communicated to user"
create_chip chip
enable_chip chip
test -n `cat $CONFIGFS_DIR/chip/dev_name` || fail "dev_name doesn't work"
remove_chip chip

echo "2. Creating and configuring simulated chips"

echo "2.1. Default number of lines is 1"
create_chip chip
enable_chip chip
test "`get_chip_num_lines chip`" = "1" || fail "default number of lines is not 1"
remove_chip chip

echo "2.2. Number of lines can be specified"
create_chip chip
set_num_lines chip 16
enable_chip chip
test "`get_chip_num_lines chip`" = "16" || fail "number of lines is not 16"
remove_chip chip

echo "2.3. Label can be set"
create_chip chip
set_label chip foobar
enable_chip chip
test "`get_chip_label chip`" = "foobar" || fail "label is incorrect"
remove_chip chip

echo "2.4. Label can be left empty"
create_chip chip
enable_chip chip
test -z "`cat $CONFIGFS_DIR/chip/label`" || fail "label is not empty"
remove_chip chip

echo "2.5. Line names can be configured"
create_chip chip
set_num_lines chip 16
set_line_name chip 0 foo
set_line_name chip 2 bar
enable_chip chip
test "`get_line_name chip 0`" = "foo" || fail "line name is incorrect"
test "`get_line_name chip 2`" = "bar" || fail "line name is incorrect"
remove_chip chip

echo "2.6. Line name can remain unused"
create_chip chip
set_num_lines chip 2
set_line_name chip 5 foobar
enable_chip chip
test "`get_line_name chip 0`" = "" || fail "line name is incorrect"
test "`get_line_name chip 1`" = "" || fail "line name is incorrect"
remove_chip chip

echo "2.7. Line configfs directory names are sanitized"
create_chip chip
mkdir $CONFIGFS_DIR/chip/line12foobar 2> /dev/null && fail "invalid configfs line name accepted"
mkdir $CONFIGFS_DIR/chip/line_no_offset 2> /dev/null && fail "invalid configfs line name accepted"
remove_chip chip

echo "2.8. Multiple chips can be created"
CHIPS="chip0 chip1 chip2"
for CHIP in $CHIPS; do
	create_chip $CHIP
	enable_chip $CHIP
done
for CHIP in $CHIPS; do
	remove_chip $CHIP
done

echo "3. Controlling simulated chips"

echo "3.3. Pull can be set over sysfs"
create_chip chip
set_num_lines chip 8
enable_chip chip
sysfs_set_pull chip 0 1
$BASE_DIR/gpio-mockup-cdev /dev/`configfs_chip_name chip` 0
test "$?" = "1" || fail "pull set incorrectly"
sysfs_set_pull chip 0 0
$BASE_DIR/gpio-mockup-cdev /dev/`configfs_chip_name chip` 1
test "$?" = "0" || fail "pull set incorrectly"
remove_chip chip

echo "3.4. Incorrect input in sysfs is rejected"
create_chip chip
set_num_lines chip 8
enable_chip chip
SYSFS_PATH="/sys/devices/platform/`configfs_dev_name chip`/control/gpio0"
echo 2 > $SYSFS_PATH 2> /dev/null && fail "invalid input not detected"
remove_chip chip

echo "4. Simulated GPIO chips are functional"

echo "4.1. Values can be read from sysfs"
create_chip chip
set_num_lines chip 8
enable_chip chip
SYSFS_PATH="/sys/devices/platform/`configfs_dev_name chip`/control/gpio0"
test `cat $SYSFS_PATH` = "0" || fail "incorrect value read from sysfs"
$BASE_DIR/gpio-mockup-cdev -s 1 /dev/`configfs_chip_name chip` 0 &
sleep 0.1 # FIXME Any better way?
test `cat $SYSFS_PATH` = "1" || fail "incorrect value read from sysfs"
kill $!
remove_chip chip

echo "4.2. Bias settings work correctly"
create_chip chip
set_num_lines chip 8
enable_chip chip
$BASE_DIR/gpio-mockup-cdev -b pull-up /dev/`configfs_chip_name chip` 0
test `cat $SYSFS_PATH` = "1" || fail "bias setting does not work"
remove_chip chip

echo "GPIO $MODULE test PASS"
