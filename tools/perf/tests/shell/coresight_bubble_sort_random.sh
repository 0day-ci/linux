#!/bin/sh -e
# Coresight / Bubblesort Random Array

# SPDX-License-Identifier: GPL-2.0
# Carsten Haitzler <carsten.haitzler@arm.com>, 2021

TEST="bubble_sort"
. $(dirname $0)/lib/coresight.sh
ARGS="$DIR/random_array.txt"
DATV="random"
DATA="$DATD/perf-$TEST-$DATV.data"

echo $ARGS

perf record $PERFRECOPT -o "$DATA" "$BIN" $ARGS

perf_dump_aux_verify "$DATA" 4188 1630 1630

err=$?
exit $err
