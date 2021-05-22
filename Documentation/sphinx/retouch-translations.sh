#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Copyright 2021 Akira Yokosawa <akiyks@gmail.com>
#
# Retouch translations.tex to add CJK on/off macros.
# The substitution rules need updates when there is some change in
# the ordering of chapters.
#
# Path of the file to be retouched is passed in command argument $1
# from docs Makefile.
# If there is no need of retouch, do nothing.

retouch=$1

if [ -e $retouch ]; then
	if grep -q 'kerneldocCJKon\\chapter' $retouch ; then
		exit 0
	fi
	sed -i -e 's/\(\\sphinxtableofcontents\)/\\kerneldocCJKon\1/' \
	    -e 's/\(\\chapter{中文\)/\\kerneldocCJKon\1/' \
	    -e 's/\(\\chapter{Traduzione[^}]*}\)/\1\\kerneldocCJKoff/' \
	    -e 's/\(\\chapter{한국어\)/\\kerneldocCJKon\1/' \
	    -e 's/\(\\chapter{Disclaimer[^}]*}\)/\1\\kerneldocCJKoff/' \
	    $retouch
	echo "$retouch retouched."
	exit 0
else
	exit 0
fi
