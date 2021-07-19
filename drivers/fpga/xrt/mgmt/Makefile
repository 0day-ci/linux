# SPDX-License-Identifier: GPL-2.0
#
# Copyright (C) 2020-2021 Xilinx, Inc. All rights reserved.
#
# Authors: Sonal.Santan@xilinx.com
#

FULL_XRT_PATH=$(srctree)/$(src)/..
FULL_DTC_PATH=$(srctree)/scripts/dtc/libfdt

obj-$(CONFIG_FPGA_XRT_XMGMT)	+= xrt-mgmt.o

xrt-mgmt-objs := root.o		\
	   xmgmt-main.o		\
	   xrt-mgr.o		\
	   xmgmt-main-region.o

ccflags-y := -I$(FULL_XRT_PATH)/include		\
	-I$(FULL_DTC_PATH)
