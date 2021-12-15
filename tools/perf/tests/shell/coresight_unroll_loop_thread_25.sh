#!/bin/sh -e
# Coresight / Unroll Loop Thread 25

# SPDX-License-Identifier: GPL-2.0
# Carsten Haitzler <carsten.haitzler@arm.com>, 2021

TEST="unroll_loop_thread"
. $(dirname $0)/lib/coresight.sh
ARGS="25"
DATV="25"
DATA="$DATD/perf-$TEST-$DATV.data"

perf record $PERFRECOPT -o "$DATA" "$BIN" $ARGS

perf_dump_aux_verify "$DATA" 72 26 25

err=$?
exit $err
