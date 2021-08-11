# SPDX-License-Identifier: GPL-2.0
# Makefile for NXP VPU driver

vpu-8q-dev-objs += vpu_drv.o \
			vpu_dev_imx8q.o

vpu-8q-core-objs += vpu_core.o \
			vpu_mbox.o \
			vpu_v4l2.o \
			vpu_helpers.o \
			vpu_cmds.o \
			vpu_msgs.o \
			vpu_rpc.o \
			vpu_imx8q.o \
			vpu_windsor.o \
			vpu_malone.o \
			vpu_color.o \
			vdec.o  \
			venc.o \
			vpu_dbg.o

obj-$(CONFIG_MXC_VPU_8Q) += vpu-8q-dev.o
obj-$(CONFIG_MXC_VPU_8Q) += vpu-8q-core.o
