#!/usr/bin/python
# SPDX-License-Identifier: GPL-2.0

from __future__ import print_function
import argparse
import sys

# Basic sanity check of perf JSON output as specified in the man page.
# Currently just checks the number of fields per line in output.

ap = argparse.ArgumentParser()
ap.add_argument('--no-args', action='store_true')
ap.add_argument('--interval', action='store_true')
ap.add_argument('--all-cpus-no-aggr', action='store_true')
ap.add_argument('--all-cpus', action='store_true')
ap.add_argument('--event', action='store_true')
ap.add_argument('--per-core', action='store_true')
ap.add_argument('--per-thread', action='store_true')
ap.add_argument('--per-die', action='store_true')
ap.add_argument('--per-node', action='store_true')
ap.add_argument('--per-socket', action='store_true')
args = ap.parse_args()

Lines = sys.stdin.readlines()
ch = ','


def check_json_output(exp):
  for line in Lines:
    if 'failed' not in line:
      count = 0
      count = line.count(ch)
      if count != exp:
        sys.stdout.write(''.join(Lines))
        raise RuntimeError('wrong number of fields. counted {0}'
                           ' expected {1} in {2}\n'.format(count, exp, line))


try:
  if args.no_args or args.all_cpus or args.event:
    check_json_output(6)
  if args.interval or args.per_thread:
    check_json_output(7)
  if args.per_core or args.per_socket or args.per_node or args.per_die:
    check_json_output(8)

except:
  sys.stdout.write('Test failed for input:\n' + ''.join(Lines))
  raise
