#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Print the linker name and its version in a 5 or 6-digit form.

set -e

# Convert the version string x.y.z to a canonical 5 or 6-digit form.
IFS=.
set -- $1

# If the 2nd or 3rd field is missing, fill it with a zero.
echo $((10000 * $1 + 100 * ${2:-0} + ${3:-0}))
