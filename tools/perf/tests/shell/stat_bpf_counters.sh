#!/bin/sh
# perf stat --bpf-counters test
# SPDX-License-Identifier: GPL-2.0

set -e

# check whether $2 is within +/- 10% of $1
compare_number()
{
	first_num=$1
	second_num=$2

	# upper bound is first_num * 110%
	upper=$(( $first_num + $first_num / 10 ))
	# lower bound is first_num * 90%
	lower=$(( $first_num - $first_num / 10 ))

	if [ $second_num -gt $upper ] || [ $second_num -lt $lower ]; then
		echo "The difference between $first_num and $second_num are greater than 10%."
		exit 1
	fi
}

# skip if --bpf-counters is not supported
perf stat --bpf-counters true > /dev/null 2>&1 || exit 2

# skip if stressapptest is not available
stressapptest -s 1 -M 100 -m 1 > /dev/null 2>&1 || exit 2

base_cycles=$(perf stat --no-big-num -e cycles -- stressapptest -s 3 -M 100 -m 1 2>&1 | grep -e cycles | awk '{print $1}')
bpf_cycles=$(perf stat --no-big-num --bpf-counters -e cycles -- stressapptest -s 3 -M 100 -m 1 2>&1 | grep -e cycles | awk '{print $1}')

compare_number $base_cycles $bpf_cycles
exit 0
