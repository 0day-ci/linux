# SPDX-License-Identifier: GPL-2.0-only

TOOLSDIR := $(abspath ../../..)
LIBUMCGDIR := $(TOOLSDIR)/lib/umcg

CFLAGS += -g -O0 -I$(LIBUMCGDIR) -I$(TOOLSDIR)/include/ -I../../../../usr/include/
LDLIBS += -lpthread -static

TEST_GEN_PROGS := umcg_core_test

include ../lib.mk

$(OUTPUT)/umcg_core_test: umcg_core_test.c $(LIBUMCGDIR)/libumcg.c
