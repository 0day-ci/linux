# SPDX-License-Identifier: GPL-2.0
#
# Makefile for drivers/platform/x86/intel
# Intel x86 Platform-Specific Drivers
#

obj-$(CONFIG_INTEL_CHT_INT33FE)		+= int33fe/
obj-$(CONFIG_INTEL_SKL_INT3472)		+= int3472/
obj-$(CONFIG_INTEL_PMT_CLASS)		+= pmt/
obj-$(CONFIG_INTEL_ATOMISP2_LED)	+= atomisp2/
obj-$(CONFIG_INTEL_ATOMISP2_PM)		+= atomisp2/
CFLAGS_hid.o			:= -I./drivers/platform/x86
intel-hid-y				:= hid.o
obj-$(CONFIG_INTEL_HID_EVENT)		+= intel-hid.o
intel_int0002_vgpio-y				:= int0002_vgpio.o
obj-$(CONFIG_INTEL_INT0002_VGPIO)	+= intel_int0002_vgpio.o
intel_oaktrail-y					:= oaktrail.o
obj-$(CONFIG_INTEL_OAKTRAIL)		+= intel_oaktrail.o
CFLAGS_vbtn.o						:= -I./drivers/platform/x86
intel-vbtn-y						:= vbtn.o
obj-$(CONFIG_INTEL_VBTN)		+= intel-vbtn.o

# Intel PMIC / PMC / P-Unit devices
intel_bxtwc_tmu-y 					:= bxtwc_tmu.o
obj-$(CONFIG_INTEL_BXTWC_PMIC_TMU)	+= intel_bxtwc_tmu.o
intel_chtdc_ti_pwrbtn-y				:= chtdc_ti_pwrbtn.o
obj-$(CONFIG_INTEL_CHTDC_TI_PWRBTN)	+= intel_chtdc_ti_pwrbtn.o
intel_mrfld_pwrbtn-y 				:= mrfld_pwrbtn.o
obj-$(CONFIG_INTEL_MRFLD_PWRBTN)	+= intel_mrfld_pwrbtn.o
intel_punit_ipc-y			:= punit_ipc.o
obj-$(CONFIG_INTEL_PUNIT_IPC)		+= intel_punit_ipc.o
obj-$(CONFIG_INTEL_PMC_CORE)		+= pmc/
obj-$(CONFIG_INTEL_SCU_IPC)			+= scu/
obj-$(CONFIG_INTEL_SCU_PCI)			+= scu/
obj-$(CONFIG_INTEL_SCU_PLATFORM)	+= scu/
obj-$(CONFIG_INTEL_SCU_WDT)			+= scu/
obj-$(CONFIG_INTEL_SCU_IPC_UTIL)	+= scu/
obj-$(CONFIG_INTEL_TELEMETRY)		+= telemetry/

# Intel uncore drivers
CFLAGS_ips.o				:= -I./drivers/platform/x86
intel_ips-y							:= ips.o
obj-$(CONFIG_INTEL_IPS)				+= intel_ips.o
intel-rst-y							:= rst.o
obj-$(CONFIG_INTEL_RST)				+= intel-rst.o
intel-smartconnect-y				:= smartconnect.o
obj-$(CONFIG_INTEL_SMARTCONNECT)		+= intel-smartconnect.o
intel_turbo_max_3-y						:= turbo_max_3.o
obj-$(CONFIG_INTEL_TURBO_MAX_3)			+= intel_turbo_max_3.o
intel-uncore-frequency-y					:= uncore-frequency.o
obj-$(CONFIG_INTEL_UNCORE_FREQ_CONTROL)		+= intel-uncore-frequency.o
obj-$(CONFIG_INTEL_SPEED_SELECT_INTERFACE)	+= speed_select_if/

#WMI drivers
obj-$(CONFIG_INTEL_WMI_SBL_FW_UPDATE)	+= wmi/
