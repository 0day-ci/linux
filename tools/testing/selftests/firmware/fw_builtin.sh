#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Test loading built-in firmware
set -e

TEST_REQS_FW_SYSFS_FALLBACK="no"
TEST_REQS_FW_SET_CUSTOM_PATH="yes"
TEST_DIR=$(dirname $0)
source $TEST_DIR/fw_lib.sh

check_mods
check_setup
verify_reqs
setup_tmp_file

trap "test_finish" EXIT

if [ "$HAS_FW_LOADER_USER_HELPER" = "yes" ]; then
	# Turn down the timeout so failures don't take so long.
	echo 1 >/sys/class/firmware/timeout
fi

# built-in firmware support can be optional to test
if [[ "$HAS_FW_LOADER_BUILTIN" != "yes" || "$HAS_TEST_FIRMWARE_BUILTIN" != "yes" ]]; then
	exit $ksft_skip
fi

echo "Testing builtin firmware API ... "

config_trigger_builtin()
{
	echo -n 1 > $DIR/trigger_request_builtin
}

test_builtin_firmware()
{
	echo -n "Testing firmware_request_builtin() ... "
	config_reset
	config_set_name $TEST_FIRMWARE_BUILTIN_FILENAME
	config_trigger_builtin
	echo OK
	# Verify the contents are what we expect.
	echo -n "Verifying file integrity ..."
	if ! diff -q "$FW" /dev/test_firmware >/dev/null ; then
		echo "$0: firmware loaded content differs" >&2
		exit 1
	else
		echo "firmware content matches what we expect - OK"
	fi
}

test_builtin_firmware_nofile()
{
	echo -n "Testing firmware_request_builtin() with fake file... "
	config_reset
	config_set_name fake-${TEST_FIRMWARE_BUILTIN_FILENAME}
	if config_trigger_builtin 2> /dev/null; then
		echo "$0: firmware shouldn't have loaded" >&2
	fi
	echo "OK"
}

test_builtin_firmware
test_builtin_firmware_nofile

# Ensure test_fw_config->is_builtin is set back to false
# otherwise we won't be able to diff against the right target
# firmware for other tests.
config_reset
