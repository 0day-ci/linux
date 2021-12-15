#!/bin/sh -e
# Coresight / Memcpy 1M 25 Threads

# SPDX-License-Identifier: GPL-2.0
# Carsten Haitzler <carsten.haitzler@arm.com>, 2021

TEST="memcpy_thread"
. $(dirname $0)/lib/coresight.sh
ARGS="1024 25 1"
DATV="1m_25"
DATA="$DATD/perf-$TEST-$DATV.data"

perf record $PERFRECOPT -o "$DATA" "$BIN" $ARGS

perf_dump_aux_verify "$DATA" 1 44 43

err=$?
exit $err
