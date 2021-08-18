#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

set -e

source "$(dirname $0)"/lib.sh

while getopts d flag; do
  case "${flag}" in
    d) debug=1;;
  esac
done

if [ -z $debug ]; then
  trap "rm -rf testdir" EXIT
fi
mkdir -p testdir
head -c 1474560 /dev/zero > testdir/floppy.raw

gen_cpio_list rdwr
gen_initrd rdwr
run_qemu_rdwr_img testdir/floppy.raw $debug
