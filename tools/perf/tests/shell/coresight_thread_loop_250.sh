#!/bin/sh -e
# Coresight / Thread Loop 250 Threads

# SPDX-License-Identifier: GPL-2.0
# Carsten Haitzler <carsten.haitzler@arm.com>, 2021

TEST="thread_loop"
. $(dirname $0)/lib/coresight.sh
ARGS="250 1"
DATV="250th"
DATA="$DATD/perf-$TEST-$DATV.data"

perf record $PERFRECOPT -o "$DATA" "$BIN" $ARGS

perf_dump_aux_verify "$DATA" 724 11 11

err=$?
exit $err
