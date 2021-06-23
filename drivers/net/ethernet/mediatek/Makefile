# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the Mediatek network device drivers.
#

obj-$(CONFIG_NET_MEDIATEK_SOC) += mtk_eth.o
mtk_eth-y := mtk_eth_soc.o mtk_sgmii.o mtk_eth_path.o mtk_ppe.o mtk_ppe_debugfs.o mtk_ppe_offload.o
obj-$(CONFIG_NET_MEDIATEK_STAR_EMAC) += mtk_star_emac.o

obj-$(CONFIG_MTK_NET_CCMNI) += ccmni/
