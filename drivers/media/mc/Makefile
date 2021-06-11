# SPDX-License-Identifier: GPL-2.0

mc-objs	:= mc-device.o mc-devnode.o mc-entity.o \
	   mc-request.o

ifeq ($(CONFIG_USB),$(filter $(CONFIG_USB),y m))
	mc-objs += mc-dev-allocator.o
endif

obj-$(CONFIG_MEDIA_SUPPORT) += mc.o
