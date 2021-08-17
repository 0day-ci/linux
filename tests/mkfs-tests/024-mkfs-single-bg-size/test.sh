#!/bin/bash
# Regression test for mkfs.btrfs using single data block group
# Expected behavior: it should create a data block group of 1G, or at least up
# to 10% of the filesystem size if it's size is < 50G.
# Commit that fixed the issue: 222e622683e9 ("btrfs-progs: drop type check in
# init_alloc_chunk_ctl_policy_regular")

source "$TEST_TOP/common"

check_prereq mkfs.btrfs
check_prereq btrfs

verify_single_bg_size()
{
	local bg_size
	local dev_size
	local expected_bg_size
	dev_size="$1"
	expected_bg_size="$2"

	prepare_test_dev $dev_size
	bg_size=`$SUDO_HELPER "$TOP/mkfs.btrfs" -f "$TEST_DEV" | awk '/single/ {print $NF}'`

	if [[ "$bg_size" != "$expected_bg_size" ]]; then
		_fail "mkfs.btrfs created a data block group of size $bg_size, but expected $expected_bg_size"
	fi
}

setup_root_helper

# using 5G as disk size should create a bg with 10% of the total disk size,
# which is 512MiB
verify_single_bg_size "5G" "512.00MiB"

# Same here, 10% of the disk size
verify_single_bg_size "1G" "102.38MiB"
verify_single_bg_size "1500M" "150.00MiB"
verify_single_bg_size "7G" "716.75MiB"
verify_single_bg_size "9G" "921.56MiB"

# From 1G on, it should create a block group of 1G of size
verify_single_bg_size "10G" "1.00GiB"
verify_single_bg_size "11G" "1.00GiB"
verify_single_bg_size "50G" "1.00GiB"
verify_single_bg_size "51G" "1.00GiB"
