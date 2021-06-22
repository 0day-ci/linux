# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the fpga framework and fpga manager drivers.
#

# Core FPGA Manager Framework
obj-$(CONFIG_FPGA)			+= fpga-mgr.o

# FPGA Manager Drivers
obj-$(CONFIG_FPGA_MGR_ICE40_SPI)	+= ice40-spi.o
obj-$(CONFIG_FPGA_MGR_MACHXO2_SPI)	+= machxo2-spi.o

# FPGA Bridge Drivers
obj-$(CONFIG_FPGA_BRIDGE)		+= fpga-bridge.o

# High Level Interfaces
obj-$(CONFIG_FPGA_REGION)		+= fpga-region.o
obj-$(CONFIG_OF_FPGA_REGION)		+= of-fpga-region.o

obj-$(CONFIG_FPGA_ALTERA) += altera/
obj-$(CONFIG_FPGA_DFL) += dfl/
obj-$(CONFIG_FPGA_XILINX) += xilinx/
