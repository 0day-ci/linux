# SPDX-License-Identifier: GPL-2.0
#
# FPGA Device Feature List (DFL) Support
obj-$(CONFIG_FPGA_DFL) += dfl.o
obj-$(CONFIG_FPGA_DFL_AFU) += dfl-afu.o
obj-$(CONFIG_FPGA_DFL_FME) += dfl-fme.o
obj-$(CONFIG_FPGA_DFL_FME_BRIDGE) += fme-br.o
obj-$(CONFIG_FPGA_DFL_FME_MGR) += fme-mgr.o
obj-$(CONFIG_FPGA_DFL_FME_REGION) += fme-region.o
obj-$(CONFIG_FPGA_DFL_NIOS_INTEL_PAC_N3000) += n3000-nios.o
obj-$(CONFIG_FPGA_DFL_PCI) += pci.o

dfl-fme-objs := fme-main.o fme-pr.o fme-error.o fme-perf.o
dfl-afu-objs := afu-main.o afu-region.o afu-dma-region.o afu-error.o
