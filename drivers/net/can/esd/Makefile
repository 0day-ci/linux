# SPDX-License-Identifier: GPL-2.0-only
#
#  Makefile for esd gmbh ESDACC controller driver
#
esd_402_pci-y := esdacc.o esd402_pci.o

ifeq ($(CONFIG_CAN_ESD_402_PCI),)
obj-m += esd_402_pci.o
else
obj-$(CONFIG_CAN_ESD_402_PCI) += esd_402_pci.o
endif
