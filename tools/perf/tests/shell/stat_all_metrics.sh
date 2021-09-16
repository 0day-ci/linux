#!/bin/sh
# perf all metrics test
# SPDX-License-Identifier: GPL-2.0

set -e

for m in `perf list --raw-dump metrics`; do
  echo "Testing $m"
  result=$(perf stat -M "$m" true)
  if [[ "$result" =~ "$m" ]]; then
    echo "Metric not printed: $m"
    exit 1
  fi
done

exit 0
