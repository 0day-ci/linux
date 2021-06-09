# SPDX-License-Identifier: GPL-2.0
#
# FPGA Device Feature List (DFL) Support
obj-$(CONFIG_FPGA_DFL) += dfl.o
obj-$(CONFIG_FPGA_DFL_AFU) += dfl-afu.o
obj-$(CONFIG_FPGA_DFL_FME) += dfl-fme.o
obj-$(CONFIG_FPGA_DFL_FME_BRIDGE) += dfl-fme-br.o
obj-$(CONFIG_FPGA_DFL_FME_MGR) += dfl-fme-mgr.o
obj-$(CONFIG_FPGA_DFL_FME_REGION) += dfl-fme-region.o
obj-$(CONFIG_FPGA_DFL_NIOS_INTEL_PAC_N3000) += dfl-n3000-nios.o
obj-$(CONFIG_FPGA_DFL_PCI) += dfl-pci.o

dfl-fme-objs := dfl-fme-main.o dfl-fme-pr.o dfl-fme-error.o		\
                dfl-fme-perf.o
dfl-afu-objs := dfl-afu-main.o dfl-afu-region.o dfl-afu-dma-region.o	\
                dfl-afu-error.o



