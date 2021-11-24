#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Loading a dbx black listed kernel image via the kexec_file_load syscall

. ./kexec_common_lib.sh

TEST="KEXEC_FILE_LOAD_DBX"

# Read KERNEL_DBX_IMAGE from the environment variable
KERNEL_DBX_IMAGE=$(printenv | grep KERNEL_DBX_IMAGE | cut -d"=" -f2)
if [ -z $KERNEL_DBX_IMAGE ]; then
	env_var="KERNEL_DBX_IMAGE"
	log_skip "$env_var not found, set using 'export $env_var=value'"
#	KERNEL_DBX_IMAGE=$KERNEL_IMAGE
#	log_info "Continuing with booted kernel"
fi

# Look for kernel matching with blacklisted binary hash
check_for_blacklist_kernel()
{
	local module_sig_string="~Module signature appended~"
	local ret=0

	tail --bytes $((${#module_sig_string} + 1)) $KERNEL_DBX_IMAGE | \
		grep -q "$module_sig_string"
	if [ $? -eq 0 ]; then
		log_info "Found $KERNEL_DBX_IMAGE with Module signature"
		# Remove the signature
		local hash=$(../../../../scripts/extract-module-sig.pl -0\
				${KERNEL_DBX_IMAGE} 2> /dev/null | sha256sum -\
				|awk -F" " '{print $1}')
		for b_hash in $BLACKLIST_BIN_HASH
		do
			# Make sure test is not going to run on booted kernel
			if [ "$hash" == "$b_hash" -a \
				"$KERNEL_IMAGE" != "$KERNEL_DBX_IMAGE" ]; then
				KERNEL_IMAGE=$KERNEL_DBX_IMAGE
				ret=1
			fi
		done
	fi
	return $ret
}

kexec_file_load_dbx_test()
{
	local succeed_msg=$1
	local failed_msg=$2

	line=$(kexec --load --kexec-file-syscall $KERNEL_IMAGE 2>&1)

	if [ $? -eq 0 ]; then
		kexec --unload --kexec-file-syscall

		# In secureboot mode with an architecture  specific
		# policy, make sure dbx blacklist exists
		if [ $secureboot -eq 1 ] && [ $dbx -eq 1 ]; then
			log_fail "$succeed_msg (secureboot and dbx enabled)"
		# secureboot mode disabled, and dbx blacklist exists
		elif [ $dbx -eq 1 ]; then
			log_fail "$succeed_msg (dbx enabled)"
		fi
	fi

	# Check the reason for the kexec_file_load failure
	if (echo $line | grep -q "Permission denied"); then
		if [ $dbx -eq 1 ]; then
			log_pass "$failed_msg (Permission denied)"
		else
			log_fail "$succeed_msg"
		fi
	fi

	return 0
}

common_steps_for_dbx

check_for_blacklist_kernel
bk=$?

if [ $bk -eq 0 ]; then
	log_skip "Not found blacklisted hash matching kernel"
fi

# Loading the black listed kernel image via kexec_file_load syscall should fail
succeed_msg="kexec blacklisted kernel image succeeded"
failed_msg="kexec blacklisted kernel image failed"
kexec_file_load_dbx_test "$succeed_msg" "$failed_msg"
