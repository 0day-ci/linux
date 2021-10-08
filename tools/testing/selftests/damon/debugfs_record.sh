#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

source ./_chk_dependency.sh

restore_attrs()
{
	echo $ORIG_ATTRS > $DBGFS/attrs
	echo $ORIG_TARGET_IDS > $DBGFS/target_ids
	echo $ORIG_RECORD > $DBGFS/record
}

ORIG_ATTRS=$(cat $DBGFS/attrs)
ORIG_TARGET_IDS=$(cat $DBGFS/target_ids)
ORIG_RECORD=$(cat $DBGFS/record)

rfile=$pwd/damon.data

rm -f $rfile
ATTRS="5000 100000 1000000 10 1000"
echo $ATTRS > $DBGFS/attrs
echo 4096 $rfile > $DBGFS/record
sleep 5 &
echo $(pidof sleep) > $DBGFS/target_ids
echo on > $DBGFS/monitor_on
sleep 0.5
killall sleep
echo off > $DBGFS/monitor_on

sync

if [ ! -f $rfile ]
then
	echo "record file not made"
	restore_attrs

	exit 1
fi

python3 ./_chk_record.py $rfile --attrs "$ATTRS"
if [ $? -ne 0 ]
then
	echo "record file is wrong"
	restore_attrs
	exit 1
fi

rm -f $rfile
restore_attrs
echo "PASS"
