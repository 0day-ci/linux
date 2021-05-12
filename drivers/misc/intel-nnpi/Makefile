# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright (C) 2019-2021 Intel Corporation
#

obj-$(CONFIG_INTEL_NNPI) := intel_nnpi.o intel_nnpi_pcie.o

intel_nnpi-y := device.o

intel_nnpi_pcie-y := nnp_pcie.o

ccflags-y += -I$(srctree)/$(src)/ipc_include
