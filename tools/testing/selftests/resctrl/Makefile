#SPDX-License-Identifier: GPL-2.0

CFLAGS += -g -Wall -O2 -D_FORTIFY_SOURCE=2

TEST_GEN_PROGS := resctrl_tests
EXTRA_SOURCES := $(wildcard *.c)

all: $(TEST_GEN_PROGS)

$(TEST_GEN_PROGS): $(EXTRA_SOURCES)
include ../lib.mk
