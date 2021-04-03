# SPDX-License-Identifier: GPL-2.0

subdir-y += fsl

DTC_FLAGS   ?= -p 1024
dtstree		:= $(srctree)/$(src)
dtb-$(CONFIG_OF_ALL_DTBS) := $(patsubst $(dtstree)/%.dts,%.dtb, $(wildcard $(dtstree)/*.dts))
