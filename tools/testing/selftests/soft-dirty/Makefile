# SPDX-License-Identifier: GPL-2.0
top_srcdir = ../../../..
INSTALL_HDR_PATH = $(top_srcdir)/usr
LINUX_HDR_PATH = $(INSTALL_HDR_PATH)/include/

CFLAGS += -Wall -I$(LINUX_HDR_PATH) -O0 -g3

TEST_GEN_PROGS := soft-dirty
include ../lib.mk
