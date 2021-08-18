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
echo -n TEST > testdir/test

gen_cpio_list rdonly
gen_initrd rdonly
run_qemu_rdonly_fat testdir $debug
