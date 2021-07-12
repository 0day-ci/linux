# SPDX-License-Identifier: GPL-2.0
r8188eu-y :=				\
		rtw_ap.o		\
		rtw_cmd.o		\
		rtw_efuse.o		\
		rtw_ieee80211.o		\
		rtw_ioctl_set.o		\
		rtw_iol.o		\
		rtw_led.o		\
		rtw_mlme.o		\
		rtw_mlme_ext.o		\
		rtw_pwrctrl.o		\
		rtw_recv.o		\
		rtw_rf.o		\
		rtw_security.o		\
		rtw_sreset.o		\
		rtw_sta_mgt.o		\
		rtw_wlan_util.o		\
		rtw_xmit.o		\
		hal/fw.o	\
		hal/mac_cfg.o \
		hal/bb_cfg.o \
		hal/rf_cfg.o \
		hal/pwrseqcmd.o \
		hal/pwrseq.o \
		hal/hal8188e_rate_adaptive.o \
		hal/hal_intf.o		\
		hal/hal_com.o		\
		hal/odm.o		\
		hal/odm_hwconfig.o	\
		hal/odm_rtl8188e.o	\
		hal/rtl8188e_cmd.o	\
		hal/rtl8188e_dm.o	\
		hal/rtl8188e_hal_init.o	\
		hal/phy.o \
		hal/rf.o \
		hal/rtl8188e_rxdesc.o	\
		hal/rtl8188e_xmit.o	\
		hal/rtl8188eu_led.o	\
		hal/rtl8188eu_recv.o	\
		hal/rtl8188eu_xmit.o	\
		hal/usb_halinit.o	\
		os_dep/ioctl_linux.o	\
		os_dep/mlme_linux.o	\
		os_dep/mon.o		\
		os_dep/os_intfs.o	\
		os_dep/osdep_service.o	\
		os_dep/recv_linux.o	\
		os_dep/rtw_android.o	\
		os_dep/usb_intf.o	\
		os_dep/usb_ops_linux.o	\
		os_dep/xmit_linux.o

obj-$(CONFIG_R8188EU)	:= r8188eu.o

ccflags-y += -I$(srctree)/$(src)/include
