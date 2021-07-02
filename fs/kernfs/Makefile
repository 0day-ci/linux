# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the kernfs pseudo filesystem
#

obj-y		:= mount.o inode.o dir.o file.o symlink.o
obj-$(CONFIG_FAIL_KERNFS_KNOBS)    += failure-injection.o
