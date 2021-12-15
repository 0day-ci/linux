#!/bin/sh -e
# Coresight / Memcpy 32M

# SPDX-License-Identifier: GPL-2.0
# Carsten Haitzler <carsten.haitzler@arm.com>, 2021

TEST="memcpy"
. $(dirname $0)/lib/coresight.sh
ARGS="32768 1"
DATV="32m"
DATA="$DATD/perf-$TEST-$DATV.data"

perf record $PERFRECOPT -o "$DATA" "$BIN" $ARGS

perf_dump_aux_verify "$DATA" 39 7804 7804

err=$?
exit $err
