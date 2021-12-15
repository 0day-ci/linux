#!/bin/sh -e
# Coresight / Bubblesort Thread Small Array

# SPDX-License-Identifier: GPL-2.0
# Carsten Haitzler <carsten.haitzler@arm.com>, 2021

TEST="bubble_sort_thread"
. $(dirname $0)/lib/coresight.sh
ARGS="$DIR/small_array.txt"
DATV="small"
DATA="$DATD/perf-$TEST-$DATV.data"

echo $ARGS

perf record $PERFRECOPT -o "$DATA" "$BIN" $ARGS

perf_dump_aux_verify "$DATA" 90 7 7

err=$?
exit $err
