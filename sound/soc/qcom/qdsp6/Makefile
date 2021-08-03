# SPDX-License-Identifier: GPL-2.0-only
snd-ar-objs := audioreach.o q6apm.o topology.o
snd-apm-dai-objs := q6apm-dai.o

snd-afe-dai-objs += q6dsp-audio-ports.o q6afe-dai.o
snd-afe-clks-objs := q6dsp-audio-clocks.o q6afe-clocks.o

obj-$(CONFIG_SND_SOC_QDSP6_COMMON) += q6dsp-common.o
obj-$(CONFIG_SND_SOC_QDSP6_CORE) += q6core.o
obj-$(CONFIG_SND_SOC_QDSP6_AFE) += q6afe.o
obj-$(CONFIG_SND_SOC_QDSP6_AFE_DAI) += snd-afe-dai.o
obj-$(CONFIG_SND_SOC_QDSP6_AFE_CLOCKS) += snd-afe-clks.o
obj-$(CONFIG_SND_SOC_QDSP6_ADM) += q6adm.o
obj-$(CONFIG_SND_SOC_QDSP6_ROUTING) += q6routing.o
obj-$(CONFIG_SND_SOC_QDSP6_ASM) += q6asm.o
obj-$(CONFIG_SND_SOC_QDSP6_ASM_DAI) += q6asm-dai.o

obj-$(CONFIG_SND_SOC_QDSP6_AUDIOREACH) += snd-ar.o
obj-$(CONFIG_SND_SOC_QDSP6_APM_DAI) += snd-apm-dai.o
