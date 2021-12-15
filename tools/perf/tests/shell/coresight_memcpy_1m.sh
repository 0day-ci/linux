#!/bin/sh -e
# Coresight / Memcpy 1M

# SPDX-License-Identifier: GPL-2.0
# Carsten Haitzler <carsten.haitzler@arm.com>, 2021

TEST="memcpy"
. $(dirname $0)/lib/coresight.sh
ARGS="1024 2"
DATV="1m"
DATA="$DATD/perf-$TEST-$DATV.data"

perf record $PERFRECOPT -o "$DATA" "$BIN" $ARGS

perf_dump_aux_verify "$DATA" 39 766 766

err=$?
exit $err
