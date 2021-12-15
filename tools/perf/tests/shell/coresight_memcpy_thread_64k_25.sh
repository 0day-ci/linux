#!/bin/sh -e
# Coresight / Memcpy 64k 25 Threads

# SPDX-License-Identifier: GPL-2.0
# Carsten Haitzler <carsten.haitzler@arm.com>, 2021

TEST="memcpy_thread"
. $(dirname $0)/lib/coresight.sh
ARGS="64 25 2"
DATV="64k_25"
DATA="$DATD/perf-$TEST-$DATV.data"

perf record $PERFRECOPT -o "$DATA" "$BIN" $ARGS

perf_dump_aux_verify "$DATA" 118 31 31

err=$?
exit $err
