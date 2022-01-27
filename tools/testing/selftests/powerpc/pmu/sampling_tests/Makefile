# SPDX-License-Identifier: GPL-2.0
include ../../../../../../scripts/Kbuild.include

all: $(TEST_GEN_PROGS)

noarg:
	$(MAKE) -C ../../

CFLAGS += -m64 -I../../../../../lib/

# Toolchains may build PIE by default which breaks the assembly
no-pie-option := $(call try-run, echo 'int main() { return 0; }' | \
	$(CC) -Werror $(KBUILD_CPPFLAGS) $(CC_OPTION_CFLAGS) -no-pie -x c - -o "$$TMP", -no-pie)

LDFLAGS += $(no-pie-option)

top_srcdir = ../../../../../..
include ../../../lib.mk

$(TEST_GEN_PROGS): ../../harness.c ../../utils.c ../event.c ../lib.c misc.c misc.h
