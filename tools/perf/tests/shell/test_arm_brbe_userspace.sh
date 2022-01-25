#!/bin/bash
# Arm64 BRBE userspace branches

# SPDX-License-Identifier: GPL-2.0
# German Gomez <german.gomez@arm.com>, 2022

PERF_DATA=$(mktemp /tmp/__perf_test.perf.data.XXXXX)
TEST_PROGRAM_SOURCE=$(mktemp /tmp/brbe_test_program.XXXXX.c)
TEST_PROGRAM=$(mktemp /tmp/brbe_test_program.XXXXX)

cleanup_files() {
	rm -f $PERF_DATA*
	rm -f $TEST_PROGRAM_SOURCE
	rm -f $TEST_PROGRAM
}

trap cleanup_files exit term int

test_brbe_user() {
	lscpu | grep -q "aarch64" || return $?
	perf record -o $PERF_DATA --branch-filter any,u -- true > /dev/null 2>&1
}

test_brbe_user || exit 2

# Skip if there's no compiler
# We need it to compile the test program
if ! [ -x "$(command -v cc)" ]; then
	echo "failed: no compiler, install gcc"
	exit 2
fi

script_has_branch() {
	local from="$1\+0x[0-9a-f]+"
	local to="$2\+0x[0-9a-f]+"
	perf script -i $PERF_DATA --fields brstacksym | egrep -qm 1 " +$from\/$to\/"
}

# compile test program
cat << EOF > $TEST_PROGRAM_SOURCE
void f2() {
}
void f1() {
  f2();
}
void f0() {
  f1();
  f2();
}
int main() {
  while(1) {
    f0();
    f1();
  }
  return 0;
}
EOF

CFLAGS="-O0 -fno-inline -static"
cc $CFLAGS $TEST_PROGRAM_SOURCE -o $TEST_PROGRAM || exit 1

perf record -o $PERF_DATA --branch-filter any,u -- timeout 1 $TEST_PROGRAM

script_has_branch "main" "f0" &&
	script_has_branch "main" "f1" &&
	script_has_branch "f0" "f1" &&
	script_has_branch "f0" "f2" &&
	script_has_branch "f1" "f2" &&
	script_has_branch "main" "main"
err=$?

echo -n "BRB user branches: "
if [ $err != 0 ]; then
	echo "FAIL"
	exit 1
else
	echo "PASS"
fi

exit 0
