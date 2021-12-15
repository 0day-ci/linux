#!/bin/sh -e
# Coresight / Memcpy 32M 2 Threads

# SPDX-License-Identifier: GPL-2.0
# Carsten Haitzler <carsten.haitzler@arm.com>, 2021

TEST="memcpy_thread"
. $(dirname $0)/lib/coresight.sh
ARGS="32768 2 1"
DATV="32m_2"
DATA="$DATD/perf-$TEST-$DATV.data"

perf record $PERFRECOPT -o "$DATA" "$BIN" $ARGS

perf_dump_aux_verify "$DATA" 3 12 12

err=$?
exit $err
