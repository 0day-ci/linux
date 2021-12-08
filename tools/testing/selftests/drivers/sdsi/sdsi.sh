#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Runs tests for the intel_sdsi driver

if ! /sbin/modprobe -q -r intel_sdsi; then
	echo "drivers/sdsi: [SKIP]"
	exit 77
fi

if /sbin/modprobe -q intel_sdsi; then
	python3 -m pytest sdsi_test.py
	/sbin/modprobe -q -r intel_sdsi

	echo "drivers/sdsi: ok"
else
	echo "drivers/sdsi: [FAIL]"
	exit 1
fi
