# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2019 Intel Corporation

#
# Makefile for the Intel(R) Data Plane Function Linux Driver
#

obj-$(CONFIG_IECM) += iecm.o

ccflags-y += -I$(srctree)/drivers/net/ethernet/intel/include \
	     -I$(srctree)/include/linux/avf

iecm-y := \
	iecm_lib.o \
	iecm_virtchnl.o \
	iecm_txrx.o \
	iecm_singleq_txrx.o \
	iecm_ethtool.o \
	iecm_controlq.o \
	iecm_controlq_setup.o \
	iecm_main.o
