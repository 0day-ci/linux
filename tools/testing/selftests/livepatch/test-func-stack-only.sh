#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2021 Miroslav Benes <mbenes@suse.cz>

# The tests for stack_only API which allows users to specify functions to be
# search for on a stack.

. $(dirname $0)/functions.sh

MOD_TARGET=test_klp_func_stack_only_mod
MOD_TARGET_BUSY=test_klp_callbacks_busy
MOD_LIVEPATCH=test_klp_func_stack_only_demo
MOD_LIVEPATCH2=test_klp_func_stack_only_demo2
MOD_REPLACE=test_klp_atomic_replace

setup_config

# Non-blocking test. parent_function() calls child_function() and sleeps. The
# live patch patches child_function(). The test does not use stack_only API and
# the live patching transition finishes immediately.
#
# - load a target module and let its parent_function() sleep
# - load a live patch which patches child_function()
# - the transition does not block, because parent_function() is not checked for
#   its presence on a stack
# - clean up afterwards

start_test "non-blocking patching without the function on a stack"

load_mod $MOD_TARGET block_transition=1
load_lp $MOD_LIVEPATCH
disable_lp $MOD_LIVEPATCH
unload_lp $MOD_LIVEPATCH
unload_mod $MOD_TARGET

check_result "% modprobe $MOD_TARGET block_transition=1
$MOD_TARGET: ${MOD_TARGET}_init
$MOD_TARGET: parent_function enter
$MOD_TARGET: child_function
% modprobe $MOD_LIVEPATCH
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
% rmmod $MOD_LIVEPATCH
% rmmod $MOD_TARGET
$MOD_TARGET: ${MOD_TARGET}_exit
$MOD_TARGET: parent_function exit"

# Blocking version of the previous test. stack_only is now set for
# parent_function(). The transition is blocked.
#
# - load a target module and let its parent_function() sleep
# - load a live patch which patches child_function() and specifies
#   parent_function() as a stack_only function
# - the transition blocks, because parent_function() is present on a stack
#   while sleeping there
# - clean up afterwards

start_test "patching blocked due to the function on a stack"

load_mod $MOD_TARGET block_transition=1
load_lp_nowait $MOD_LIVEPATCH func_stack_only=1

# Wait until the livepatch reports in-transition state, i.e. that it's
# stalled on $MOD_TARGET::parent_function()
loop_until 'grep -q '^1$' /sys/kernel/livepatch/$MOD_LIVEPATCH/transition' ||
	die "failed to stall transition"

disable_lp $MOD_LIVEPATCH
unload_lp $MOD_LIVEPATCH
unload_mod $MOD_TARGET

check_result "% modprobe $MOD_TARGET block_transition=1
$MOD_TARGET: ${MOD_TARGET}_init
$MOD_TARGET: parent_function enter
$MOD_TARGET: child_function
% modprobe $MOD_LIVEPATCH func_stack_only=1
livepatch: enabling patch '$MOD_LIVEPATCH'
livepatch: '$MOD_LIVEPATCH': initializing patching transition
livepatch: '$MOD_LIVEPATCH': starting patching transition
% echo 0 > /sys/kernel/livepatch/$MOD_LIVEPATCH/enabled
livepatch: '$MOD_LIVEPATCH': reversing transition from patching to unpatching
livepatch: '$MOD_LIVEPATCH': starting unpatching transition
livepatch: '$MOD_LIVEPATCH': completing unpatching transition
livepatch: '$MOD_LIVEPATCH': unpatching complete
% rmmod $MOD_LIVEPATCH
% rmmod $MOD_TARGET
$MOD_TARGET: ${MOD_TARGET}_exit
$MOD_TARGET: parent_function exit"

# Test an atomic replace live patch on top of stack_only live patch. The aim is
# to test the correct handling of nop functions in stack_only environment.
#
# - load a target module and do not let its parent_function() sleep
# - load a busy target module but do not let its busymod_work_func() sleep.
#   This is only to have another target module loaded for the next steps.
# - load a stack_only live patch. It patches the first target module and
#   defines parent_function() to be stack_only. So there is a klp_object with
#   both !stack_only and stack_only functions. The live patch also has another
#   klp_object with busymod_work_func() as stack_only function (and nothing
#   else). The live patch is smoothly applied because there is no blocking
#   involved.
# - load atomic replace live patch which patches a function in vmlinux. No nop
#   function should be created for stack_only functions
# - clean up afterwards

start_test "atomic replace on top of a stack_only live patch"

load_mod $MOD_TARGET
load_mod $MOD_TARGET_BUSY
load_lp $MOD_LIVEPATCH2
load_lp $MOD_REPLACE replace=1
disable_lp $MOD_REPLACE
unload_lp $MOD_REPLACE
unload_lp $MOD_LIVEPATCH2
unload_mod $MOD_TARGET_BUSY
unload_mod $MOD_TARGET

check_result "% modprobe $MOD_TARGET
$MOD_TARGET: ${MOD_TARGET}_init
$MOD_TARGET: parent_function enter
$MOD_TARGET: child_function
$MOD_TARGET: parent_function exit
% modprobe $MOD_TARGET_BUSY
$MOD_TARGET_BUSY: ${MOD_TARGET_BUSY}_init
$MOD_TARGET_BUSY: busymod_work_func enter
$MOD_TARGET_BUSY: busymod_work_func exit
% modprobe $MOD_LIVEPATCH2
livepatch: enabling patch '$MOD_LIVEPATCH2'
livepatch: '$MOD_LIVEPATCH2': initializing patching transition
livepatch: '$MOD_LIVEPATCH2': starting patching transition
livepatch: '$MOD_LIVEPATCH2': completing patching transition
livepatch: '$MOD_LIVEPATCH2': patching complete
% modprobe $MOD_REPLACE replace=1
livepatch: enabling patch '$MOD_REPLACE'
livepatch: '$MOD_REPLACE': initializing patching transition
livepatch: '$MOD_REPLACE': starting patching transition
livepatch: '$MOD_REPLACE': completing patching transition
livepatch: '$MOD_REPLACE': patching complete
% echo 0 > /sys/kernel/livepatch/$MOD_REPLACE/enabled
livepatch: '$MOD_REPLACE': initializing unpatching transition
livepatch: '$MOD_REPLACE': starting unpatching transition
livepatch: '$MOD_REPLACE': completing unpatching transition
livepatch: '$MOD_REPLACE': unpatching complete
% rmmod $MOD_REPLACE
% rmmod $MOD_LIVEPATCH2
% rmmod $MOD_TARGET_BUSY
$MOD_TARGET_BUSY: ${MOD_TARGET_BUSY}_exit
% rmmod $MOD_TARGET
$MOD_TARGET: ${MOD_TARGET}_exit"

exit 0
