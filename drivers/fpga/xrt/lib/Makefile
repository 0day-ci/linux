# SPDX-License-Identifier: GPL-2.0
#
# Copyright (C) 2020-2021 Xilinx, Inc. All rights reserved.
#
# Authors: Sonal.Santan@xilinx.com
#

FULL_XRT_PATH=$(srctree)/$(src)/..
FULL_DTC_PATH=$(srctree)/scripts/dtc/libfdt

obj-$(CONFIG_FPGA_XRT_LIB) += xrt-lib.o

xrt-lib-objs :=			\
	lib-drv.o		\
	xroot.o			\
	xclbin.o		\
	subdev.o		\
	cdev.o			\
	group.o			\
	xleaf/vsec.o		\
	xleaf/axigate.o		\
	xleaf/devctl.o		\
	xleaf/icap.o		\
	xleaf/clock.o		\
	xleaf/clkfreq.o		\
	xleaf/ucs.o		\
	xleaf/ddr_calibration.o

ccflags-y := -I$(FULL_XRT_PATH)/include	 \
	-I$(FULL_DTC_PATH)
