#!/bin/sh -e
# Coresight / Thread Loop 25 Threads

# SPDX-License-Identifier: GPL-2.0
# Carsten Haitzler <carsten.haitzler@arm.com>, 2021

TEST="thread_loop"
. $(dirname $0)/lib/coresight.sh
ARGS="25 2"
DATV="25th"
DATA="$DATD/perf-$TEST-$DATV.data"

perf record $PERFRECOPT -o "$DATA" "$BIN" $ARGS

perf_dump_aux_verify "$DATA" 388121 1255 1255

err=$?
exit $err
