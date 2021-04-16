#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# This will enable the resume firmware request call. Since we have no
# control over a guest / hypervisor, the caller is in charge of the
# actual suspend / resume cycle. This only enables the test to be triggered
# upon resume.
set -e

TEST_REQS_FW_SYSFS_FALLBACK="no"
TEST_REQS_FW_SET_CUSTOM_PATH="yes"
TEST_DIR=$(dirname $0)
FWPATH=""
source $TEST_DIR/fw_lib.sh

check_mods
check_setup
verify_reqs
#setup_tmp_file --skip-file-creation

test_resume()
{
	if [ ! -f $DIR/reset ]; then
		echo "Configuration triggers not present, ignoring test"
		exit $ksft_skip
	fi
}

release_all_firmware()
{
	echo 1 >  $DIR/release_all_firmware
}

config_enable_resume_test()
{
	echo 1 >  $DIR/config_enable_resume_test
	if [ "$HAS_FW_LOADER_USER_HELPER" = "yes" ]; then
		# Turn down the timeout so failures don't take so long.
		echo 1 >/sys/class/firmware/timeout
	fi
}

config_disable_resume_test()
{
	echo 0 >  $DIR/config_enable_resume_test
}

usage()
{
	echo "Usage: $0 [ -v ] | [ -h | --help]"
	echo ""
	echo "    --check-resume-test   Verify resume test"
	echo "    -h|--help             Help"
	echo
	echo "Without any arguments this will enable the resume firmware test"
	echo "after suspend. To verify that the test went well, run with -v".
	echo
	exit 1
}

verify_resume_test()
{
	trap "test_finish" EXIT
}

parse_args()
{
	if [ $# -eq 0 ]; then
		config_enable_resume_test
	else
		if [[ "$1" = "--check-resume-test" ]]; then
			config_disable_resume_test
			verify_resume_test
		else
			usage
		fi
	fi
}

exit 0
