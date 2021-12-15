#!/bin/sh -e
# Coresight / Unroll Loop Thread 250

# SPDX-License-Identifier: GPL-2.0
# Carsten Haitzler <carsten.haitzler@arm.com>, 2021

TEST="unroll_loop_thread"
. $(dirname $0)/lib/coresight.sh
ARGS="250"
DATV="250"
DATA="$DATD/perf-$TEST-$DATV.data"

perf record $PERFRECOPT -o "$DATA" "$BIN" $ARGS

perf_dump_aux_verify "$DATA" 544 2417 2417

err=$?
exit $err
