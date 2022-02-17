#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Kselftest frmework requirement - SKIP code is 4.
ksft_skip=4

ensure_write_succ()
{
	file=$1
	content=$2
	reason=$3

	if ! echo "$content" > "$file"
	then
		echo "writing $content to $file failed"
		echo "expected success because $reason"
		exit 1
	fi
}

ensure_write_fail()
{
	file=$1
	content=$2
	reason=$3

	if echo "$content" > "$file"
	then
		echo "writing $content to $file succeed ($fail_reason)"
		echo "expected failure because $reason"
		exit 1
	fi
}

ensure_dir()
{
	dir=$1
	to_ensure=$2
	if [ "$to_ensure" = "exist" ] && [ ! -d "$dir" ]
	then
		echo "$dir dir is expected but not found"
		exit 1
	elif [ "$to_ensure" = "not_exist" ] && [ -d "$dir" ]
	then
		echo "$dir dir is not expected but found"
		exit 1
	fi
}

ensure_file()
{
	file=$1
	to_ensure=$2
	permission=$3
	if [ "$to_ensure" = "exist" ]
	then
		if [ ! -f "$file" ]
		then
			echo "$file is expected but not found"
			exit 1
		fi
		perm=$(stat -c "%a" "$file")
		if [ ! "$perm" = "$permission" ]
		then
			echo "$file permission: expected $permission but $perm"
			exit 1
		fi
	elif [ "$to_ensure" = "not_exist" ] && [ -f "$dir" ]
	then
		echo "$file is not expected but found"
		exit 1
	fi
}

test_range()
{
	range_dir=$1
	ensure_dir "$range_dir" "exist"
	ensure_file "$range_dir/min" "exist" 600
	ensure_file "$range_dir/max" "exist" 600
}

test_target()
{
	target_dir=$1
	ensure_dir "$target_dir" "exist"
	ensure_file "$target_dir/pid" "exist" "600"
}

test_targets()
{
	targets_dir=$1
	ensure_dir "$targets_dir" "exist"
	ensure_file "$targets_dir/nr" "exist" 600

	ensure_write_succ  "$targets_dir/nr" "1" "valid input"
	test_target "$targets_dir/0"

	ensure_write_succ  "$targets_dir/nr" "2" "valid input"
	test_target "$targets_dir/0"
	test_target "$targets_dir/1"

	ensure_write_succ "$targets_dir/nr" "0" "valid input"
	ensure_dir "$targets_dir/0" "not_exist"
	ensure_dir "$targets_dir/1" "not_exist"
}

test_intervals()
{
	intervals_dir=$1
	ensure_dir "$intervals_dir" "exist"
	ensure_file "$intervals_dir/aggr_us" "exist" "600"
	ensure_file "$intervals_dir/sample_us" "exist" "600"
	ensure_file "$intervals_dir/update_us" "exist" "600"
}

test_monitoring_attrs()
{
	monitoring_attrs_dir=$1
	ensure_dir "$monitoring_attrs_dir" "exist"
	test_intervals "$monitoring_attrs_dir/intervals"
	test_range "$monitoring_attrs_dir/nr_regions"
}

test_context()
{
	context_dir=$1
	ensure_dir "$context_dir" "exist"
	ensure_file "$context_dir/operations" "exist" 600
	test_monitoring_attrs "$context_dir/monitoring_attrs"
	test_targets "$context_dir/targets"
}

test_contexts()
{
	contexts_dir=$1
	ensure_dir "$contexts_dir" "exist"
	ensure_file "$contexts_dir/nr" "exist" 600

	ensure_write_succ  "$contexts_dir/nr" "1" "valid input"
	test_context "$contexts_dir/0"

	ensure_write_fail "$contexts_dir/nr" "2" "only 0/1 are supported"
	test_context "$contexts_dir/0"

	ensure_write_succ "$contexts_dir/nr" "0" "valid input"
	ensure_dir "$contexts_dir/0" "not_exist"
}

test_kdamond()
{
	kdamond_dir=$1
	ensure_dir "$kdamond_dir" "exist"
	ensure_file "$kdamond_dir/state" "exist" "600"
	ensure_file "$kdamond_dir/pid" "exist" 400
	test_contexts "$kdamond_dir/contexts"
}

test_kdamonds()
{
	kdamonds_dir=$1
	ensure_dir "$kdamonds_dir" "exist"

	ensure_file "$kdamonds_dir/nr" "exist" "600"

	ensure_write_succ  "$kdamonds_dir/nr" "1" "valid input"
	test_kdamond "$kdamonds_dir/0"

	ensure_write_succ  "$kdamonds_dir/nr" "2" "valid input"
	test_kdamond "$kdamonds_dir/0"
	test_kdamond "$kdamonds_dir/1"

	ensure_write_succ "$kdamonds_dir/nr" "0" "valid input"
	ensure_dir "$kdamonds_dir/0" "not_exist"
	ensure_dir "$kdamonds_dir/1" "not_exist"
}

test_damon_sysfs()
{
	damon_sysfs=$1
	if [ ! -d "$damon_sysfs" ]
	then
		echo "$damon_sysfs not found"
		exit $ksft_skip
	fi

	test_kdamonds "$damon_sysfs/kdamonds"
}

check_dependencies()
{
	if [ $EUID -ne 0 ]
	then
		echo "Run as root"
		exit $ksft_skip
	fi
}

check_dependencies
test_damon_sysfs "/sys/kernel/mm/damon/admin"
