#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

set -e

source "$(dirname $0)"/lib.sh

while getopts d flag; do
  case "${flag}" in
    d) debug=1;;
  esac
done

gen_cpio_list empty
gen_initrd empty
run_qemu_empty $debug
