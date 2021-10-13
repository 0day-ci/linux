# SPDX-License-Identifier: GPL-2.0
#
# Copyright (C) Microsoft Corporation. All rights reserved.
#
# Makefile for building the IPE module as part of the kernel tree.
#

ccflags-y := -I$(srctree)/security/ipe/modules

quiet_cmd_polgen = IPE_POL $(2)
      cmd_polgen = scripts/ipe/polgen/polgen security/ipe/boot-policy.c $(2)

$(eval $(call config_filename,IPE_BOOT_POLICY))

targets += boot-policy.c

$(obj)/boot-policy.c: scripts/ipe/polgen/polgen $(IPE_BOOT_POLICY_FILENAME) FORCE
	$(call if_changed,polgen,$(IPE_BOOT_POLICY_FILENAME))

obj-$(CONFIG_SECURITY_IPE) += \
	boot-policy.o \
	ctx.o \
	eval.o \
	fs.o \
	hooks.o \
	ipe.o \
	modules/ \
	modules.o \
	parsers/ \
	parsers.o \
	policy.o \
	policyfs.o \

obj-$(CONFIG_AUDIT) += audit.o

obj-$(CONFIG_SECURITY_IPE_KUNIT_TEST) += \
	policy_parser_tests.o \

clean-files := boot-policy.c \
