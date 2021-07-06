# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for loongson drm drivers.
# This driver provides support for the
# Direct Rendering Infrastructure (DRI)

ccflags-y := -Iinclude/drm
loongson-y := loongson_drv.o \
	loongson_crtc.o \
	loongson_plane.o \
	loongson_device.o \
	loongson_connector.o \
	loongson_encoder.o \
	loongson_i2c.o
obj-$(CONFIG_DRM_LOONGSON) += loongson.o
