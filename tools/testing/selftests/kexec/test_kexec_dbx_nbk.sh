#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Loading a dbx black listed kernel image via the kexec_file_load syscall

. ./kexec_common_lib.sh

TEST="KEXEC_FILE_LOAD_DBX_NB"

# Look for kernel matching with blacklisted binary hash
check_for_non_blacklist_kernel()
{
	local module_sig_string="~Module signature appended~"
	local ret=0

	tail --bytes $((${#module_sig_string} + 1)) $KERNEL_IMAGE | \
		grep -q "$module_sig_string"
	if [ $? -eq 0 ]; then
		log_info "Found $KERNEL_IMAGE with Module signature"
		# Remove the signature
		local hash=$(../../../../scripts/extract-module-sig.pl -0\
				${KERNEL_IMAGE} 2> /dev/null | sha256sum -\
				|awk -F" " '{print $1}')
		for b_hash in $BLACKLIST_BIN_HASH
		do
			if ! [ "$hash" == "$b_hash" ]; then
				ret=1
			fi
		done
	fi
	return $ret
}

kexec_file_load_nbk_test()
{
	local succeed_msg=$1
	local failed_msg=$2

	line=$(kexec --load --kexec-file-syscall $KERNEL_IMAGE 2>&1)

	if [ $? -eq 0 -a $secureboot -eq 1 ]; then
		kexec --unload --kexec-file-syscall
		log_pass "$succeed_msg (secureboot enabled)"
	elif (echo $line | grep -q "Permission denied"); then
		log_fail "$failed_msg (Permission denied)"
	fi

}

common_steps_for_dbx

check_for_non_blacklist_kernel
bk=$?

if [ $bk -eq 0 ]; then
	log_skip "Found blacklisted hash matching kernel"
fi

# Loading the black listed kernel image via kexec_file_load syscall should fail
succeed_msg="kexec non blacklisted kernel image succeeded"
failed_msg="kexec non blacklisted kernel image failed"
kexec_file_load_nbk_test "$succeed_msg" "$failed_msg"
