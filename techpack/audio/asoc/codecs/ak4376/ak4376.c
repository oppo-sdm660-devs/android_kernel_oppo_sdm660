/*
 * ak4376.c -- AK4376 headphone amplifier codec driver
 *
 * Copyright (C) 2015 Asahi Kasei Microdevices Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include <soc/oppo/oppo_project.h>

#include "ak4376.h"

#define AK4376_SUPPLY_UV 1800000
#define AK4376_SUPPLY_LOAD_UA 200000

struct ak4376_priv {
	struct mutex lock;
	struct i2c_client *i2c;
	struct regmap *regmap;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *audio_vdd_gpio;
	struct regulator *tvdd;
	struct regulator *avdd;
	bool tvdd_enabled;
	bool avdd_enabled;
	int pdn1;
	int pdn2;
	int fs1;
	int rclk;
	int nBickFreq;
	int nPllMode;
	int nPllMCKI;
	int nDeviceID;
	int lpmode;
	int xtalfreq;
	int nDACOn;
	u8 pcb_version;
};

static const struct reg_default ak4376_reg[] = {
	{ 0x0, 0x01 }, /*    0x00    AK4376_00_POWER_MANAGEMENT1        */
	{ 0x1, 0x33 }, /*    0x01    AK4376_01_POWER_MANAGEMENT2        */
	{ 0x2, 0x01 }, /*    0x02    AK4376_02_POWER_MANAGEMENT3        */
	{ 0x3, 0x53 }, /*    0x03    AK4376_03_POWER_MANAGEMENT4        */
	{ 0x4, 0x14 }, /*    0x04    AK4376_04_OUTPUT_MODE_SETTING    */
	{ 0x5, 0x0a }, /*    0x05    AK4376_05_CLOCK_MODE_SELECT        */
	{ 0x6, 0x00 }, /*    0x06    AK4376_06_DIGITAL_FILTER_SELECT    */
	{ 0x7, 0x21 }, /*    0x07    AK4376_07_DAC_MONO_MIXING        */
	{ 0x8, 0x00 }, /*    0x08    AK4376_08_RESERVED                */
	{ 0x9, 0x00 }, /*    0x09    AK4376_09_RESERVED                */
	{ 0xA, 0x00 }, /*    0x0A    AK4376_0A_RESERVED                */
	{ 0xB, 0x12 }, /*    0x0B    AK4376_0B_LCH_OUTPUT_VOLUME        */
	{ 0xC, 0x12 }, /*    0x0C    AK4376_0C_RCH_OUTPUT_VOLUME        */
	{ 0xD, 0x0b }, /*    0x0D    AK4376_0D_HP_VOLUME_CONTROL        */
	{ 0xE, 0x00 }, /*    0x0E    AK4376_0E_PLL_CLK_SOURCE_SELECT    */
	{ 0xF, 0x00 }, /*    0x0F    AK4376_0F_PLL_REF_CLK_DIVIDER1    */
	{ 0x10, 0x04 }, /*    0x10    AK4376_10_PLL_REF_CLK_DIVIDER2    */
	{ 0x11, 0x00 }, /*    0x11    AK4376_11_PLL_FB_CLK_DIVIDER1    */
	{ 0x12, 0x3f }, /*    0x12    AK4376_12_PLL_FB_CLK_DIVIDER2    */
	{ 0x13, 0x01 }, /*    0x13    AK4376_13_DAC_CLK_SOURCE        */
	{ 0x14, 0x09 }, /*    0x14    AK4376_14_DAC_CLK_DIVIDER        */
	{ 0x15, 0x50 }, /*    0x15    AK4376_15_AUDIO_IF_FORMAT        */
	{ 0x16, 0x00 }, /*    0x16    AK4376_16_DUMMY                    */
	{ 0x17, 0x00 }, /*    0x17    AK4376_17_DUMMY                    */
	{ 0x18, 0x00 }, /*    0x18    AK4376_18_DUMMY                    */
	{ 0x19, 0x00 }, /*    0x19    AK4376_19_DUMMY                    */
	{ 0x1A, 0x00 }, /*    0x1A    AK4376_1A_DUMMY                    */
	{ 0x1B, 0x00 }, /*    0x1B    AK4376_1B_DUMMY                    */
	{ 0x1C, 0x00 }, /*    0x1C    AK4376_1C_DUMMY                    */
	{ 0x1D, 0x00 }, /*    0x1D    AK4376_1D_DUMMY                    */
	{ 0x1E, 0x00 }, /*    0x1E    AK4376_1E_DUMMY                    */
	{ 0x1F, 0x00 }, /*    0x1F    AK4376_1F_DUMMY                    */
	{ 0x20, 0x00 }, /*    0x20    AK4376_20_DUMMY                    */
	{ 0x21, 0x00 }, /*    0x21    AK4376_21_DUMMY                    */
	{ 0x22, 0x00 }, /*    0x22    AK4376_22_DUMMY                    */
	{ 0x23, 0x00 }, /*    0x23    AK4376_23_DUMMY                    */
	{ 0x24, 0x00 }, /*    0x24    AK4376_24_MODE_CONTROL            */
	{ 0x25, 0x6c }, /*    0x25                                      */
	{ 0x26, 0x20 }, /*    0x26    AK4376_26_DAC_ADJUSTMENT_1        */
	{ 0x27, 0x00 }, /*    0x27                                      */
	{ 0x28, 0x00 }, /*    0x28                                      */
	{ 0x29, 0x00 }, /*    0x29                                      */
	{ 0x2A, 0x07 }, /*    0x2A    AK4376_2A_DAC_ADJUSTMENT_2        */
};

static int ak4376_set_power(struct ak4376_priv *ak4376, bool on)
{
	int ret = 0;

	mutex_lock(&ak4376->lock);

	if (on == !!ak4376->pdn1)
		goto out;

	if (on) {
		gpiod_set_value_cansleep(ak4376->reset_gpio, 1);
		usleep_range(800, 1000);
		regcache_cache_only(ak4376->regmap, false);
		ret = regcache_sync(ak4376->regmap);
		if (ret) {
			regcache_cache_only(ak4376->regmap, true);
			regcache_mark_dirty(ak4376->regmap);
			gpiod_set_value_cansleep(ak4376->reset_gpio, 0);
			goto out;
		}
		ak4376->pdn1 = 1;
		ak4376->pdn2 = 1;
	} else {
		regcache_cache_only(ak4376->regmap, true);
		regcache_mark_dirty(ak4376->regmap);
		gpiod_set_value_cansleep(ak4376->reset_gpio, 0);
		ak4376->pdn1 = 0;
		ak4376->pdn2 = 0;
	}

out:
	mutex_unlock(&ak4376->lock);
	return ret;
}

static int ak4376_pdn_control(struct snd_soc_component *component, int pdn)
{
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);

	return ak4376_set_power(ak4376, pdn);
}

static int ak4376_hw_read(struct ak4376_priv *ak4376, unsigned int reg)
{
	u8 address = reg;
	u8 value;
	struct i2c_msg messages[] = {
		{
			.addr = ak4376->i2c->addr,
			.len = sizeof(address),
			.buf = &address,
		},
		{
			.addr = ak4376->i2c->addr,
			.flags = I2C_M_RD,
			.len = sizeof(value),
			.buf = &value,
		},
	};
	int ret;

	ret = i2c_transfer(ak4376->i2c->adapter, messages,
			   ARRAY_SIZE(messages));
	if (ret < 0)
		return ret;
	if (ret != ARRAY_SIZE(messages))
		return -EIO;

	return value;
}

static DECLARE_TLV_DB_SCALE(ovl_tlv, -1250, 50, 0);
static DECLARE_TLV_DB_SCALE(ovr_tlv, -1250, 50, 0);

/* HP-Amp Analog volume: -22 to 6 dB in 2 dB steps. */
static DECLARE_TLV_DB_SCALE(hpg_tlv, -2200, 200, 0);

static const char *const ak4376_ovolcn_select_texts[] = { "Dependent",
							  "Independent" };
static const char *const ak4376_mdacl_select_texts[] = { "x1", "x1/2" };
static const char *const ak4376_mdacr_select_texts[] = { "x1", "x1/2" };
static const char *const ak4376_invl_select_texts[] = { "Normal", "Inverting" };
static const char *const ak4376_invr_select_texts[] = { "Normal", "Inverting" };
static const char *const ak4376_cpmod_select_texts[] = { "Automatic Switching",
							 "+-VDD Operation",
							 "+-1/2VDD Operation" };
static const char *const ak4376_hphl_select_texts[] = { "9ohm", "Hi-Z" };
static const char *const ak4376_hphr_select_texts[] = { "9ohm", "Hi-Z" };
static const char *const ak4376_dacfil_select_texts[] = {
	"Sharp Roll-Off", "Slow Roll-Off", "Short Delay Sharp Roll-Off",
	"Short Delay Slow Roll-Off"
};
static const char *const ak4376_bcko_select_texts[] = { "64fs", "32fs" };
static const char *const ak4376_dfthr_select_texts[] = { "Digital Filter",
							 "Bypass" };
static const char *const ak4376_ngate_select_texts[] = { "On", "Off" };
static const char *const ak4376_ngatet_select_texts[] = { "Short", "Long" };

static const struct soc_enum ak4376_dac_enum[] = {
	SOC_ENUM_SINGLE(AK4376_0B_LCH_OUTPUT_VOLUME, 7,
			ARRAY_SIZE(ak4376_ovolcn_select_texts),
			ak4376_ovolcn_select_texts),
	SOC_ENUM_SINGLE(AK4376_07_DAC_MONO_MIXING, 2,
			ARRAY_SIZE(ak4376_mdacl_select_texts),
			ak4376_mdacl_select_texts),
	SOC_ENUM_SINGLE(AK4376_07_DAC_MONO_MIXING, 6,
			ARRAY_SIZE(ak4376_mdacr_select_texts),
			ak4376_mdacr_select_texts),
	SOC_ENUM_SINGLE(AK4376_07_DAC_MONO_MIXING, 3,
			ARRAY_SIZE(ak4376_invl_select_texts),
			ak4376_invl_select_texts),
	SOC_ENUM_SINGLE(AK4376_07_DAC_MONO_MIXING, 7,
			ARRAY_SIZE(ak4376_invr_select_texts),
			ak4376_invr_select_texts),
	SOC_ENUM_SINGLE(AK4376_03_POWER_MANAGEMENT4, 2,
			ARRAY_SIZE(ak4376_cpmod_select_texts),
			ak4376_cpmod_select_texts),
	SOC_ENUM_SINGLE(AK4376_04_OUTPUT_MODE_SETTING, 0,
			ARRAY_SIZE(ak4376_hphl_select_texts),
			ak4376_hphl_select_texts),
	SOC_ENUM_SINGLE(AK4376_04_OUTPUT_MODE_SETTING, 1,
			ARRAY_SIZE(ak4376_hphr_select_texts),
			ak4376_hphr_select_texts),
	SOC_ENUM_SINGLE(AK4376_06_DIGITAL_FILTER_SELECT, 6,
			ARRAY_SIZE(ak4376_dacfil_select_texts),
			ak4376_dacfil_select_texts),
	SOC_ENUM_SINGLE(AK4376_15_AUDIO_IF_FORMAT, 3,
			ARRAY_SIZE(ak4376_bcko_select_texts),
			ak4376_bcko_select_texts),
	SOC_ENUM_SINGLE(AK4376_06_DIGITAL_FILTER_SELECT, 3,
			ARRAY_SIZE(ak4376_dfthr_select_texts),
			ak4376_dfthr_select_texts),
	SOC_ENUM_SINGLE(AK4376_06_DIGITAL_FILTER_SELECT, 0,
			ARRAY_SIZE(ak4376_ngate_select_texts),
			ak4376_ngate_select_texts),
	SOC_ENUM_SINGLE(AK4376_06_DIGITAL_FILTER_SELECT, 1,
			ARRAY_SIZE(ak4376_ngatet_select_texts),
			ak4376_ngatet_select_texts),
};

static const char *const bickfreq_on_select[] = { "32fs", "48fs", "64fs" };
static const char *const pllmcki_on_select[] = { "9.6MHz", "11.2896MHz",
						 "12.288MHz", "19.2MHz" };
static const char *const lpmode_on_select[] = { "High Performance",
						"Low Power" };
static const char *const xtalfreq_on_select[] = { "12.288MHz", "11.2896MHz" };
static const char *const pdn_on_select[] = { "Off", "On" };

static const struct soc_enum ak4376_bitset_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(bickfreq_on_select), bickfreq_on_select),
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(pllmcki_on_select), pllmcki_on_select),
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(lpmode_on_select), lpmode_on_select),
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(xtalfreq_on_select), xtalfreq_on_select),
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(pdn_on_select), pdn_on_select),
};

static int get_bickfs(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);

	ucontrol->value.enumerated.item[0] = ak4376->nBickFreq;

	return 0;
}

static int ak4376_set_bickfs(struct snd_soc_component *component)
{
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);

	if (ak4376->nBickFreq == 0) { //32fs
		snd_soc_component_update_bits(component,
					      AK4376_15_AUDIO_IF_FORMAT, 0x03,
					      0x01); //DL1-0=01(16bit, >=32fs)
	} else if (ak4376->nBickFreq == 1) { //48fs
		snd_soc_component_update_bits(component,
					      AK4376_15_AUDIO_IF_FORMAT, 0x03,
					      0x00); //DL1-0=00(24bit, >=48fs)
	} else { //64fs
		snd_soc_component_update_bits(component,
					      AK4376_15_AUDIO_IF_FORMAT, 0x03,
					      0x02); //DL1-0=1x(32bit, >=64fs)
	}

	return 0;
}

static int set_bickfs(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);

	ak4376->nBickFreq = ucontrol->value.enumerated.item[0];

	ak4376_set_bickfs(component);

	return 0;
}

static int get_pllmcki(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);

	ucontrol->value.enumerated.item[0] = ak4376->nPllMCKI;

	return 0;
}

static int set_pllmcki(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);

	ak4376->nPllMCKI = ucontrol->value.enumerated.item[0];

	return 0;
}

static int get_lpmode(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);

	ucontrol->value.enumerated.item[0] = ak4376->lpmode;

	return 0;
}

static int ak4376_set_lpmode(struct snd_soc_component *component)
{
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);

	if (ak4376->lpmode == 0) { //High Performance Mode
		snd_soc_component_update_bits(
			component, AK4376_02_POWER_MANAGEMENT3, 0x10,
			0x00); //LPMODE=0(High Performance Mode)
		if (ak4376->fs1 <= 12000) {
			snd_soc_component_update_bits(component,
						      AK4376_24_MODE_CONTROL,
						      0x40, 0x40); //DSMLP=1
		} else {
			snd_soc_component_update_bits(component,
						      AK4376_24_MODE_CONTROL,
						      0x40, 0x00); //DSMLP=0
		}
	} else { //Low Power Mode
		snd_soc_component_update_bits(component,
					      AK4376_02_POWER_MANAGEMENT3, 0x10,
					      0x10); //LPMODE=1(Low Power Mode)
		snd_soc_component_update_bits(component, AK4376_24_MODE_CONTROL,
					      0x40, 0x40); //DSMLP=1
	}

	return 0;
}

static int set_lpmode(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);

	ak4376->lpmode = ucontrol->value.enumerated.item[0];

	ak4376_set_lpmode(component);

	return 0;
}

static int get_xtalfreq(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);

	ucontrol->value.enumerated.item[0] = ak4376->xtalfreq;

	return 0;
}

static int set_xtalfreq(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);

	ak4376->xtalfreq = ucontrol->value.enumerated.item[0];

	return 0;
}

static int get_pdn(struct snd_kcontrol *kcontrol,
		   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);

	ucontrol->value.enumerated.item[0] = ak4376->pdn2;

	return 0;
}

static int set_pdn(struct snd_kcontrol *kcontrol,
		   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);

	return ak4376_pdn_control(component,
				  ucontrol->value.enumerated.item[0]);
}

static const char *const ftm_hp_rev_text[] = { "NG", "OK" };
static const struct soc_enum ftm_hp_rev_enum =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(ftm_hp_rev_text), ftm_hp_rev_text);

static int ftm_hp_rev_get(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);
	bool power_down = !ak4376->pdn1;
	int retries = 50;
	int ret;

	if (power_down) {
		ret = ak4376_pdn_control(component, 1);
		if (ret)
			return ret;
		usleep_range(10000, 11000);
	}

	do {
		ret = ak4376_hw_read(ak4376, AK4376_15_AUDIO_IF_FORMAT);
		if (ret >= 0 && (ret & 0xe0) == 0x40)
			break;
		usleep_range(5000, 6000);
	} while (retries--);

	ucontrol->value.enumerated.item[0] = ret >= 0 && (ret & 0xe0) == 0x40;

	if (power_down)
		ak4376_pdn_control(component, 0);

	return 0;
}

static int ftm_hp_rev_put(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	return 0;
}

static const struct snd_kcontrol_new ak4376_snd_controls[] = {
	SOC_SINGLE_TLV("AK4376 Digital Output VolumeL",
		       AK4376_0B_LCH_OUTPUT_VOLUME, 0, 0x1F, 0, ovl_tlv),
	SOC_SINGLE_TLV("AK4376 Digital Output VolumeR",
		       AK4376_0C_RCH_OUTPUT_VOLUME, 0, 0x1F, 0, ovr_tlv),
	SOC_SINGLE_TLV("AK4376 HP-Amp Analog Volume",
		       AK4376_0D_HP_VOLUME_CONTROL, 0, 0x0F, 0, hpg_tlv),

	SOC_ENUM("AK4376 Digital Volume Control", ak4376_dac_enum[0]),
	SOC_ENUM("AK4376 DACL Signal Level", ak4376_dac_enum[1]),
	SOC_ENUM("AK4376 DACR Signal Level", ak4376_dac_enum[2]),
	SOC_ENUM("AK4376 DACL Signal Invert", ak4376_dac_enum[3]),
	SOC_ENUM("AK4376 DACR Signal Invert", ak4376_dac_enum[4]),
	SOC_ENUM("AK4376 Charge Pump Mode", ak4376_dac_enum[5]),
	SOC_ENUM("AK4376 HPL Power-down Resistor", ak4376_dac_enum[6]),
	SOC_ENUM("AK4376 HPR Power-down Resistor", ak4376_dac_enum[7]),
	SOC_ENUM("AK4376 DAC Digital Filter Mode", ak4376_dac_enum[8]),
	SOC_ENUM("AK4376 BICK Output Frequency", ak4376_dac_enum[9]),
	SOC_ENUM("AK4376 Digital Filter Mode", ak4376_dac_enum[10]),
	SOC_ENUM("AK4376 Noise Gate", ak4376_dac_enum[11]),
	SOC_ENUM("AK4376 Noise Gate Time", ak4376_dac_enum[12]),

	SOC_ENUM_EXT("AK4376 BICK Frequency Select", ak4376_bitset_enum[0],
		     get_bickfs, set_bickfs),
	SOC_ENUM_EXT("AK4376 PLL MCKI Frequency", ak4376_bitset_enum[1],
		     get_pllmcki, set_pllmcki),
	SOC_ENUM_EXT("AK4376 Low Power Mode", ak4376_bitset_enum[2], get_lpmode,
		     set_lpmode),
	SOC_ENUM_EXT("AK4376 Xtal Frequency", ak4376_bitset_enum[3],
		     get_xtalfreq, set_xtalfreq),
	SOC_ENUM_EXT("AK4376 PDN Control", ak4376_bitset_enum[4], get_pdn,
		     set_pdn),

	/*John.Xu@PSW.MM.AudioDriver.HeadsetDAC, 2017/01/03, Add for get spk revsion*/
	SOC_ENUM_EXT("HP_Pa Revision", ftm_hp_rev_enum, ftm_hp_rev_get,
		     ftm_hp_rev_put),
};

/* DAC control */
static int ak4376_dac_event2(struct snd_soc_component *component, int event)
{
	u8 MSmode;
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);

	MSmode = snd_soc_component_read32(component, AK4376_15_AUDIO_IF_FORMAT);
	switch (event) {
	case SND_SOC_DAPM_PRE_PMU: /* before widget power up */
		ak4376->nDACOn = 1;
		snd_soc_component_update_bits(component,
					      AK4376_01_POWER_MANAGEMENT2, 0x01,
					      0x01); //PMCP1=1
		mdelay(6); //wait 6ms
		udelay(500); //wait 0.5ms
		snd_soc_component_update_bits(component,
					      AK4376_01_POWER_MANAGEMENT2, 0x30,
					      0x30); //PMLDO1P/N=1
		mdelay(1); //wait 1ms
		break;
	case SND_SOC_DAPM_POST_PMU: /* after widget power up */
		snd_soc_component_update_bits(component,
					      AK4376_01_POWER_MANAGEMENT2, 0x02,
					      0x02); //PMCP2=1
		mdelay(4); //wait 4ms
		udelay(500); //wait 0.5ms
		break;
	case SND_SOC_DAPM_PRE_PMD: /* before widget power down */
		snd_soc_component_update_bits(component,
					      AK4376_01_POWER_MANAGEMENT2, 0x02,
					      0x00); //PMCP2=0
		break;
	case SND_SOC_DAPM_POST_PMD: /* after widget power down */
		snd_soc_component_update_bits(component,
					      AK4376_01_POWER_MANAGEMENT2, 0x30,
					      0x00); //PMLDO1P/N=0
		snd_soc_component_update_bits(component,
					      AK4376_01_POWER_MANAGEMENT2, 0x01,
					      0x00); //PMCP1=0

		if (ak4376->nPllMode == 0) {
			if (MSmode & 0x10) { //Master mode
				snd_soc_component_update_bits(
					component, AK4376_15_AUDIO_IF_FORMAT,
					0x10, 0x00); //MS bit = 0
			}
		}

		ak4376->nDACOn = 0;

		break;
	}
	return 0;
}

static int ak4376_dac_event(struct snd_soc_dapm_widget *w,
			    struct snd_kcontrol *kcontrol,
			    int event) //CONFIG_LINF
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);

	ak4376_dac_event2(component, event);

	return 0;
}

/* PLL control */
static int ak4376_pll_event2(struct snd_soc_component *component, int event)
{
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU: /* before widget power up */
	case SND_SOC_DAPM_POST_PMU: /* after widget power up */
		if ((ak4376->nPllMode == 1) || (ak4376->nPllMode == 2)) {
			snd_soc_component_update_bits(
				component, AK4376_00_POWER_MANAGEMENT1, 0x01,
				0x01); //PMPLL=1
		} else if (ak4376->nPllMode == 3) {
			snd_soc_component_update_bits(
				component, AK4376_00_POWER_MANAGEMENT1, 0x10,
				0x10); //PMOSC=1
		}
		break;
	case SND_SOC_DAPM_PRE_PMD: /* before widget power down */
	case SND_SOC_DAPM_POST_PMD: /* after widget power down */
		if ((ak4376->nPllMode == 1) || (ak4376->nPllMode == 2)) {
			snd_soc_component_update_bits(
				component, AK4376_00_POWER_MANAGEMENT1, 0x01,
				0x00); //PMPLL=0
		} else if (ak4376->nPllMode == 3) {
			snd_soc_component_update_bits(
				component, AK4376_00_POWER_MANAGEMENT1, 0x10,
				0x00); //PMOSC=0
		}
		break;
	}

	return 0;
}

static int ak4376_pll_event(struct snd_soc_dapm_widget *w,
			    struct snd_kcontrol *kcontrol,
			    int event) //CONFIG_LINF
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);

	ak4376_pll_event2(component, event);

	return 0;
}

/* HPL Mixer */
static const struct snd_kcontrol_new ak4376_hpl_mixer_controls[] = {
	SOC_DAPM_SINGLE("LDACL", AK4376_07_DAC_MONO_MIXING, 0, 1, 0),
	SOC_DAPM_SINGLE("RDACL", AK4376_07_DAC_MONO_MIXING, 1, 1, 0),
};

/* HPR Mixer */
static const struct snd_kcontrol_new ak4376_hpr_mixer_controls[] = {
	SOC_DAPM_SINGLE("LDACR", AK4376_07_DAC_MONO_MIXING, 4, 1, 0),
	SOC_DAPM_SINGLE("RDACR", AK4376_07_DAC_MONO_MIXING, 5, 1, 0),
};

/* ak4376 dapm widgets */
static const struct snd_soc_dapm_widget ak4376_dapm_widgets[] = {
	// DAC
	SND_SOC_DAPM_DAC_E("AK4376 DAC", "NULL", AK4376_02_POWER_MANAGEMENT3, 0,
			   0, ak4376_dac_event,
			   (SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD |
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD)),

	// PLL, OSC
	SND_SOC_DAPM_SUPPLY_S("AK4376 PLL", 0, SND_SOC_NOPM, 0, 0,
			      ak4376_pll_event,
			      (SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD |
			       SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD)),

	SND_SOC_DAPM_AIF_IN("AK4376 SDTI", "Playback", 0, SND_SOC_NOPM, 0, 0),

	// Analog Output
	SND_SOC_DAPM_OUTPUT("AK4376 HPL"),
	SND_SOC_DAPM_OUTPUT("AK4376 HPR"),

	SND_SOC_DAPM_MIXER("AK4376 HPR Mixer", AK4376_03_POWER_MANAGEMENT4, 1,
			   0, &ak4376_hpr_mixer_controls[0],
			   ARRAY_SIZE(ak4376_hpr_mixer_controls)),

	SND_SOC_DAPM_MIXER("AK4376 HPL Mixer", AK4376_03_POWER_MANAGEMENT4, 0,
			   0, &ak4376_hpl_mixer_controls[0],
			   ARRAY_SIZE(ak4376_hpl_mixer_controls)),

};

static const struct snd_soc_dapm_route ak4376_intercon[] = {

	{ "AK4376 DAC", NULL, "AK4376 PLL" },
	{ "AK4376 DAC", NULL, "AK4376 SDTI" },

	{ "AK4376 HPL Mixer", "LDACL", "AK4376 DAC" },
	{ "AK4376 HPL Mixer", "RDACL", "AK4376 DAC" },
	{ "AK4376 HPR Mixer", "LDACR", "AK4376 DAC" },
	{ "AK4376 HPR Mixer", "RDACR", "AK4376 DAC" },

	{ "AK4376 HPL", NULL, "AK4376 HPL Mixer" },
	{ "AK4376 HPR", NULL, "AK4376 HPR Mixer" },

};

static int ak4376_set_mcki(struct snd_soc_component *component, int fs,
			   int rclk)
{
	u8 mode;
	u8 mode2;
	int mcki_rate;
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);

	if ((fs != 0) && (rclk != 0)) {
		if (rclk > 28800000)
			return -EINVAL;

		if (ak4376->nPllMode == 0) { //PLL_OFF
			mcki_rate = rclk / fs;
		} else { //XTAL_MODE
			if (ak4376->xtalfreq == 0) { //12.288MHz
				mcki_rate = 12288000 / fs;
			} else { //11.2896MHz
				mcki_rate = 11289600 / fs;
			}
		}

		mode = snd_soc_component_read32(component,
						AK4376_05_CLOCK_MODE_SELECT);
		mode &= ~AK4376_CM;

		if (ak4376->lpmode == 0) { //High Performance Mode
			switch (mcki_rate) {
			case 32:
				mode |= AK4376_CM_0;
				break;
			case 64:
				mode |= AK4376_CM_1;
				break;
			case 128:
				mode |= AK4376_CM_3;
				break;
			case 256:
				mode |= AK4376_CM_0;
				mode2 = snd_soc_component_read32(
					component, AK4376_24_MODE_CONTROL);
				if (fs <= 12000) {
					mode2 |= 0x40; //DSMLP=1
					snd_soc_component_write(
						component,
						AK4376_24_MODE_CONTROL, mode2);
				} else {
					mode2 &= ~0x40; //DSMLP=0
					snd_soc_component_write(
						component,
						AK4376_24_MODE_CONTROL, mode2);
				}
				break;
			case 512:
				mode |= AK4376_CM_1;
				break;
			case 1024:
				mode |= AK4376_CM_2;
				break;
			default:
				return -EINVAL;
			}
		} else { //Low Power Mode (LPMODE == DSMLP == 1)
			switch (mcki_rate) {
			case 32:
				mode |= AK4376_CM_0;
				break;
			case 64:
				mode |= AK4376_CM_1;
				break;
			case 128:
				mode |= AK4376_CM_3;
				break;
			case 256:
				mode |= AK4376_CM_0;
				break;
			case 512:
				mode |= AK4376_CM_1;
				break;
			case 1024:
				mode |= AK4376_CM_2;
				break;
			default:
				return -EINVAL;
			}
		}

		snd_soc_component_write(component, AK4376_05_CLOCK_MODE_SELECT,
					mode);
	}

	return 0;
}

static int ak4376_set_pllblock(struct snd_soc_component *component, int fs)
{
	u8 mode;
	int nMClk, nPLLClk, nRefClk;
	int PLDbit, PLMbit, MDIVbit;
	int PLLMCKI;
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);

	mode = snd_soc_component_read32(component, AK4376_05_CLOCK_MODE_SELECT);
	mode &= ~AK4376_CM;

	if (fs <= 24000) {
		mode |= AK4376_CM_1;
		nMClk = 512 * fs;
	} else if (fs <= 96000) {
		mode |= AK4376_CM_0;
		nMClk = 256 * fs;
	} else if (fs <= 192000) {
		mode |= AK4376_CM_3;
		nMClk = 128 * fs;
	} else { //fs > 192kHz
		mode |= AK4376_CM_1;
		nMClk = 64 * fs;
	}

	snd_soc_component_write(component, AK4376_05_CLOCK_MODE_SELECT, mode);

	if ((fs % 8000) == 0) {
		nPLLClk = 122880000;
	} else if ((fs == 11025) && (ak4376->nBickFreq == 1) &&
		   (ak4376->nPllMode == 1)) {
		nPLLClk = 101606400;
	} else {
		nPLLClk = 112896000;
	}

	if (ak4376->nPllMode == 1) { //BICK_PLL (Slave)
		if (ak4376->nBickFreq == 0) { //32fs
			if (fs <= 96000)
				PLDbit = 1;
			else if (fs <= 192000)
				PLDbit = 2;
			else
				PLDbit = 4;
			nRefClk = 32 * fs / PLDbit;
		} else if (ak4376->nBickFreq == 1) { //48fs
			if (fs <= 16000)
				PLDbit = 1;
			else if (fs <= 192000)
				PLDbit = 3;
			else
				PLDbit = 6;
			nRefClk = 48 * fs / PLDbit;
		} else { // 64fs
			if (fs <= 48000)
				PLDbit = 1;
			else if (fs <= 96000)
				PLDbit = 2;
			else if (fs <= 192000)
				PLDbit = 4;
			else
				PLDbit = 8;
			nRefClk = 64 * fs / PLDbit;
		}
	}

	else { //MCKI_PLL (Master)
		if (ak4376->nPllMCKI == 0) { //9.6MHz
			PLLMCKI = 9600000;
			if ((fs % 4000) == 0)
				nRefClk = 1920000;
			else
				nRefClk = 384000;
		} else if (ak4376->nPllMCKI == 1) { //11.2896MHz
			PLLMCKI = 11289600;
			if ((fs % 4000) == 0)
				return -EINVAL;
			nRefClk = 2822400;
		} else if (ak4376->nPllMCKI == 2) { //12.288MHz
			PLLMCKI = 12288000;
			if ((fs % 4000) == 0)
				nRefClk = 3072000;
			else
				nRefClk = 768000;
		} else { //19.2MHz
			PLLMCKI = 19200000;
			if ((fs % 4000) == 0)
				nRefClk = 1920000;
			else
				nRefClk = 384000;
		}
		PLDbit = PLLMCKI / nRefClk;
	}

	PLMbit = nPLLClk / nRefClk;
	MDIVbit = nPLLClk / nMClk;

	PLDbit--;
	PLMbit--;
	MDIVbit--;

	//PLD15-0
	snd_soc_component_write(component, AK4376_0F_PLL_REF_CLK_DIVIDER1,
				((PLDbit & 0xFF00) >> 8));
	snd_soc_component_write(component, AK4376_10_PLL_REF_CLK_DIVIDER2,
				((PLDbit & 0x00FF) >> 0));
	//PLM15-0
	snd_soc_component_write(component, AK4376_11_PLL_FB_CLK_DIVIDER1,
				((PLMbit & 0xFF00) >> 8));
	snd_soc_component_write(component, AK4376_12_PLL_FB_CLK_DIVIDER2,
				((PLMbit & 0x00FF) >> 0));

	if (ak4376->nPllMode == 1) { //BICK_PLL (Slave)
		snd_soc_component_update_bits(component,
					      AK4376_0E_PLL_CLK_SOURCE_SELECT,
					      0x03, 0x01); //PLS=1(BICK)
	} else { //MCKI PLL (Slave/Master)
		snd_soc_component_update_bits(component,
					      AK4376_0E_PLL_CLK_SOURCE_SELECT,
					      0x03, 0x00); //PLS=0(MCKI)
	}

	//MDIV7-0
	snd_soc_component_write(component, AK4376_14_DAC_CLK_DIVIDER, MDIVbit);

	return 0;
}

static int ak4376_set_timer(struct snd_soc_component *component)
{
	int ret, curdata;
	int count, tm, nfs;
	int lvdtm, vddtm;
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);

	lvdtm = 0;
	vddtm = 0;
	nfs = ak4376->fs1;

	//LVDTM2-0 bits set
	ret = snd_soc_component_read32(component, AK4376_03_POWER_MANAGEMENT4);
	curdata = (ret & 0x70) >> 4; //Current data Save
	ret &= ~0x70;
	do {
		count = 1000 * (64 << lvdtm);
		tm = count / nfs;
		if (tm > LVDTM_HOLD_TIME)
			break;
		lvdtm++;
	} while (lvdtm < 7); //LVDTM2-0 = 0~7
	if (curdata != lvdtm) {
		snd_soc_component_write(component, AK4376_03_POWER_MANAGEMENT4,
					(ret | (lvdtm << 4)));
	}

	//VDDTM3-0 bits set
	ret = snd_soc_component_read32(component,
				       AK4376_04_OUTPUT_MODE_SETTING);
	curdata = (ret & 0x3C) >> 2; //Current data Save
	ret &= ~0x3C;
	do {
		count = 1000 * (1024 << vddtm);
		tm = count / nfs;
		if (tm > VDDTM_HOLD_TIME)
			break;
		vddtm++;
	} while (vddtm < 8); //VDDTM3-0 = 0~8
	if (curdata != vddtm) {
		snd_soc_component_write(component,
					AK4376_04_OUTPUT_MODE_SETTING,
					(ret | (vddtm << 2)));
	}

	return 0;
}

static int ak4376_hw_params_set(struct snd_soc_component *component, int nfs1)
{
	u8 fs;
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);

	fs = snd_soc_component_read32(component, AK4376_05_CLOCK_MODE_SELECT);
	fs &= ~AK4376_FS;

	switch (nfs1) {
	case 8000:
		fs |= AK4376_FS_8KHZ;
		break;
	case 11025:
		fs |= AK4376_FS_11_025KHZ;
		break;
	case 16000:
		fs |= AK4376_FS_16KHZ;
		break;
	case 22050:
		fs |= AK4376_FS_22_05KHZ;
		break;
	case 32000:
		fs |= AK4376_FS_32KHZ;
		break;
	case 44100:
		fs |= AK4376_FS_44_1KHZ;
		break;
	case 48000:
		fs |= AK4376_FS_48KHZ;
		break;
	case 88200:
		fs |= AK4376_FS_88_2KHZ;
		break;
	case 96000:
		fs |= AK4376_FS_96KHZ;
		break;
	case 176400:
		fs |= AK4376_FS_176_4KHZ;
		break;
	case 192000:
		fs |= AK4376_FS_192KHZ;
		break;
	case 352800:
		fs |= AK4376_FS_352_8KHZ;
		break;
	case 384000:
		fs |= AK4376_FS_384KHZ;
		break;
	default:
		return -EINVAL;
	}
	snd_soc_component_write(component, AK4376_05_CLOCK_MODE_SELECT, fs);

	if (ak4376->nPllMode == 0) { //PLL Off
		snd_soc_component_update_bits(component,
					      AK4376_13_DAC_CLK_SOURCE, 0x03,
					      0x00); //DACCKS=0
		ak4376_set_mcki(component, nfs1, ak4376->rclk);
	} else if (ak4376->nPllMode == 3) { //XTAL MODE
		snd_soc_component_update_bits(component,
					      AK4376_13_DAC_CLK_SOURCE, 0x03,
					      0x02); //DACCKS=2
		ak4376_set_mcki(component, nfs1, ak4376->rclk);
	} else { //PLL mode
		snd_soc_component_update_bits(component,
					      AK4376_13_DAC_CLK_SOURCE, 0x03,
					      0x01); //DACCKS=1
		ak4376_set_pllblock(component, nfs1);
	}

	ak4376_set_timer(component);

	return 0;
}

static int ak4376_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);

	ak4376->fs1 = params_rate(params);
	return ak4376_hw_params_set(component, ak4376->fs1);
}

static int ak4376_set_dai_sysclk(struct snd_soc_dai *dai, int clk_id,
				 unsigned int freq, int dir)
{
	struct snd_soc_component *component = dai->component;
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);

	ak4376->rclk = freq;
	if (ak4376->nPllMode == 0 || ak4376->nPllMode == 3)
		return ak4376_set_mcki(component, ak4376->fs1, freq);

	return 0;
}

static int ak4376_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);
	unsigned int format;

	format = snd_soc_component_read32(component, AK4376_15_AUDIO_IF_FORMAT);
	format &= ~AK4376_DIF;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		format |= AK4376_SLAVE_MODE;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		if (ak4376->nDeviceID != 2)
			return -EINVAL;
		format |= AK4376_MASTER_MODE;
		break;
	default:
		dev_err(component->dev, "unsupported clock mode\n");
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		format |= AK4376_DIF_I2S_MODE;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		format |= AK4376_DIF_MSB_MODE;
		break;
	default:
		return -EINVAL;
	}

	return snd_soc_component_write(component, AK4376_15_AUDIO_IF_FORMAT,
				       format);
}

static int ak4376_set_bias_level(struct snd_soc_component *component,
				 enum snd_soc_bias_level level)
{
	enum snd_soc_bias_level old_level;

	old_level = snd_soc_component_get_bias_level(component);
	if (level == SND_SOC_BIAS_STANDBY && old_level == SND_SOC_BIAS_OFF)
		return ak4376_pdn_control(component, 1);
	if (level == SND_SOC_BIAS_OFF && old_level != SND_SOC_BIAS_OFF)
		return ak4376_pdn_control(component, 0);

	return 0;
}

static int ak4376_set_dai_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_component *component = dai->component;
	struct ak4376_priv *ak4376 = snd_soc_component_get_drvdata(component);
	unsigned int mode;

	if (ak4376->nPllMode == 0 && !ak4376->nDACOn) {
		mode = snd_soc_component_read32(component,
						AK4376_15_AUDIO_IF_FORMAT);
		if (mode & AK4376_MASTER_MODE)
			snd_soc_component_update_bits(component,
						      AK4376_15_AUDIO_IF_FORMAT,
						      AK4376_MASTER_MODE, 0);
	}

	return 0;
}

#define AK4376_RATES                                                           \
	(SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 | SNDRV_PCM_RATE_16000 |   \
	 SNDRV_PCM_RATE_22050 | SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |  \
	 SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000 |  \
	 SNDRV_PCM_RATE_176400 | SNDRV_PCM_RATE_192000)

#define AK4376_FORMATS                                                         \
	(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE |                   \
	 SNDRV_PCM_FMTBIT_S32_LE)

static const struct snd_soc_dai_ops ak4376_dai_ops = {
	.hw_params = ak4376_hw_params,
	.set_sysclk = ak4376_set_dai_sysclk,
	.set_fmt = ak4376_set_dai_fmt,
	.digital_mute = ak4376_set_dai_mute,
};

static struct snd_soc_dai_driver ak4376_dai = {
	.name = "ak4376-AIF1",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = AK4376_RATES,
		.formats = AK4376_FORMATS,
	},
	.ops = &ak4376_dai_ops,
};

static void ak4376_component_remove(struct snd_soc_component *component)
{
	ak4376_pdn_control(component, 0);
}

static int ak4376_component_suspend(struct snd_soc_component *component)
{
	return ak4376_pdn_control(component, 0);
}

static int ak4376_component_resume(struct snd_soc_component *component)
{
	return 0;
}

static const struct snd_soc_component_driver ak4376_component_driver = {
	.non_legacy_dai_naming = 1,
	.use_pmdown_time = 1,
	.endianness = 1,
	.controls = ak4376_snd_controls,
	.num_controls = ARRAY_SIZE(ak4376_snd_controls),
	.dapm_widgets = ak4376_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(ak4376_dapm_widgets),
	.dapm_routes = ak4376_intercon,
	.num_dapm_routes = ARRAY_SIZE(ak4376_intercon),
	.remove = ak4376_component_remove,
	.suspend = ak4376_component_suspend,
	.resume = ak4376_component_resume,
	.set_bias_level = ak4376_set_bias_level,
};

static const struct regmap_config ak4376_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = AK4376_2A_DAC_ADJUSTMENT_2,
	.reg_defaults = ak4376_reg,
	.num_reg_defaults = ARRAY_SIZE(ak4376_reg),
	.cache_type = REGCACHE_RBTREE,
};

static bool ak4376_has_supply(struct device_node *node, const char *property)
{
	return of_find_property(node, property, NULL);
}

static int ak4376_get_supply(struct device *dev, struct regulator **supply,
				     const char *legacy_id,
				     const char *legacy_property,
				     const char *current_id,
				     const char *current_property,
				     bool use_legacy)
{
	const char *id;
	int ret;

	if (use_legacy && ak4376_has_supply(dev->of_node, legacy_property))
		id = legacy_id;
	else if (!use_legacy &&
		 ak4376_has_supply(dev->of_node, current_property))
		id = current_id;
	else if (ak4376_has_supply(dev->of_node, current_property))
		id = current_id;
	else if (ak4376_has_supply(dev->of_node, legacy_property))
		id = legacy_id;
	else
		return 0;

	*supply = devm_regulator_get(dev, id);
	if (IS_ERR(*supply))
		return PTR_ERR(*supply);

	if (regulator_count_voltages(*supply) > 0) {
		ret = regulator_set_voltage(*supply, AK4376_SUPPLY_UV,
					    AK4376_SUPPLY_UV);
		if (ret)
			return ret;
	}

	return regulator_set_load(*supply, AK4376_SUPPLY_LOAD_UA);
}

static void ak4376_disable_resources(void *data)
{
	struct ak4376_priv *ak4376 = data;

	regcache_cache_only(ak4376->regmap, true);
	regcache_mark_dirty(ak4376->regmap);
	gpiod_set_value_cansleep(ak4376->reset_gpio, 0);
	ak4376->pdn1 = 0;
	ak4376->pdn2 = 0;
	if (ak4376->audio_vdd_gpio)
		gpiod_set_value_cansleep(ak4376->audio_vdd_gpio, 0);
	if (ak4376->avdd_enabled) {
		regulator_disable(ak4376->avdd);
		regulator_set_load(ak4376->avdd, 0);
		ak4376->avdd_enabled = false;
	}
	if (ak4376->tvdd_enabled) {
		regulator_disable(ak4376->tvdd);
		regulator_set_load(ak4376->tvdd, 0);
		ak4376->tvdd_enabled = false;
	}
}

static int ak4376_enable_resources(struct ak4376_priv *ak4376)
{
	int ret;

	if (ak4376->tvdd) {
		ret = regulator_enable(ak4376->tvdd);
		if (ret)
			return ret;
		ak4376->tvdd_enabled = true;
	}

	if (ak4376->avdd) {
		ret = regulator_enable(ak4376->avdd);
		if (ret)
			return ret;
		ak4376->avdd_enabled = true;
	}

	if (ak4376->audio_vdd_gpio)
		gpiod_set_value_cansleep(ak4376->audio_vdd_gpio, 1);

	usleep_range(1000, 2000);
	return 0;
}

static int ak4376_identify(struct ak4376_priv *ak4376)
{
	struct device *dev = &ak4376->i2c->dev;
	int device_id;
	int ret;

	ret = ak4376_set_power(ak4376, true);
	if (ret)
		return ret;

	device_id = ak4376_hw_read(ak4376, AK4376_15_AUDIO_IF_FORMAT);
	if (device_id < 0) {
		ret = device_id;
		goto power_down;
	}

	ak4376->nDeviceID = device_id >> 5;
	switch (ak4376->nDeviceID) {
	case 0:
		dev_info(dev, "AK4375 detected\n");
		break;
	case 1:
		dev_info(dev, "AK4375A detected\n");
		break;
	case 2:
		dev_info(dev, "AK4376 detected\n");
		break;
	default:
		dev_warn(dev, "unexpected device ID %#x\n", device_id);
		break;
	}

power_down:
	if (ak4376_set_power(ak4376, false) && !ret)
		ret = -EIO;

	return ret;
}

static int ak4376_i2c_probe(struct i2c_client *i2c,
				    const struct i2c_device_id *id)
{
	struct device *dev = &i2c->dev;
	struct ak4376_priv *ak4376;
	int ret;

	if (!dev->of_node)
		return -EINVAL;

	ak4376 = devm_kzalloc(dev, sizeof(*ak4376), GFP_KERNEL);
	if (!ak4376)
		return -ENOMEM;

	mutex_init(&ak4376->lock);
	ak4376->i2c = i2c;
	ak4376->fs1 = 48000;
	ak4376->nBickFreq = 1;
	ak4376->nPllMode = 2;
	i2c_set_clientdata(i2c, ak4376);

	ret = oppo_project_info_init();
	if (ret)
		return dev_err_probe(dev, ret,
				     "OPPO project information is unavailable\n");

	ak4376->pcb_version = get_PCB_Version();
	if (ak4376->pcb_version <= HW_VERSION__UNKNOWN ||
	    ak4376->pcb_version > HW_VERSION__16)
		return dev_err_probe(dev, -EINVAL,
				     "invalid OPPO PCB version %u\n",
				     ak4376->pcb_version);
	dev_info(dev, "OPPO project %u PCB version %u\n",
		 get_project(), ak4376->pcb_version);

	ak4376->regmap = devm_regmap_init_i2c(i2c, &ak4376_regmap_config);
	if (IS_ERR(ak4376->regmap))
		return dev_err_probe(dev, PTR_ERR(ak4376->regmap),
				     "failed to initialize regmap\n");

	regcache_cache_only(ak4376->regmap, true);
	regcache_mark_dirty(ak4376->regmap);

	ak4376->reset_gpio =
		devm_gpiod_get_from_of_node(dev, dev->of_node,
					    "ak4376,reset-gpio", 0,
					    GPIOD_OUT_LOW, "ak4376-reset");
	if (IS_ERR(ak4376->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ak4376->reset_gpio),
				     "failed to get reset GPIO\n");

	if (ak4376->pcb_version < HW_VERSION__12 &&
	    of_find_property(dev->of_node, "audio-vdd-enable-gpio", NULL)) {
		ak4376->audio_vdd_gpio = devm_gpiod_get_from_of_node(
			dev, dev->of_node, "audio-vdd-enable-gpio", 0,
			GPIOD_OUT_LOW, "ak4376-audio-vdd");
		if (IS_ERR(ak4376->audio_vdd_gpio))
			return dev_err_probe(dev,
					     PTR_ERR(ak4376->audio_vdd_gpio),
					     "failed to get audio VDD GPIO\n");
	}

	ret = ak4376_get_supply(dev, &ak4376->tvdd, "ak4376-tvdd",
				"ak4376-tvdd-supply", "ak4376-tvdd-L8",
				"ak4376-tvdd-L8-supply",
				ak4376->pcb_version == HW_VERSION__10);
	if (ret)
		return dev_err_probe(dev, ret, "failed to configure TVDD\n");

	ret = ak4376_get_supply(dev, &ak4376->avdd, "ak4376-avdd",
				"ak4376-avdd-supply", "ak4376-avdd-L8",
				"ak4376-avdd-L8-supply",
				ak4376->pcb_version == HW_VERSION__10);
	if (ret)
		return dev_err_probe(dev, ret, "failed to configure AVDD\n");

	ret = devm_add_action_or_reset(dev, ak4376_disable_resources, ak4376);
	if (ret)
		return ret;

	ret = ak4376_enable_resources(ak4376);
	if (ret)
		return dev_err_probe(dev, ret, "failed to enable supplies\n");

	ret = ak4376_identify(ak4376);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to communicate with codec\n");

	return devm_snd_soc_register_component(dev, &ak4376_component_driver,
					       &ak4376_dai, 1);
}

static const struct of_device_id ak4376_of_match[] = { { .compatible =
								 "akm,ak4376" },
						       {} };
MODULE_DEVICE_TABLE(of, ak4376_of_match);

static const struct i2c_device_id ak4376_i2c_id[] = { { "ak4376", 0 }, {} };
MODULE_DEVICE_TABLE(i2c, ak4376_i2c_id);

static struct i2c_driver ak4376_i2c_driver = {
	.driver = {
		.name = "ak4376",
		.of_match_table = ak4376_of_match,
	},
	.probe = ak4376_i2c_probe,
	.id_table = ak4376_i2c_id,
};
module_i2c_driver(ak4376_i2c_driver);

MODULE_DESCRIPTION("ASoC AK4376 codec driver");
MODULE_LICENSE("GPL");
