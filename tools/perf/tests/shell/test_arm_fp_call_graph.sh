#!/bin/sh
# Check frame pointer call-graphs are correct on Arm64

# SPDX-License-Identifier: GPL-2.0
# German Gomez <german.gomez@arm.com>, 2022

# This test checks that the perf tool injects the missing caller of a
# leaf function on Arm64 when unwinding using frame pointers.
# See: https://lore.kernel.org/r/20211217154521.80603-7-german.gomez@arm.com

# Only run this test on Arm64
lscpu | grep -q "aarch64" || return 2

if ! [ -x "$(command -v cc)" ]; then
	echo "failed: no compiler, install gcc"
	exit 2
fi

PERF_DATA=$(mktemp /tmp/__perf_test.perf.data.XXXXX)
TEST_PROGRAM_SOURCE=$(mktemp /tmp/test_program.XXXXX.c)
TEST_PROGRAM=$(mktemp /tmp/test_program.XXXXX)
SCRIPT_FILE=$(mktemp /tmp/perf.script.XXXXX)

cleanup_files() {
	rm -f $PERF_DATA*
	rm -f $TEST_PROGRAM_SOURCE
	rm -f $TEST_PROGRAM
	rm -f $SCRIPT_FILE
}

trap cleanup_files exit term int

# compile test program
cat << EOF > $TEST_PROGRAM_SOURCE
int a = 0;
void leaf(void) {
  for (int i = 0; i < 10000000; i++)
    a *= a;
}
void parent(void) {
  leaf();
}
int main(void) {
  parent();
  return 0;
}
EOF

CFLAGS="-O0 -fno-inline -fno-omit-frame-pointer"
cc $CFLAGS $TEST_PROGRAM_SOURCE -o $TEST_PROGRAM || exit 1

perf record --call-graph fp -o $PERF_DATA -- $TEST_PROGRAM

# search for the following pattern in perf-script output
#     734 leaf+0x18 (...)
#     78b parent+0xb (...)
#     7a4 main+0xc (...)

perf script -i $PERF_DATA | egrep "[0-9a-f]+ +leaf" -A2 -m1 > $SCRIPT_FILE

egrep -q " +leaf\+0x[0-9a-f]+" $SCRIPT_FILE && \
egrep -q " +parent\+0x[0-9a-f]+" $SCRIPT_FILE && \
egrep -q " +main\+0x[0-9a-f]+" $SCRIPT_FILE
err=$?

echo -n "Check frame pointer call-graphs on Arm64: "
if [ $err != 0 ]; then
	echo "FAIL"
	exit 1
else
	echo "PASS"
fi

exit 0
