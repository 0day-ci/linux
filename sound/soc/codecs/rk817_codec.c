// SPDX-License-Identifier: GPL-2.0
//
// rk817 ALSA SoC Audio driver
//
// Copyright (c) 2018, Fuzhou Rockchip Electronics Co., Ltd All rights reserved.

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/mfd/rk808.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <sound/core.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include "rk817_codec.h"

struct rk817_codec_priv {
	struct snd_soc_component *component;
	struct regmap *regmap;
	struct rk808 *rk817;
	struct clk *mclk;
	unsigned int stereo_sysclk;
	bool mic_in_differential;
};

static const struct reg_default rk817_reg_defaults[] = {
	{ RK817_CODEC_DTOP_VUCTL, 0x03 },
	{ RK817_CODEC_DTOP_VUCTIME, 0x00 },
	{ RK817_CODEC_DTOP_LPT_SRST, 0x00 },
	{ RK817_CODEC_DTOP_DIGEN_CLKE, 0x00 },
	/* from vendor driver, CODEC_AREF_RTCFG0 not defined in data sheet */
	{ RK817_CODEC_AREF_RTCFG0, 0x00 },
	{ RK817_CODEC_AREF_RTCFG1, 0x06 },
	{ RK817_CODEC_AADC_CFG0, 0xc8 },
	/* from vendor driver, CODEC_AADC_CFG1 not defined in data sheet */
	{ RK817_CODEC_AADC_CFG1, 0x00 },
	{ RK817_CODEC_DADC_VOLL, 0x00 },
	{ RK817_CODEC_DADC_VOLR, 0x00 },
	{ RK817_CODEC_DADC_SR_ACL0, 0x00 },
	{ RK817_CODEC_DADC_ALC1, 0x00 },
	{ RK817_CODEC_DADC_ALC2, 0x00 },
	{ RK817_CODEC_DADC_NG, 0x00 },
	{ RK817_CODEC_DADC_HPF, 0x00 },
	{ RK817_CODEC_DADC_RVOLL, 0xff },
	{ RK817_CODEC_DADC_RVOLR, 0xff },
	{ RK817_CODEC_AMIC_CFG0, 0x70 },
	{ RK817_CODEC_AMIC_CFG1, 0x00 },
	{ RK817_CODEC_DMIC_PGA_GAIN, 0x66 },
	{ RK817_CODEC_DMIC_LMT1, 0x00 },
	{ RK817_CODEC_DMIC_LMT2, 0x00 },
	{ RK817_CODEC_DMIC_NG1, 0x00 },
	{ RK817_CODEC_DMIC_NG2, 0x00 },
	/* from vendor driver, CODEC_ADAC_CFG0 not defined in data sheet */
	{ RK817_CODEC_ADAC_CFG0, 0x00 },
	{ RK817_CODEC_ADAC_CFG1, 0x07 },
	{ RK817_CODEC_DDAC_POPD_DACST, 0x82 },
	{ RK817_CODEC_DDAC_VOLL, 0x00 },
	{ RK817_CODEC_DDAC_VOLR, 0x00 },
	{ RK817_CODEC_DDAC_SR_LMT0, 0x00 },
	{ RK817_CODEC_DDAC_LMT1, 0x00 },
	{ RK817_CODEC_DDAC_LMT2, 0x00 },
	{ RK817_CODEC_DDAC_MUTE_MIXCTL, 0xa0 },
	{ RK817_CODEC_DDAC_RVOLL, 0xff },
	{ RK817_CODEC_DDAC_RVOLR, 0xff },
	{ RK817_CODEC_AHP_ANTI0, 0x00 },
	{ RK817_CODEC_AHP_ANTI1, 0x00 },
	{ RK817_CODEC_AHP_CFG0, 0xe0 },
	{ RK817_CODEC_AHP_CFG1, 0x1f },
	{ RK817_CODEC_AHP_CP, 0x09 },
	{ RK817_CODEC_ACLASSD_CFG1, 0x69 },
	{ RK817_CODEC_ACLASSD_CFG2, 0x44 },
	{ RK817_CODEC_APLL_CFG0, 0x04 },
	{ RK817_CODEC_APLL_CFG1, 0x00 },
	{ RK817_CODEC_APLL_CFG2, 0x30 },
	{ RK817_CODEC_APLL_CFG3, 0x19 },
	{ RK817_CODEC_APLL_CFG4, 0x65 },
	{ RK817_CODEC_APLL_CFG5, 0x01 },
	{ RK817_CODEC_DI2S_CKM, 0x01 },
	{ RK817_CODEC_DI2S_RSD, 0x00 },
	{ RK817_CODEC_DI2S_RXCR1, 0x00 },
	{ RK817_CODEC_DI2S_RXCR2, 0x17 },
	{ RK817_CODEC_DI2S_RXCMD_TSD, 0x00 },
	{ RK817_CODEC_DI2S_TXCR1, 0x00 },
	{ RK817_CODEC_DI2S_TXCR2, 0x17 },
	{ RK817_CODEC_DI2S_TXCR3_TXCMD, 0x00 },
};

static bool rk817_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case RK817_CODEC_DTOP_LPT_SRST:
		return true;
	default:
		return false;
	}
}

static bool rk817_codec_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case RK817_CODEC_DTOP_VUCTL:
	case RK817_CODEC_DTOP_VUCTIME:
	case RK817_CODEC_DTOP_LPT_SRST:
	case RK817_CODEC_DTOP_DIGEN_CLKE:
	case RK817_CODEC_AREF_RTCFG0:
	case RK817_CODEC_AREF_RTCFG1:
	case RK817_CODEC_AADC_CFG0:
	case RK817_CODEC_AADC_CFG1:
	case RK817_CODEC_DADC_VOLL:
	case RK817_CODEC_DADC_VOLR:
	case RK817_CODEC_DADC_SR_ACL0:
	case RK817_CODEC_DADC_ALC1:
	case RK817_CODEC_DADC_ALC2:
	case RK817_CODEC_DADC_NG:
	case RK817_CODEC_DADC_HPF:
	case RK817_CODEC_DADC_RVOLL:
	case RK817_CODEC_DADC_RVOLR:
	case RK817_CODEC_AMIC_CFG0:
	case RK817_CODEC_AMIC_CFG1:
	case RK817_CODEC_DMIC_PGA_GAIN:
	case RK817_CODEC_DMIC_LMT1:
	case RK817_CODEC_DMIC_LMT2:
	case RK817_CODEC_DMIC_NG1:
	case RK817_CODEC_DMIC_NG2:
	case RK817_CODEC_ADAC_CFG0:
	case RK817_CODEC_ADAC_CFG1:
	case RK817_CODEC_DDAC_POPD_DACST:
	case RK817_CODEC_DDAC_VOLL:
	case RK817_CODEC_DDAC_VOLR:
	case RK817_CODEC_DDAC_SR_LMT0:
	case RK817_CODEC_DDAC_LMT1:
	case RK817_CODEC_DDAC_LMT2:
	case RK817_CODEC_DDAC_MUTE_MIXCTL:
	case RK817_CODEC_DDAC_RVOLL:
	case RK817_CODEC_DDAC_RVOLR:
	case RK817_CODEC_AHP_ANTI0:
	case RK817_CODEC_AHP_ANTI1:
	case RK817_CODEC_AHP_CFG0:
	case RK817_CODEC_AHP_CFG1:
	case RK817_CODEC_AHP_CP:
	case RK817_CODEC_ACLASSD_CFG1:
	case RK817_CODEC_ACLASSD_CFG2:
	case RK817_CODEC_APLL_CFG0:
	case RK817_CODEC_APLL_CFG1:
	case RK817_CODEC_APLL_CFG2:
	case RK817_CODEC_APLL_CFG3:
	case RK817_CODEC_APLL_CFG4:
	case RK817_CODEC_APLL_CFG5:
	case RK817_CODEC_DI2S_CKM:
	case RK817_CODEC_DI2S_RSD:
	case RK817_CODEC_DI2S_RXCR1:
	case RK817_CODEC_DI2S_RXCR2:
	case RK817_CODEC_DI2S_RXCMD_TSD:
	case RK817_CODEC_DI2S_TXCR1:
	case RK817_CODEC_DI2S_TXCR2:
	case RK817_CODEC_DI2S_TXCR3_TXCMD:
		return true;
	default:
		return false;
	}
}

/*
 * This sets the codec up with the values defined in the default implementation including the APLL
 * from the Rockchip vendor kernel. I do not know if these values are universal despite differing
 * from the default values defined above and taken from the datasheet, or implementation specific.
 * I don't have another implementation to compare from the Rockchip sources. Hard-coding this for
 * now.
 */

static int rk817_init(struct snd_soc_component *component)
{
	struct rk817_codec_priv *rk817 = snd_soc_component_get_drvdata(component);

	snd_soc_component_write(component, RK817_CODEC_APLL_CFG1, 0x58);
	snd_soc_component_write(component, RK817_CODEC_APLL_CFG2, 0x2d);
	snd_soc_component_write(component, RK817_CODEC_APLL_CFG3, 0x0c);
	snd_soc_component_write(component, RK817_CODEC_APLL_CFG4, 0xa5);
	snd_soc_component_write(component, RK817_CODEC_DDAC_POPD_DACST, 0x02);
	snd_soc_component_write(component, RK817_CODEC_DDAC_SR_LMT0, 0x02);
	snd_soc_component_write(component, RK817_CODEC_DMIC_PGA_GAIN, 0x99);
	snd_soc_component_write(component, RK817_CODEC_DADC_SR_ACL0, 0x02);
	snd_soc_component_write(component, RK817_CODEC_DTOP_VUCTIME, 0xf4);
	if (rk817->mic_in_differential) {
		snd_soc_component_update_bits(component, RK817_CODEC_AMIC_CFG0, MIC_DIFF_MASK,
					     MIC_DIFF_EN);
	};
	return 0;
}

/*
 * DDAC/DADC L/R volume setting
 * 0db~-95db,0.375db/step,for example:
 * 0: 0dB
 * 0x0a: -3.75dB
 * 0x7d: -46dB
 * 0xff: -95dB
 */

static const DECLARE_TLV_DB_MINMAX(rk817_vol_tlv, -9500, 0);

static const struct snd_kcontrol_new rk817_dac_controls[] = {
	SOC_DOUBLE_R_RANGE_TLV("Master Playback Volume", RK817_CODEC_DDAC_VOLL,
		RK817_CODEC_DDAC_VOLR, 0, 0x00, 0xff, 1, rk817_vol_tlv),
	SOC_DOUBLE_R_RANGE_TLV("Master Capture Volume", RK817_CODEC_DADC_VOLL,
			      RK817_CODEC_DADC_VOLR, 0, 0x00, 0xff, 1, rk817_vol_tlv)
};

static const char *dac_mux_text[] = {
	"HP",
	"SPK",
};

static SOC_ENUM_SINGLE_VIRT_DECL(dac_enum, dac_mux_text);

static const struct snd_kcontrol_new dac_mux =
	SOC_DAPM_ENUM("Playback Mux", dac_enum);

static const struct snd_soc_dapm_widget rk817_dapm_widgets[] = {

	/* capture/playback common */
	SND_SOC_DAPM_SUPPLY("LDO Regulator", RK817_CODEC_AREF_RTCFG1, 6, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("IBIAS Block", RK817_CODEC_AREF_RTCFG1, 2, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("VAvg Buffer", RK817_CODEC_AREF_RTCFG1, 1, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("PLL Power", RK817_CODEC_APLL_CFG5, 0, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("I2S TX1 Transfer Start", RK817_CODEC_DI2S_RXCMD_TSD, 5, 0, NULL, 0),

	/* capture path common */
	SND_SOC_DAPM_SUPPLY("ADC Clock", RK817_CODEC_DTOP_DIGEN_CLKE, 7, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("I2S TX Clock", RK817_CODEC_DTOP_DIGEN_CLKE, 6, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC Channel Enable", RK817_CODEC_DTOP_DIGEN_CLKE, 5, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("I2S TX Channel Enable", RK817_CODEC_DTOP_DIGEN_CLKE, 4, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("MIC Power On", RK817_CODEC_AMIC_CFG0, 6, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("I2S TX3 Transfer Start", RK817_CODEC_DI2S_TXCR3_TXCMD, 7, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("I2S TX3 Right Justified", RK817_CODEC_DI2S_TXCR3_TXCMD, 3, 0, NULL, 0),

	/* capture path L */
	SND_SOC_DAPM_ADC("ADC L", "Capture", RK817_CODEC_AADC_CFG0, 7, 1),
	SND_SOC_DAPM_SUPPLY("PGA L Power On", RK817_CODEC_AMIC_CFG0, 5, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Mic Boost L1", RK817_CODEC_AMIC_CFG0, 3, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Mic Boost L2", RK817_CODEC_AMIC_CFG0, 2, 0, NULL, 0),

	/* capture path R */
	SND_SOC_DAPM_ADC("ADC R", "Capture", RK817_CODEC_AADC_CFG0, 6, 1),
	SND_SOC_DAPM_SUPPLY("PGA R Power On", RK817_CODEC_AMIC_CFG0, 4, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Mic Boost R1", RK817_CODEC_AMIC_CFG0, 3, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Mic Boost R2", RK817_CODEC_AMIC_CFG0, 3, 0, NULL, 0),

	/* playback path common */
	SND_SOC_DAPM_SUPPLY("DAC Clock", RK817_CODEC_DTOP_DIGEN_CLKE, 3, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("I2S RX Clock", RK817_CODEC_DTOP_DIGEN_CLKE, 2, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DAC Channel Enable", RK817_CODEC_DTOP_DIGEN_CLKE, 1, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("I2S RX Channel Enable", RK817_CODEC_DTOP_DIGEN_CLKE, 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DAC Bias", RK817_CODEC_ADAC_CFG1, 3, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DAC Mute Off", RK817_CODEC_DDAC_MUTE_MIXCTL, 0, 1, NULL, 0),

	/* playback path speaker */
	SND_SOC_DAPM_SUPPLY("Class D Mode", RK817_CODEC_DDAC_MUTE_MIXCTL, 4, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("High Pass Filter", RK817_CODEC_DDAC_MUTE_MIXCTL, 7, 0, NULL, 0),
	SND_SOC_DAPM_DAC("SPK DAC", "Playback", RK817_CODEC_ADAC_CFG1, 2, 1),
	SND_SOC_DAPM_SUPPLY("Enable Class D", RK817_CODEC_ACLASSD_CFG1, 7, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Disable Class D Mute Ramp", RK817_CODEC_ACLASSD_CFG1, 6, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Class D Mute Rate 1", RK817_CODEC_ACLASSD_CFG1, 3, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Class D Mute Rate 2", RK817_CODEC_ACLASSD_CFG1, 2, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Class D OCPP 2", RK817_CODEC_ACLASSD_CFG2, 5, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Class D OCPP 3", RK817_CODEC_ACLASSD_CFG2, 4, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Class D OCPN 2", RK817_CODEC_ACLASSD_CFG2, 1, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Class D OCPN 3", RK817_CODEC_ACLASSD_CFG2, 0, 0, NULL, 0),

	/* playback path headphones */
	SND_SOC_DAPM_SUPPLY("Headphone Charge Pump", RK817_CODEC_AHP_CP, 4, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Headphone CP Discharge LDO", RK817_CODEC_AHP_CP, 3, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Headphone OStage", RK817_CODEC_AHP_CFG0, 6, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Headphone Pre Amp", RK817_CODEC_AHP_CFG0, 5, 1, NULL, 0),
	SND_SOC_DAPM_DAC("DAC L", "Playback", RK817_CODEC_ADAC_CFG1, 1, 1),
	SND_SOC_DAPM_DAC("DAC R", "Playback", RK817_CODEC_ADAC_CFG1, 0, 1),

	/* Mux for input/output path selection */
	SND_SOC_DAPM_MUX("Playback Mux", SND_SOC_NOPM, 1, 0, &dac_mux),

	/* Pins for Simple Card Bindings */
	SND_SOC_DAPM_INPUT("MICL"),
	SND_SOC_DAPM_INPUT("MICR"),
	SND_SOC_DAPM_OUTPUT("HPOL"),
	SND_SOC_DAPM_OUTPUT("HPOR"),
	SND_SOC_DAPM_OUTPUT("SPKO"),
};

static const struct snd_soc_dapm_route rk817_dapm_routes[] = {

	/* capture path */
	/* left mic */
	{"ADC L", NULL, "LDO Regulator"},
	{"ADC L", NULL, "IBIAS Block"},
	{"ADC L", NULL, "VAvg Buffer"},
	{"ADC L", NULL, "PLL Power"},
	{"ADC L", NULL, "ADC Clock"},
	{"ADC L", NULL, "I2S TX Clock"},
	{"ADC L", NULL, "ADC Channel Enable"},
	{"ADC L", NULL, "I2S TX Channel Enable"},
	{"ADC L", NULL, "I2S TX1 Transfer Start"},
	{"MICL", NULL, "MIC Power On"},
	{"MICL", NULL, "PGA L Power On"},
	{"MICL", NULL, "Mic Boost L1"},
	{"MICL", NULL, "Mic Boost L2"},
	{"MICL", NULL, "I2S TX3 Transfer Start"},
	{"MICL", NULL, "I2S TX3 Right Justified"},
	{"ADC L", NULL, "MICL"},

	/* right mic */
	{"ADC R", NULL, "LDO Regulator"},
	{"ADC R", NULL, "IBIAS Block"},
	{"ADC R", NULL, "VAvg Buffer"},
	{"ADC R", NULL, "PLL Power"},
	{"ADC R", NULL, "ADC Clock"},
	{"ADC R", NULL, "I2S TX Clock"},
	{"ADC R", NULL, "ADC Channel Enable"},
	{"ADC R", NULL, "I2S TX Channel Enable"},
	{"ADC R", NULL, "I2S TX1 Transfer Start"},
	{"MICR", NULL, "MIC Power On"},
	{"MICR", NULL, "PGA R Power On"},
	{"MICR", NULL, "Mic Boost R1"},
	{"MICR", NULL, "Mic Boost R2"},
	{"MICR", NULL, "I2S TX3 Transfer Start"},
	{"MICR", NULL, "I2S TX3 Right Justified"},
	{"ADC R", NULL, "MICR"},

	/* playback path */
	/* speaker path */
	{"SPK DAC", NULL, "LDO Regulator"},
	{"SPK DAC", NULL, "IBIAS Block"},
	{"SPK DAC", NULL, "VAvg Buffer"},
	{"SPK DAC", NULL, "PLL Power"},
	{"SPK DAC", NULL, "I2S TX1 Transfer Start"},
	{"SPK DAC", NULL, "DAC Clock"},
	{"SPK DAC", NULL, "I2S RX Clock"},
	{"SPK DAC", NULL, "DAC Channel Enable"},
	{"SPK DAC", NULL, "I2S RX Channel Enable"},
	{"SPK DAC", NULL, "Class D Mode"},
	{"SPK DAC", NULL, "DAC Bias"},
	{"SPK DAC", NULL, "DAC Mute Off"},
	{"SPK DAC", NULL, "Enable Class D"},
	{"SPK DAC", NULL, "Disable Class D Mute Ramp"},
	{"SPK DAC", NULL, "Class D Mute Rate 1"},
	{"SPK DAC", NULL, "Class D Mute Rate 2"},
	{"SPK DAC", NULL, "Class D OCPP 2"},
	{"SPK DAC", NULL, "Class D OCPP 3"},
	{"SPK DAC", NULL, "Class D OCPN 2"},
	{"SPK DAC", NULL, "Class D OCPN 3"},
	{"SPK DAC", NULL, "High Pass Filter"},

	/* headphone path L */
	{"DAC L", NULL, "LDO Regulator"},
	{"DAC L", NULL, "IBIAS Block"},
	{"DAC L", NULL, "VAvg Buffer"},
	{"DAC L", NULL, "PLL Power"},
	{"DAC L", NULL, "I2S TX1 Transfer Start"},
	{"DAC L", NULL, "DAC Clock"},
	{"DAC L", NULL, "I2S RX Clock"},
	{"DAC L", NULL, "DAC Channel Enable"},
	{"DAC L", NULL, "I2S RX Channel Enable"},
	{"DAC L", NULL, "DAC Bias"},
	{"DAC L", NULL, "DAC Mute Off"},
	{"DAC L", NULL, "Headphone Charge Pump"},
	{"DAC L", NULL, "Headphone CP Discharge LDO"},
	{"DAC L", NULL, "Headphone OStage"},
	{"DAC L", NULL, "Headphone Pre Amp"},

	/* headphone path R */
	{"DAC R", NULL, "LDO Regulator"},
	{"DAC R", NULL, "IBIAS Block"},
	{"DAC R", NULL, "VAvg Buffer"},
	{"DAC R", NULL, "PLL Power"},
	{"DAC R", NULL, "I2S TX1 Transfer Start"},
	{"DAC R", NULL, "DAC Clock"},
	{"DAC R", NULL, "I2S RX Clock"},
	{"DAC R", NULL, "DAC Channel Enable"},
	{"DAC R", NULL, "I2S RX Channel Enable"},
	{"DAC R", NULL, "DAC Bias"},
	{"DAC R", NULL, "DAC Mute Off"},
	{"DAC R", NULL, "Headphone Charge Pump"},
	{"DAC R", NULL, "Headphone CP Discharge LDO"},
	{"DAC R", NULL, "Headphone OStage"},
	{"DAC R", NULL, "Headphone Pre Amp"},

	/* mux path for output selection */
	{"Playback Mux", "HP", "DAC L"},
	{"Playback Mux", "HP", "DAC R"},
	{"Playback Mux", "SPK", "SPK DAC"},
	{"SPKO", NULL, "Playback Mux"},
	{"HPOL", NULL, "Playback Mux"},
	{"HPOR", NULL, "Playback Mux"},
};

static int rk817_set_dai_sysclk(struct snd_soc_dai *codec_dai,
				int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = codec_dai->component;
	struct rk817_codec_priv *rk817 = snd_soc_component_get_drvdata(component);

	rk817->stereo_sysclk = freq;

	return 0;
}

static int rk817_set_dai_fmt(struct snd_soc_dai *codec_dai,
			     unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;
	unsigned int i2s_mst = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		i2s_mst |= RK817_I2S_MODE_SLV;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		i2s_mst |= RK817_I2S_MODE_MST;
		break;
	default:
		dev_err(component->dev, "%s : set master mask failed!\n", __func__);
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, RK817_CODEC_DI2S_CKM,
				      RK817_I2S_MODE_MASK, i2s_mst);

	return 0;
}

static int rk817_hw_params(struct snd_pcm_substream *substream,
			   struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		snd_soc_component_write(component, RK817_CODEC_DI2S_RXCR2,
					VDW_RX_16BITS);
		snd_soc_component_write(component, RK817_CODEC_DI2S_TXCR2,
					VDW_TX_16BITS);
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
	case SNDRV_PCM_FORMAT_S32_LE:
		snd_soc_component_write(component, RK817_CODEC_DI2S_RXCR2,
					VDW_RX_24BITS);
		snd_soc_component_write(component, RK817_CODEC_DI2S_TXCR2,
					VDW_TX_24BITS);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int rk817_digital_mute(struct snd_soc_dai *dai, int mute, int stream)
{
	struct snd_soc_component *component = dai->component;

	if (mute)
		snd_soc_component_update_bits(component,
					      RK817_CODEC_DDAC_MUTE_MIXCTL,
					      DACMT_MASK, DACMT_ENABLE);
	else
		snd_soc_component_update_bits(component,
					      RK817_CODEC_DDAC_MUTE_MIXCTL,
					      DACMT_MASK, DACMT_DISABLE);

	return 0;
}

#define RK817_PLAYBACK_RATES (SNDRV_PCM_RATE_8000 |\
			      SNDRV_PCM_RATE_16000 |	\
			      SNDRV_PCM_RATE_32000 |	\
			      SNDRV_PCM_RATE_44100 |	\
			      SNDRV_PCM_RATE_48000 |	\
			      SNDRV_PCM_RATE_96000)

#define RK817_CAPTURE_RATES (SNDRV_PCM_RATE_8000 |\
			      SNDRV_PCM_RATE_16000 |	\
			      SNDRV_PCM_RATE_32000 |	\
			      SNDRV_PCM_RATE_44100 |	\
			      SNDRV_PCM_RATE_48000 |	\
			      SNDRV_PCM_RATE_96000)

#define RK817_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
			SNDRV_PCM_FMTBIT_S20_3LE |\
			SNDRV_PCM_FMTBIT_S24_LE |\
			SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_ops rk817_dai_ops = {
	.hw_params	= rk817_hw_params,
	.set_fmt	= rk817_set_dai_fmt,
	.set_sysclk	= rk817_set_dai_sysclk,
	.mute_stream	= rk817_digital_mute,
	.no_capture_mute	= 1,
};

static struct snd_soc_dai_driver rk817_dai[] = {
	{
		.name = "rk817-hifi",
		.playback = {
			.stream_name = "Playback",
			.channels_min = 2,
			.channels_max = 8,
			.rates = RK817_PLAYBACK_RATES,
			.formats = RK817_FORMATS,
		},
		.capture = {
			.stream_name = "Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RK817_CAPTURE_RATES,
			.formats = RK817_FORMATS,
		},
		.ops = &rk817_dai_ops,
	},
};

static int rk817_probe(struct snd_soc_component *component)
{
	struct rk817_codec_priv *rk817 = snd_soc_component_get_drvdata(component);

	snd_soc_component_init_regmap(component, rk817->regmap);
	rk817->component = component;

	clk_prepare_enable(rk817->mclk);

	snd_soc_component_write(component, RK817_CODEC_DTOP_LPT_SRST, 0x40);
	rk817_init(component);

	return 0;
}

static void rk817_remove(struct snd_soc_component *component)
{
	struct rk817_codec_priv *rk817 = snd_soc_component_get_drvdata(component);

	snd_soc_component_exit_regmap(component);
	clk_disable_unprepare(rk817->mclk);
	mdelay(10);

}

static const struct snd_soc_component_driver soc_codec_dev_rk817 = {
	.probe = rk817_probe,
	.remove = rk817_remove,
	.idle_bias_on = 1,
	.use_pmdown_time = 1,
	.endianness = 1,
	.non_legacy_dai_naming = 1,
	.controls = rk817_dac_controls,
	.num_controls = ARRAY_SIZE(rk817_dac_controls),
	.dapm_routes = rk817_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(rk817_dapm_routes),
	.dapm_widgets = rk817_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rk817_dapm_widgets),
};

static int rk817_codec_parse_dt_property(struct device *dev,
					 struct rk817_codec_priv *rk817)
{
	struct device_node *node = dev->parent->of_node;

	if (!node) {
		dev_err(dev, "%s() dev->parent->of_node is NULL\n",
			__func__);
		return -ENODEV;
	}

	node = of_get_child_by_name(dev->parent->of_node, "codec");
	if (!node) {
		dev_err(dev, "%s() Can not get child: codec\n",
			__func__);
		return -ENODEV;
	}

	rk817->mic_in_differential =
			of_property_read_bool(node, "mic-in-differential");

	return 0;
}

static const struct regmap_config rk817_codec_regmap_config = {
	.name = "rk817-codec",
	.reg_bits = 8,
	.val_bits = 8,
	.reg_stride = 1,
	.max_register = 0x4f,
	.cache_type = REGCACHE_NONE,
	.volatile_reg = rk817_volatile_register,
	.writeable_reg = rk817_codec_register,
	.readable_reg = rk817_codec_register,
	.reg_defaults = rk817_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(rk817_reg_defaults),
};

static int rk817_platform_probe(struct platform_device *pdev)
{
	struct rk808 *rk817 = dev_get_drvdata(pdev->dev.parent);
	struct rk817_codec_priv *rk817_codec_data;
	int ret;

	if (!rk817) {
		dev_err(&pdev->dev, "%s : rk817 is NULL\n", __func__);
		return -EINVAL;
	}

	rk817_codec_data = devm_kzalloc(&pdev->dev,
					sizeof(struct rk817_codec_priv),
					GFP_KERNEL);
	if (!rk817_codec_data)
		return -ENOMEM;

	platform_set_drvdata(pdev, rk817_codec_data);

	ret = rk817_codec_parse_dt_property(&pdev->dev, rk817_codec_data);
	if (ret < 0) {
		dev_err(&pdev->dev, "%s() parse device tree property error %d\n",
			__func__, ret);
		goto err_;
	}

	rk817_codec_data->regmap = devm_regmap_init_i2c(rk817->i2c,
					    &rk817_codec_regmap_config);
	if (IS_ERR(rk817_codec_data->regmap)) {
		ret = PTR_ERR(rk817_codec_data->regmap);
		dev_err(&pdev->dev, "failed to allocate register map: %d\n",
			ret);
		goto err_;
	}

	rk817_codec_data->mclk = devm_clk_get(&pdev->dev, "mclk");
	if (IS_ERR(rk817_codec_data->mclk)) {
		dev_err(&pdev->dev, "Unable to get mclk\n");
		ret = -ENXIO;
		goto err_;
	}

	clk_prepare_enable(rk817_codec_data->mclk);

	ret = devm_snd_soc_register_component(&pdev->dev, &soc_codec_dev_rk817,
					      rk817_dai, ARRAY_SIZE(rk817_dai));
	if (ret < 0) {
		dev_err(&pdev->dev, "%s() register codec error %d\n",
			__func__, ret);
		goto err_;
	}

	return 0;
err_:

	return ret;
}

MODULE_DEVICE_TABLE(of, rk817_codec_dt_ids);

static struct platform_driver rk817_codec_driver = {
	.driver = {
		   .name = "rk817-codec",
		   },
	.probe = rk817_platform_probe,
};

module_platform_driver(rk817_codec_driver);

MODULE_DESCRIPTION("ASoC RK817 codec driver");
