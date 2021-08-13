#!/bin/bash
# perf stat csv output test
# SPDX-License-Identifier: GPL-2.0
# Tests various perf stat CSV output commands for the
# correct number of fields and the CSV separator set to ','.

set -e

pythonchecker=$(dirname $0)/lib/perf_csv_output_lint.py
file="/proc/sys/kernel/perf_event_paranoid"
paranoia=$(cat "$file" | grep -o -E '[0-9]+')
echo $paranoia

check_no_args()
{
	perf stat -x, sleep 1 2>&1 | \
	python $pythonchecker --no-args --separator
}

if [ $paranoia -gt 0 ]; then
	echo check_all_cpus test skipped because of paranoia level.
else
	check_all_cpus()
	{
		perf stat -x, -a 2>&1 sleep 1 | \
		python $pythonchecker --all-cpus --separator
	}
	check_all_cpus
fi

check_interval()
{
	perf stat -x, -I 1000 2>&1 sleep 1 | \
	python $pythonchecker --interval --separator
}

check_all_cpus_no_aggr()
{
	perf stat -x, -A -a --no-merge 2>&1 sleep 1 | \
	python $pythonchecker --all-cpus-no-aggr --separator
}

check_event()
{
	perf stat -x, -e cpu-clock 2>&1 sleep 1 | \
	python $pythonchecker --event --separator
}

if [ $paranoia -gt 0 ]; then
	echo check_all_cpus test skipped because of paranoia level.
else
	check_per_core()
	{
		perf stat -x, --per-core -a 2>&1 sleep 1 | \
		python $pythonchecker --per-core --separator
	}
	check_per_core
fi

if [ $paranoia -gt 0 ]; then
	echo check_all_cpus test skipped because of paranoia level.
else
	check_per_thread()
	{
		perf stat -x, --per-thread -a 2>&1 sleep 1 | \
		python $pythonchecker --per-thread --separator
	}
	check_per_thread
fi

if [ $paranoia -gt 0 ]; then
	echo check_per_die test skipped because of paranoia level.
else
	check_per_die()
	{
		perf stat -x, --per-die -a 2>&1 sleep 1 | \
		python $pythonchecker --per-die --separator
	}
	check_per_die
fi

if [ $paranoia -gt 0 ]; then
	echo check_per_node test skipped because of paranoia level.
else
	check_per_node()
	{
		perf stat -x, --per-node -a 2>&1 sleep 1 | \
		python $pythonchecker --per-node --separator
	}
	check_per_node
fi

if [ $paranoia -gt 0 ]; then
	echo check_per_socket test skipped because of paranoia level.
else
	check_per_socket()
	{
		perf stat -x, --per-socket -a 2>&1 sleep 1 | \
		python $pythonchecker --per-socket --separator
	}
	check_per_socket
fi

check_no_args
check_interval
check_all_cpus_no_aggr
check_event

exit 0
