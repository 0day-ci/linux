#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# perf stat JSON output linter
# Tests various perf stat JSON output commands for the
# correct number of fields.

set -e
set -x

pythonchecker=$(dirname $0)/lib/perf_json_output_lint.py
file="/proc/sys/kernel/perf_event_paranoid"
paranoia=$(cat "$file" | grep -o -E '[0-9]+')

check_no_args()
{
	perf stat -j sleep 1 2>&1 | \
	python $pythonchecker --no-args
}

if [ $paranoia -gt 0 ];
then
	echo check_all_cpus test skipped because of paranoia level.
else
	check_all_cpus()
	{
		perf stat -j -a 2>&1 sleep 1 | \
		python $pythonchecker --all-cpus
	}
	check_all_cpus
fi

check_interval()
{
	perf stat -j -I 1000 2>&1 sleep 1 | \
	python $pythonchecker --interval
}

check_all_cpus_no_aggr()
{
	perf stat -j -A -a --no-merge 2>&1 sleep 1 | \
	python $pythonchecker --all-cpus-no-aggr
}

check_event()
{
	perf stat -j -e cpu-clock 2>&1 sleep 1 | \
	python $pythonchecker --event
}

if [ $paranoia -gt 0 ];
then
	echo check_all_cpus test skipped because of paranoia level.
else
	check_per_core()
	{
		perf stat -j --per-core -a 2>&1 sleep 1 | \
		python $pythonchecker --per-core
	}
	check_per_core
fi

if [ $paranoia -gt 0 ];
then
	echo check_all_cpus test skipped because of paranoia level.
else
	check_per_thread()
	{
		perf stat -j --per-thread -a 2>&1 sleep 1 | \
		python $pythonchecker --per-thread
	}
	check_per_thread
fi

if [ $paranoia -gt 0 ];
then
	echo check_per_die test skipped because of paranoia level.
else
	check_per_die()
	{
		perf stat -j --per-die -a 2>&1 sleep 1 | \
		python $pythonchecker --per-die
	}
	check_per_die
fi

if [ $paranoia -gt 0 ];
then
	echo check_per_node test skipped because of paranoia level.
else
	check_per_node()
	{
		perf stat -j --per-node -a 2>&1 sleep 1 | \
		python $pythonchecker --per-node
	}
	check_per_node
fi

if [ $paranoia -gt 0 ];
then
	echo check_per_socket test skipped because of paranoia level.
else
	check_per_socket()
	{
		perf stat -j --per-socket -a 2>&1 sleep 1 | \
		python $pythonchecker --per-socket
	}
	check_per_socket
fi

check_no_args
check_interval
check_all_cpus_no_aggr
check_event
exit 0
