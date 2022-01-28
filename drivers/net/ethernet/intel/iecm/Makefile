# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2019 Intel Corporation

#
# Makefile for the Intel(R) Data Plane Function Linux Driver
#

obj-$(CONFIG_IECM) += iecm.o

ccflags-y += -I$(srctree)/drivers/net/ethernet/intel/include

iecm-y := \
	iecm_lib.o \
	iecm_main.o
