# SPDX-License-Identifier: GPL-2.0
# Makefile for kernel virtual machines on s390
#
# Copyright IBM Corp. 2008

include $(srctree)/virt/kvm/Makefile.kvm

ccflags-y := -Ivirt/kvm -Iarch/s390/kvm

kvm-objs := kvm-s390.o intercept.o interrupt.o priv.o sigp.o
kvm-objs += diag.o gaccess.o guestdbg.o vsie.o pv.o

obj-$(CONFIG_KVM) += kvm.o
