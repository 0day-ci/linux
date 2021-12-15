#!/bin/sh -e
# Coresight / Memcpy 64k 250 Threads

# SPDX-License-Identifier: GPL-2.0
# Carsten Haitzler <carsten.haitzler@arm.com>, 2021

TEST="memcpy_thread"
. $(dirname $0)/lib/coresight.sh
ARGS="64 250 1"
DATV="64k_250"
DATA="$DATD/perf-$TEST-$DATV.data"

perf record $PERFRECOPT -o "$DATA" "$BIN" $ARGS

perf_dump_aux_verify "$DATA" 340 1878 1878

err=$?
exit $err
