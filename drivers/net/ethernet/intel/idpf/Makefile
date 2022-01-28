# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2019 Intel Corporation

#
# Makefile for the Intel(R) Data Plane Function Linux Driver
#

obj-$(CONFIG_IDPF) += idpf.o

ccflags-y += -I$(srctree)/drivers/net/ethernet/intel/include \
			 -I$(srctree)/include/linux/avf

idpf-y := \
	idpf_main.o \
	idpf_reg.o
