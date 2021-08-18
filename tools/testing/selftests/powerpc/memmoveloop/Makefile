# SPDX-License-Identifier: GPL-2.0
CFLAGS += -m64
CFLAGS += -I$(CURDIR)
CFLAGS += -D SELFTEST
CFLAGS += -maltivec

ASFLAGS = $(CFLAGS) -Wa,-mpower4

TEST_GEN_PROGS := memmove_64
EXTRA_SOURCES := validate.c ../harness.c stubs.S
CPPFLAGS += -D TEST_MEMMOVE=test_memmove

top_srcdir = ../../../../..
include ../../lib.mk

$(OUTPUT)/memmove_64: mem_64.S memcpy_64.S $(EXTRA_SOURCES)
	$(CC) $(CPPFLAGS) $(CFLAGS) \
		-D TEST_MEMMOVE=test_memmove \
		-o $@ $^
