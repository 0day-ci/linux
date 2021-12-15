#!/bin/sh -e
# Coresight / Unroll Loop Thread 1

# SPDX-License-Identifier: GPL-2.0
# Carsten Haitzler <carsten.haitzler@arm.com>, 2021

TEST="unroll_loop_thread"
. $(dirname $0)/lib/coresight.sh
ARGS="1"
DATV="1"
DATA="$DATD/perf-$TEST-$DATV.data"

perf record $PERFRECOPT -o "$DATA" "$BIN" $ARGS

perf_dump_aux_verify "$DATA" 118 14 14

err=$?
exit $err
