#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2018 Joe Lawrence <joe.lawrence@redhat.com>

. $(dirname $0)/functions.sh

MOD_LIVEPATCH=test_klp_livepatch
MOD_REPLACE=test_klp_atomic_replace
MOD_KLP_CONVERT_MOD=test_klp_convert_mod
MOD_KLP_CONVERT1=test_klp_convert1
MOD_KLP_CONVERT2=test_klp_convert2
MOD_KLP_CONVERT_SECTIONS=test_klp_convert_sections

setup_config


# - load a livepatch that modifies the output from /proc/cmdline and
#   verify correct behavior
# - unload the livepatch and make sure the patch was removed

start_test "basic function patching"

load_lp $MOD_LIVEPATCH

if [[ "$(cat /proc/cmdline)" != "$MOD_LIVEPATCH: this has been live patched" ]] ; then
	echo -e "FAIL\n\n"
	die "livepatch kselftest(s) failed"
fi

disable_lp $MOD_LIVEPATCH
unload_lp $MOD_LIVEPATCH

if [[ "$(cat /proc/cmdline)" == "$MOD_LIVEPATCH: this has been live patched" ]] ; then
	echo -e "FAIL\n\n"
	die "livepatch kselftest(s) failed"
fi

check_result "% modprobe $MOD_LIVEPATCH
livepatch: enabling patch '$MOD_LIVEPATCH'
livepatch: '$MOD_LIVEPATCH': initializing patching transition
livepatch: '$MOD_LIVEPATCH': starting patching transition
livepatch: '$MOD_LIVEPATCH': completing patching transition
livepatch: '$MOD_LIVEPATCH': patching complete
% echo 0 > /sys/kernel/livepatch/$MOD_LIVEPATCH/enabled
livepatch: '$MOD_LIVEPATCH': initializing unpatching transition
livepatch: '$MOD_LIVEPATCH': starting unpatching transition
livepatch: '$MOD_LIVEPATCH': completing unpatching transition
livepatch: '$MOD_LIVEPATCH': unpatching complete
% rmmod $MOD_LIVEPATCH"


# - load a livepatch that modifies the output from /proc/cmdline and
#   verify correct behavior
# - load another livepatch and verify that both livepatches are active
# - unload the second livepatch and verify that the first is still active
# - unload the first livepatch and verify none are active

start_test "multiple livepatches"

load_lp $MOD_LIVEPATCH

grep 'live patched' /proc/cmdline > /dev/kmsg
grep 'live patched' /proc/meminfo > /dev/kmsg

load_lp $MOD_REPLACE replace=0

grep 'live patched' /proc/cmdline > /dev/kmsg
grep 'live patched' /proc/meminfo > /dev/kmsg

disable_lp $MOD_REPLACE
unload_lp $MOD_REPLACE

grep 'live patched' /proc/cmdline > /dev/kmsg
grep 'live patched' /proc/meminfo > /dev/kmsg

disable_lp $MOD_LIVEPATCH
unload_lp $MOD_LIVEPATCH

grep 'live patched' /proc/cmdline > /dev/kmsg
grep 'live patched' /proc/meminfo > /dev/kmsg

check_result "% modprobe $MOD_LIVEPATCH
livepatch: enabling patch '$MOD_LIVEPATCH'
livepatch: '$MOD_LIVEPATCH': initializing patching transition
livepatch: '$MOD_LIVEPATCH': starting patching transition
livepatch: '$MOD_LIVEPATCH': completing patching transition
livepatch: '$MOD_LIVEPATCH': patching complete
$MOD_LIVEPATCH: this has been live patched
% modprobe $MOD_REPLACE replace=0
livepatch: enabling patch '$MOD_REPLACE'
livepatch: '$MOD_REPLACE': initializing patching transition
livepatch: '$MOD_REPLACE': starting patching transition
livepatch: '$MOD_REPLACE': completing patching transition
livepatch: '$MOD_REPLACE': patching complete
$MOD_LIVEPATCH: this has been live patched
$MOD_REPLACE: this has been live patched
% echo 0 > /sys/kernel/livepatch/$MOD_REPLACE/enabled
livepatch: '$MOD_REPLACE': initializing unpatching transition
livepatch: '$MOD_REPLACE': starting unpatching transition
livepatch: '$MOD_REPLACE': completing unpatching transition
livepatch: '$MOD_REPLACE': unpatching complete
% rmmod $MOD_REPLACE
$MOD_LIVEPATCH: this has been live patched
% echo 0 > /sys/kernel/livepatch/$MOD_LIVEPATCH/enabled
livepatch: '$MOD_LIVEPATCH': initializing unpatching transition
livepatch: '$MOD_LIVEPATCH': starting unpatching transition
livepatch: '$MOD_LIVEPATCH': completing unpatching transition
livepatch: '$MOD_LIVEPATCH': unpatching complete
% rmmod $MOD_LIVEPATCH"


# - load a livepatch that modifies the output from /proc/cmdline and
#   verify correct behavior
# - load an atomic replace livepatch and verify that only the second is active
# - remove the first livepatch and verify that the atomic replace livepatch
#   is still active
# - remove the atomic replace livepatch and verify that none are active

start_test "atomic replace livepatch"

load_lp $MOD_LIVEPATCH

grep 'live patched' /proc/cmdline > /dev/kmsg
grep 'live patched' /proc/meminfo > /dev/kmsg

load_lp $MOD_REPLACE replace=1

grep 'live patched' /proc/cmdline > /dev/kmsg
grep 'live patched' /proc/meminfo > /dev/kmsg

unload_lp $MOD_LIVEPATCH

grep 'live patched' /proc/cmdline > /dev/kmsg
grep 'live patched' /proc/meminfo > /dev/kmsg

disable_lp $MOD_REPLACE
unload_lp $MOD_REPLACE

grep 'live patched' /proc/cmdline > /dev/kmsg
grep 'live patched' /proc/meminfo > /dev/kmsg

check_result "% modprobe $MOD_LIVEPATCH
livepatch: enabling patch '$MOD_LIVEPATCH'
livepatch: '$MOD_LIVEPATCH': initializing patching transition
livepatch: '$MOD_LIVEPATCH': starting patching transition
livepatch: '$MOD_LIVEPATCH': completing patching transition
livepatch: '$MOD_LIVEPATCH': patching complete
$MOD_LIVEPATCH: this has been live patched
% modprobe $MOD_REPLACE replace=1
livepatch: enabling patch '$MOD_REPLACE'
livepatch: '$MOD_REPLACE': initializing patching transition
livepatch: '$MOD_REPLACE': starting patching transition
livepatch: '$MOD_REPLACE': completing patching transition
livepatch: '$MOD_REPLACE': patching complete
$MOD_REPLACE: this has been live patched
% rmmod $MOD_LIVEPATCH
$MOD_REPLACE: this has been live patched
% echo 0 > /sys/kernel/livepatch/$MOD_REPLACE/enabled
livepatch: '$MOD_REPLACE': initializing unpatching transition
livepatch: '$MOD_REPLACE': starting unpatching transition
livepatch: '$MOD_REPLACE': completing unpatching transition
livepatch: '$MOD_REPLACE': unpatching complete
% rmmod $MOD_REPLACE"


# TEST: klp-convert symbols
# - load a livepatch that modifies the output from /proc/cmdline
#   including a reference to vmlinux-local symbol that klp-convert
#   will process
# - verify correct behavior
# - unload the livepatch and make sure the patch was removed

start_test "klp-convert symbols"

saved_cmdline=$(cat /proc/cmdline)

load_mod $MOD_KLP_CONVERT_MOD

load_lp $MOD_KLP_CONVERT1
echo 1 > /sys/module/$MOD_KLP_CONVERT1/parameters/print_debug
disable_lp $MOD_KLP_CONVERT1
unload_lp $MOD_KLP_CONVERT1

load_lp $MOD_KLP_CONVERT2
echo 1 > /sys/module/$MOD_KLP_CONVERT2/parameters/print_debug
disable_lp $MOD_KLP_CONVERT2
unload_lp $MOD_KLP_CONVERT2

unload_mod $MOD_KLP_CONVERT_MOD

check_result "% modprobe $MOD_KLP_CONVERT_MOD
% modprobe $MOD_KLP_CONVERT1
livepatch: enabling patch '$MOD_KLP_CONVERT1'
livepatch: '$MOD_KLP_CONVERT1': initializing patching transition
livepatch: '$MOD_KLP_CONVERT1': starting patching transition
livepatch: '$MOD_KLP_CONVERT1': completing patching transition
livepatch: '$MOD_KLP_CONVERT1': patching complete
$MOD_KLP_CONVERT1: saved_command_line, 0: $saved_cmdline
$MOD_KLP_CONVERT1: driver_name, 0: $MOD_KLP_CONVERT_MOD
$MOD_KLP_CONVERT1: test_klp_get_driver_name(), 0: $MOD_KLP_CONVERT_MOD
$MOD_KLP_CONVERT1: homonym_string, 1: homonym string A
$MOD_KLP_CONVERT1: get_homonym_string(), 1: homonym string A
% echo 0 > /sys/kernel/livepatch/$MOD_KLP_CONVERT1/enabled
livepatch: '$MOD_KLP_CONVERT1': initializing unpatching transition
livepatch: '$MOD_KLP_CONVERT1': starting unpatching transition
livepatch: '$MOD_KLP_CONVERT1': completing unpatching transition
livepatch: '$MOD_KLP_CONVERT1': unpatching complete
% rmmod $MOD_KLP_CONVERT1
% modprobe $MOD_KLP_CONVERT2
livepatch: enabling patch '$MOD_KLP_CONVERT2'
livepatch: '$MOD_KLP_CONVERT2': initializing patching transition
livepatch: '$MOD_KLP_CONVERT2': starting patching transition
livepatch: '$MOD_KLP_CONVERT2': completing patching transition
livepatch: '$MOD_KLP_CONVERT2': patching complete
$MOD_KLP_CONVERT2: saved_command_line (auto): $saved_cmdline
$MOD_KLP_CONVERT2: driver_name, 0: $MOD_KLP_CONVERT_MOD
$MOD_KLP_CONVERT2: test_klp_get_driver_name(), (auto): $MOD_KLP_CONVERT_MOD
$MOD_KLP_CONVERT2: homonym_string, 2: homonym string B
$MOD_KLP_CONVERT2: get_homonym_string(), 2: homonym string B
% echo 0 > /sys/kernel/livepatch/$MOD_KLP_CONVERT2/enabled
livepatch: '$MOD_KLP_CONVERT2': initializing unpatching transition
livepatch: '$MOD_KLP_CONVERT2': starting unpatching transition
livepatch: '$MOD_KLP_CONVERT2': completing unpatching transition
livepatch: '$MOD_KLP_CONVERT2': unpatching complete
% rmmod $MOD_KLP_CONVERT2
% rmmod $MOD_KLP_CONVERT_MOD"


# TEST: klp-convert symbols (late module patching)
# - load a livepatch that modifies the output from /proc/cmdline
#   including a reference to vmlinux-local symbol that klp-convert
#   will process
# - load target module
# - verify correct behavior
# - unload the livepatch

start_test "klp-convert symbols (late module patching)"

saved_cmdline=$(cat /proc/cmdline)

load_lp $MOD_KLP_CONVERT1
load_mod $MOD_KLP_CONVERT_MOD
echo 1 > /sys/module/$MOD_KLP_CONVERT1/parameters/print_debug
disable_lp $MOD_KLP_CONVERT1
unload_lp $MOD_KLP_CONVERT1
unload_mod $MOD_KLP_CONVERT_MOD

load_lp $MOD_KLP_CONVERT2
load_mod $MOD_KLP_CONVERT_MOD
echo 1 > /sys/module/$MOD_KLP_CONVERT2/parameters/print_debug
disable_lp $MOD_KLP_CONVERT2
unload_lp $MOD_KLP_CONVERT2
unload_mod $MOD_KLP_CONVERT_MOD

check_result "% modprobe $MOD_KLP_CONVERT1
livepatch: enabling patch '$MOD_KLP_CONVERT1'
livepatch: '$MOD_KLP_CONVERT1': initializing patching transition
livepatch: '$MOD_KLP_CONVERT1': starting patching transition
livepatch: '$MOD_KLP_CONVERT1': completing patching transition
livepatch: '$MOD_KLP_CONVERT1': patching complete
% modprobe $MOD_KLP_CONVERT_MOD
livepatch: applying patch '$MOD_KLP_CONVERT1' to loading module '$MOD_KLP_CONVERT_MOD'
$MOD_KLP_CONVERT1: saved_command_line, 0: $saved_cmdline
$MOD_KLP_CONVERT1: driver_name, 0: $MOD_KLP_CONVERT_MOD
$MOD_KLP_CONVERT1: test_klp_get_driver_name(), 0: $MOD_KLP_CONVERT_MOD
$MOD_KLP_CONVERT1: homonym_string, 1: homonym string A
$MOD_KLP_CONVERT1: get_homonym_string(), 1: homonym string A
% echo 0 > /sys/kernel/livepatch/$MOD_KLP_CONVERT1/enabled
livepatch: '$MOD_KLP_CONVERT1': initializing unpatching transition
livepatch: '$MOD_KLP_CONVERT1': starting unpatching transition
livepatch: '$MOD_KLP_CONVERT1': completing unpatching transition
livepatch: '$MOD_KLP_CONVERT1': unpatching complete
% rmmod $MOD_KLP_CONVERT1
% rmmod $MOD_KLP_CONVERT_MOD
% modprobe $MOD_KLP_CONVERT2
livepatch: enabling patch '$MOD_KLP_CONVERT2'
livepatch: '$MOD_KLP_CONVERT2': initializing patching transition
livepatch: '$MOD_KLP_CONVERT2': starting patching transition
livepatch: '$MOD_KLP_CONVERT2': completing patching transition
livepatch: '$MOD_KLP_CONVERT2': patching complete
% modprobe $MOD_KLP_CONVERT_MOD
livepatch: applying patch '$MOD_KLP_CONVERT2' to loading module '$MOD_KLP_CONVERT_MOD'
$MOD_KLP_CONVERT2: saved_command_line (auto): $saved_cmdline
$MOD_KLP_CONVERT2: driver_name, 0: $MOD_KLP_CONVERT_MOD
$MOD_KLP_CONVERT2: test_klp_get_driver_name(), (auto): $MOD_KLP_CONVERT_MOD
$MOD_KLP_CONVERT2: homonym_string, 2: homonym string B
$MOD_KLP_CONVERT2: get_homonym_string(), 2: homonym string B
% echo 0 > /sys/kernel/livepatch/$MOD_KLP_CONVERT2/enabled
livepatch: '$MOD_KLP_CONVERT2': initializing unpatching transition
livepatch: '$MOD_KLP_CONVERT2': starting unpatching transition
livepatch: '$MOD_KLP_CONVERT2': completing unpatching transition
livepatch: '$MOD_KLP_CONVERT2': unpatching complete
% rmmod $MOD_KLP_CONVERT2
% rmmod $MOD_KLP_CONVERT_MOD"

exit 0
