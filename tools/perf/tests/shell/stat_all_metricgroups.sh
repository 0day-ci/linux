#!/bin/sh
# perf all metricgroups test
# SPDX-License-Identifier: GPL-2.0

set -e

for m in $(perf list --raw-dump metricgroups); do
  echo "Testing $m"
  perf stat -M "$m" perf bench internals synthesize
done

exit 0
