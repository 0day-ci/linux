#!/bin/bash
#

source "$TEST_TOP/common"


check_prereq mkfs.btrfs
check_prereq btrfs
setup_root_helper

setup_loopdevs 2
prepare_loopdevs

run_check $SUDO_HELPER "$TOP/mkfs.btrfs" -f -draid1 -mraid1 "${loopdevs[@]}"
run_check $SUDO_HELPER mount -o space_cache=v2 "${loopdevs[1]}" "$TEST_MNT"
run_check $SUDO_HELPER umount "$TEST_MNT"
run_check $SUDO_HELPER losetup -d "${loopdevs[2]}"
run_check $SUDO_HELPER mount -o degraded "${loopdevs[1]}" "$TEST_MNT"
run_check $SUDO_HELPER touch "$TEST_MNT/file1.txt"
run_check $SUDO_HELPER umount "$TEST_MNT"
run_check $SUDO_HELPER losetup "${loopdevs[2]}" "$loopdev_prefix"2
run_check $SUDO_HELPER mount "${loopdevs[1]}" "$TEST_MNT"
run_check $SUDO_HELPER "$TOP/btrfs" scrub start "$TEST_MNT"
sleep 3
run_check_stdout $SUDO_HELPER "$TOP/btrfs" scrub status "$TEST_MNT" | grep -q "finished" || _fail "scrub for raid1 and one dev is no update to data failed."
run_check $SUDO_HELPER umount "$TEST_MNT"

cleanup_loopdevs
