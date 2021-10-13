# SPDX-License-Identifier: GPL-2.0
#
# Copyright (C) Microsoft Corporation. All rights reserved.
#
# Makefile for building the IPE module as part of the kernel tree.
#

ccflags-y := -I$(srctree)/security/ipe/modules

obj-$(CONFIG_SECURITY_IPE) += \
	ctx.o \
	eval.o \
	fs.o \
	hooks.o \
	ipe.o \
	modules.o \
	parsers/ \
	parsers.o \
	policy.o \
	policyfs.o \
