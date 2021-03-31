# SPDX-License-Identifier: GPL-2.0
# Makefile for KVM support for MIPS
#

ccflags-y += -Ivirt/kvm -Iarch/mips/kvm

kvm-y := $(addprefix ../../../virt/kvm/, kvm_main.o coalesced_mmio.o eventfd.o)
kvm-$(CONFIG_CPU_HAS_MSA) += msa.o

kvm-y +=    mips.o emulate.o entry.o \
	    interrupt.o stats.o commpage.o \
	    fpu.o
kvm-y += hypcall.o
kvm-y += mmu.o
kvm-$(CONFIG_CPU_LOONGSON64) += loongson_ipi.o

ifdef CONFIG_KVM_MIPS_VZ
kvm-y		+= vz.o
else
kvm-y		+= dyntrans.o
kvm-y		+= trap_emul.o
endif
obj-$(CONFIG_KVM)	+= kvm.o
obj-y			+= callback.o tlb.o
