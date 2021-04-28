#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Adapted from tools/perf/check-headers.sh

FILES='
arch/x86/include/asm/cpufeatures.h
arch/x86/include/asm/disabled-features.h
arch/x86/include/asm/msr-index.h
arch/x86/include/asm/required-features.h
arch/x86/include/uapi/asm/kvm.h
include/linux/bits.h
include/linux/const.h
include/uapi/asm-generic/bitsperlong.h
include/uapi/linux/const.h
include/vdso/bits.h
include/vdso/const.h
'

check_2 () {
  file1=$1
  file2=$2

  shift
  shift

  cmd="diff $* $file1 $file2 > /dev/null"

  test -f $file2 && {
    eval $cmd || {
      echo "Warning: Kernel header at '$file1' differs from latest version at '$file2'" >&2
      echo diff -u $file1 $file2
    }
  }
}

check () {
  file=$1

  shift

  check_2 tools/$file $file $*
}

# Check if we are at the right place (we have the kernel headers)
# (tools/testing/selftests/kvm/../../../../include)
test -d ../../../../include || exit 0

cd ../../../..

# simple diff check
for i in $FILES; do
  check $i -B
done

# diff with extra ignore lines
check include/linux/build_bug.h       '-I "^#\(ifndef\|endif\)\( \/\/\)* static_assert$"'

# diff non-symmetric files
check_2 tools/testing/selftests/kvm/include/x86_64/asm/cpufeature.h arch/x86/include/asm/cpufeature.h
check_2 tools/testing/selftests/kvm/include/x86_64/reverse-cpuid.h arch/x86/kvm/reverse-cpuid.h


cd tools/testing/selftests/kvm
