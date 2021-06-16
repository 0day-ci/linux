#!/bin/sh -efu
# SPDX-License-Identifier: GPL-2.0

# To get the traditional sort order that uses native byte values
export LC_ALL=C

cd ${0%/*}

src=spelling.txt
comments=`sed -n '/#/p' $src`

# Convert the format of 'codespell' to the current
sed -r -i 's/ ==> /||/' $src

# For all spelling "mistake||correction" pairs(non-comment lines):
# Sort based on "correction", then "mistake", and remove duplicates
sed -n '/#/!p' $src | sort -u -t '|' -k 3 -k 1 -o $src

# Backfill comment lines
ln=0
echo "$comments" | while read line
do
	let ln+=1
	sed -i "$ln i\\$line" $src
done

cd - > /dev/null
