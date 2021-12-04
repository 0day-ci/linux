# SPDX-License-Identifier: GPL-2.0
#
# Copyright (C) 2020-2021 Xilinx, Inc. All rights reserved.
#
# Authors: Sonal.Santan@xilinx.com
#

FULL_XRT_PATH=$(srctree)/$(src)/..

obj-$(CONFIG_FPGA_XRT_LIB) += xrt-lib.o

xrt-lib-objs :=			\
	lib-drv.o		\
	xroot.o			\
	group.o			\
	xrt-bus.dtb.o

ccflags-y := -I$(FULL_XRT_PATH)/include
