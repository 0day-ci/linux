# SPDX-License-Identifier: GPL-2.0
CFLAGS := -static -I../../../../usr/include

TEST_PROGS := run_empty.sh run_rdonly.sh run_rdwr.sh
TEST_GEN_FILES := init empty rdonly rdwr
TEST_FILES := lib.sh

include ../lib.mk

EXTRA_CLEAN := initrd cpio_list testdir
