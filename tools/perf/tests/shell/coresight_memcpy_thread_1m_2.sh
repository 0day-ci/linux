#!/bin/sh -e
# Coresight / Memcpy 1M 2 Threads

# SPDX-License-Identifier: GPL-2.0
# Carsten Haitzler <carsten.haitzler@arm.com>, 2021

TEST="memcpy_thread"
. $(dirname $0)/lib/coresight.sh
ARGS="1024 2 400"
DATV="1m_2"
DATA="$DATD/perf-$TEST-$DATV.data"

perf record $PERFRECOPT -o "$DATA" "$BIN" $ARGS

perf_dump_aux_verify "$DATA" 125 26 26

err=$?
exit $err
