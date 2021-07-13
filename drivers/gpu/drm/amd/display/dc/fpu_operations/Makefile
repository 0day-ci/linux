# SPDX-License-Identifier: MIT
#
# Copyright 2021 Advanced Micro Devices, Inc.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
# OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
# ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
# OTHER DEALINGS IN THE SOFTWARE.
#
#
# Makefile for fpu operations component.
#

FPU_OPERATIONS = dcn2x.o

ifdef CONFIG_X86
fpu_ccflags := -mhard-float -msse
endif

ifdef CONFIG_PPC64
fpu_ccflags := -mhard-float -maltivec
endif

ifdef CONFIG_CC_IS_GCC
ifeq ($(call cc-ifversion, -lt, 0701, y), y)
IS_OLD_GCC = 1
endif
endif

ifdef CONFIG_X86
ifdef IS_OLD_GCC
# Stack alignment mismatch, proceed with caution.
# GCC < 7.1 cannot compile code using `double` and -mpreferred-stack-boundary=3
# (8B stack alignment).
fpu_ccflags := -mpreferred-stack-boundary=4
else
fpu_ccflags :=  -msse2
endif
endif

CFLAGS_$(AMDDALPATH)/dc/fpu_operations/dcn2x.o += $(fpu_ccflags)

AMD_DAL_FPU_OPERATIONS = $(addprefix $(AMDDALPATH)/dc/fpu_operations/,$(FPU_OPERATIONS))

AMD_DISPLAY_FILES += $(AMD_DAL_FPU_OPERATIONS)
