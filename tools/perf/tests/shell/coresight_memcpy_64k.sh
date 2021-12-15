#!/bin/sh -e
# Coresight / Memcpy 64K

# SPDX-License-Identifier: GPL-2.0
# Carsten Haitzler <carsten.haitzler@arm.com>, 2021

TEST="memcpy"
. $(dirname $0)/lib/coresight.sh
ARGS="64 40"
DATV="64k"
DATA="$DATD/perf-$TEST-$DATV.data"

perf record $PERFRECOPT -o "$DATA" "$BIN" $ARGS

perf_dump_aux_verify "$DATA" 40 934 934

err=$?
exit $err
