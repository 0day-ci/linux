# SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
#
# Copyright(c) 2020 - 2021 Intel Corporation.
#
obj-$(CONFIG_INFINIBAND_RV) += rv.o

rv-y := rv_main.o rv_mr_cache.o trace.o

CFLAGS_trace.o = -I$(src)
