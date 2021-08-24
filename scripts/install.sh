#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1995 by Linus Torvalds
#
# Adapted from code in arch/i386/boot/Makefile by H. Peter Anvin
#
# Arguments:
#   $1 - kernel version
#   $2 - kernel image file
#   $3 - kernel map file
#   $4 - default install path (blank if root directory)

verify () {
	if [ ! -f "$1" ]; then
		echo >&2
		echo >&2 " *** Missing file: $1"
		echo >&2 ' *** You need to run "make" before "make install".'
		echo >&2
		exit 1
	fi
}

# Make sure the files actually exist
verify "$2"
verify "$3"

# User/arch may have a custom install script

for script in "~/bin/${INSTALLKERNEL}" "/sbin/${INSTALLKERNEL}" \
		"arch/${SRCARCH}/install.sh" "arch/${SRCARCH}/boot/install.sh"
do
	if [ -x "${script}" ]; then
		exec "${script}" "$@"
	fi
done

echo "No install script found" >&2
exit 1
