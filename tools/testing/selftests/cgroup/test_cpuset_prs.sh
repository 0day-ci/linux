#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test for cpuset v2 partition root state (PRS)
#
# The sched verbose flag is set, if available, so that the console log
# can be examined for the correct setting of scheduling domain.
#

skip_test() {
	echo "$1"
	echo "Test SKIPPED"
	exit 0
}

[[ $(id -u) -eq 0 ]] || skip_test "Test must be run as root!"

# Set sched verbose flag, if available
[[ -d /sys/kernel/debug/sched ]] && echo Y > /sys/kernel/debug/sched/verbose

# Find cgroup v2 mount point
CGROUP2=$(mount | grep "^cgroup2" | awk -e '{print $3}')
[[ -n "$CGROUP2" ]] || skip_test "Cgroup v2 mount point not found!"

CPUS=$(lscpu | grep "^CPU(s)" | sed -e "s/.*:[[:space:]]*//")
[[ $CPUS -lt 4 ]] && skip_test "Test needs at least 4 cpus available!"

cd $CGROUP2
echo +cpuset > cgroup.subtree_control
[[ -d test ]] || mkdir test
cd test
echo 2-3 > cpuset.cpus
TYPE=$(cat cpuset.cpus.partition)
[[ $TYPE = member ]] || echo member > cpuset.cpus.partition

console_msg()
{
	MSG=$1
	echo "$MSG"
	echo "" > /dev/console
	echo "$MSG" > /dev/console
	sleep 1
}

test_partition()
{
	EXPECTED_VAL=$1
	echo $EXPECTED_VAL > cpuset.cpus.partition
	[[ $? -eq 0 ]] || exit 1
	ACTUAL_VAL=$(cat cpuset.cpus.partition)
	[[ $ACTUAL_VAL != $EXPECTED_VAL ]] && {
		echo "cpuset.cpus.partition: expect $EXPECTED_VAL, found $EXPECTED_VAL"
		echo "Test FAILED"
		exit 1
	}
}

test_effective_cpus()
{
	EXPECTED_VAL=$1
	ACTUAL_VAL=$(cat cpuset.cpus.effective)
	[[ "$ACTUAL_VAL" != "$EXPECTED_VAL" ]] && {
		echo "cpuset.cpus.effective: expect '$EXPECTED_VAL', found '$EXPECTED_VAL'"
		echo "Test FAILED"
		exit 1
	}
}

# Adding current process to cgroup.procs as a test
test_add_proc()
{
	OUTSTR="$1"
	ERRMSG=$((echo $$ > cgroup.procs) |& cat)
	echo $ERRMSG | grep -q "$OUTSTR"
	[[ $? -ne 0 ]] && {
		echo "cgroup.procs: expect '$OUTSTR', got '$ERRMSG'"
		echo "Test FAILED"
		exit 1
	}
	echo $$ > $CGROUP2/cgroup.procs	# Move out the task
}

#
# Testing the new "root-nolb" partition root type
#
console_msg "Change from member to root"
test_partition root

console_msg "Change from root to root-nolb"
test_partition root-nolb

console_msg "Change from root-nolb to member"
test_partition member

console_msg "Change from member to root-nolb"
test_partition root-nolb

console_msg "Change from root-nolb to root"
test_partition root

console_msg "Change from root to member"
test_partition member

#
# Testing partition root with no cpu
#
console_msg "Distribute all cpus to child partition"
echo +cpuset > cgroup.subtree_control
test_partition root

mkdir t1
cd t1
echo 2-3 > cpuset.cpus
test_partition root
test_effective_cpus 2-3
cd ..
test_effective_cpus ""

console_msg "Moving task to partition test"
test_add_proc "No space left"
cd t1
test_add_proc ""
cd ..

console_msg "Shrink and expand child partition"
cd t1
echo 2 > cpuset.cpus
cd ..
test_effective_cpus 3
cd t1
echo 2-3 > cpuset.cpus
cd ..
test_effective_cpus ""

# Cleaning up
console_msg "Cleaning up"
echo $$ > $CGROUP2/cgroup.procs
[[ -d t1 ]] && rmdir t1
cd ..
rmdir test
echo "Test PASSED"
