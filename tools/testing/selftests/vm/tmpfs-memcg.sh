#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

CGROUP_PATH=/dev/cgroup/memory/tmpfs-memcg-test
REMOUNT_CGROUP_PATH=/dev/cgroup/memory/remount-memcg-test

function cleanup() {
  rm -rf /mnt/tmpfs/*
  umount /mnt/tmpfs
  rm -rf /mnt/tmpfs

  rmdir $CGROUP_PATH
  rmdir $REMOUNT_CGROUP_PATH

  echo CLEANUP DONE
}

function setup() {
  mkdir -p $CGROUP_PATH
  mkdir -p $REMOUNT_CGROUP_PATH
  echo $((10 * 1024 * 1024)) > $CGROUP_PATH/memory.limit_in_bytes
  echo 0 > $CGROUP_PATH/cpuset.cpus
  echo 0 > $CGROUP_PATH/cpuset.mems

  mkdir -p /mnt/tmpfs

  echo SETUP DONE
}

function expect_equal() {
  local expected="$1"
  local actual="$2"
  local error="$3"

  if [[ "$actual" != "$expected" ]]; then
    echo "expected ($expected) != actual ($actual): $3" >&2
    cleanup
    exit 1
  fi
}

function expect_ge() {
  local expected="$1"
  local actual="$2"
  local error="$3"

  if [[ "$actual" -lt "$expected" ]]; then
    echo "expected ($expected) < actual ($actual): $3" >&2
    cleanup
    exit 1
  fi
}

cleanup
setup

mount -t tmpfs -o memcg=$REMOUNT_CGROUP_PATH tmpfs /mnt/tmpfs
check=$(cat /proc/mounts | grep -i remount-memcg-test)
if [ -z "$check" ]; then
  echo "tmpfs memcg= was not mounted correctly:"
  echo $check
  echo "FAILED"
  cleanup
  exit 1
fi

mount -t tmpfs -o remount,memcg=$CGROUP_PATH tmpfs /mnt/tmpfs
check=$(cat /proc/mounts | grep -i tmpfs-memcg-test)
if [ -z "$check" ]; then
  echo "tmpfs memcg= was not remounted correctly:"
  echo $check
  echo "FAILED"
  cleanup
  exit 1
fi

TARGET_MEMCG_USAGE=$(cat $CGROUP_PATH/memory.usage_in_bytes)
expect_equal 0 "$TARGET_MEMCG_USAGE" "Before echo, memcg usage should be 0"

# Echo to allocate a page in the tmpfs
echo
echo
echo hello > /mnt/tmpfs/test
TARGET_MEMCG_USAGE=$(cat $CGROUP_PATH/memory.usage_in_bytes)
expect_ge 4096 "$TARGET_MEMCG_USAGE" "After echo, memcg usage should be greater than 4096"
echo "Echo test succeeded"

echo
echo
tools/testing/selftests/vm/mmap_write -p /mnt/tmpfs/test -s $((1 * 1024 * 1024))
TARGET_MEMCG_USAGE=$(cat $CGROUP_PATH/memory.usage_in_bytes)
expect_ge $((1 * 1024 * 1024)) "$TARGET_MEMCG_USAGE" "After mmap_write, memcg usage should greater than 1MB"
echo "WRITE TEST SUCCEEDED"

# SIGBUS the remote container on pagefault.
echo
echo
echo "SIGBUS the process doing the remote charge on hitting the limit of the remote cgroup."
echo "This will take a long time because the kernel goes through reclaim retries,"
echo "but should eventually the write process should receive a SIGBUS"
set +e
tools/testing/selftests/vm/mmap_write -p /mnt/tmpfs/test -s $((11 * 1024 * 1024)) &
wait $!
expect_equal "$?" "135" "mmap_write should have exited with SIGBUS"
set -e

# ENOSPC the remote container on non pagefault.
echo
echo
echo "OOMing the remote container using cat (non-pagefault)"
echo "This will take a long time because the kernel goes through reclaim retries,"
echo "but should eventually the cat command should receive an ENOSPC"
cat /dev/random > /mnt/tmpfs/random || true

cleanup
echo TEST PASSED
