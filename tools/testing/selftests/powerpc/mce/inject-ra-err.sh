#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later

if [[ ! -w /dev/crypto/nx-gzip ]]; then
	echo "WARN: Can't access /dev/crypto/nx-gzip, skipping"
	exit 0
fi

# Timeout in 5 seconds, If not handled it may run indefinitely.
timeout 5 ./inject-ra-err

# 128 + 7 (SIGBUS) = 135, 128 is a exit Code With Special Meaning.
if [ $? -ne 135 ]; then
	echo "FAILED: Control memory access error not handled"
	exit $?
fi

echo "OK: Control memory access error is handled"
exit 0
