#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later

if [[ ! -w /dev/crypto/nx-gzip ]]; then
	echo "WARN: Can't access /dev/crypto/nx-gzip, skipping"
	exit 0
fi

timeout 5 ./inject-ra-err

# 128 + 7 (SIGBUS) = 135, 128 is a exit code with special meaning.
if [ $? -ne 135 ]; then
	echo "FAILED: Real address or Control memory access error not handled"
	exit $?
fi

echo "OK: Real address or Control memory access error is handled"
exit 0
