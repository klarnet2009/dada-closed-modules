/*
 * Copyright (C) 2018, SI-IN, Yun Shi (yun.shi@si-in.com).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define DEBUG
#define LOG_FLAG	"sipa_91xx"


#include <linux/regmap.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>
#include <sound/soc-dapm.h>
#include <linux/interrupt.h>
#include <sound/pcm_params.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/firmware.h>
#include <linux/device.h>
#include <linux/kernel.h>

#include "sipa_regmap.h"
#include "sia91xx_common.h"
#include "sipa_parameter_typedef.h"
#include "sipa_parameter.h"


static struct snd_soc_dapm_widget sia91xx_dapm_widgets_common[] = {
	/* Stream widgets */
	SND_SOC_DAPM_AIF_IN("AIF IN", "AIF Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF OUT", "AIF Capture", 0, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_OUTPUT("OUTL"),
	SND_SOC_DAPM_INPUT("AEC Loopback"),
};

static const struct snd_soc_dapm_route sia91xx_dapm_routes_common[] = {
	{ "OUTL", NULL, "AIF IN" },
	{ "AIF OUT", NULL, "AEC Loopback" },
};

const struct sia91xx_irq_desc irq_range[] = {
	{0x1, "PORI"},
	{0x4, "NOCLK"},
	{0x8, "OTPI"},
	{0x20, "UVPI"},
	{0x40, "OCPI"},
	{0x80, "TDMERR"},
};

static irqreturn_t sia91xx_irq(
	int irq,
	void *data)
{
	sipa_dev_t *si_pa = (sipa_dev_t *)data;
	if (NULL == si_pa) {
		pr_err("[  err][%s] %s: si_pa is NULL !!! \n", LOG_FLAG, __func__);
		return IRQ_HANDLED;
	}
	queue_delayed_work(si_pa->sia91xx_wq, &si_pa->interrupt_work, 0);

	return IRQ_HANDLED;
}


void sia91xx_register_interrupt(sipa_dev_t *si_pa)
{
	int irq_flags;
	int ret;

	if (gpio_is_valid(si_pa->irq_pin)) {
		irq_flags = IRQF_TRIGGER_RISING;

		if (si_pa->channel_num == SIPA_CHANNEL_0) {
			ret = request_irq(gpio_to_irq(si_pa->irq_pin), sia91xx_irq,
				irq_flags, "sia91xx_L", si_pa);
		} else {
			ret = request_irq(gpio_to_irq(si_pa->irq_pin), sia91xx_irq,
				irq_flags, "sia91xx_R", si_pa);
		}
	} else {
		pr_err("[  err][%s] %s: irq pin error !!! \n", LOG_FLAG, __func__);
	}
}

#if 0
void sia91xx_monitor(struct work_struct *work)
{
	uint16_t IntVal;
	sipa_dev_t *si_pa = container_of(work, sipa_dev_t, interrupt_work.work);

	sia91xx_read_reg16(si_pa, SIA91XX_REG_INT_STAT, &IntVal);
	if (IntVal & 0x3ff) {
		pr_err("[  err]%s: interrupt event occurred !!! \n", __func__);
	}

	/* reschedule */
	queue_delayed_work(si_pa->sia91xx_wq, &si_pa->monitor_work, 5*HZ);

}
#endif

void sia91xx_interrupt(struct work_struct *work)
{
	sipa_dev_t *si_pa = container_of(work, sipa_dev_t, interrupt_work.work);
	unsigned int IntReg = 0, IntEn = 0, IntVal, regVal = 0;
	unsigned int i;

	sipa_read_reg(si_pa->regmap, SIA91XX_REG_INT_REG, &IntReg);
	sipa_read_reg(si_pa->regmap, SIA91XX_REG_INT_EN, &IntEn);

	if (0 == (IntEn & IntReg)) {
		pr_err("[  err][%s] %s: interrupt source error !!! \n",LOG_FLAG, __func__);
		return;
	}
	IntVal = IntReg & 0xffff;	/* G41a: stock and w20, w8, #0xffff */

	for (i = 0; i < ARRAY_SIZE(irq_range); i++) {
		if (IntVal == irq_range[i].index)
			break;
	}

#if 0
	switch (IntReg & 0xff) {
	case IRQ_NOCLK:
		pr_debug("interrupt event desc = %s\n", irq_range[i].desc);
		break;
	case IRQ_OTPI:
		pr_debug("interrupt event desc = %s\n", irq_range[i].desc);
		break;
	case IRQ_UVPI:
		pr_debug("interrupt event desc = %s\n",  irq_range[i].desc);
		break;
	case IRQ_OCPI:
		pr_debug("interrupt event desc = %s\n", irq_range[i].desc);
		break;
	case IRQ_TDM:
		pr_debug("interrupt event desc = %s\n", irq_range[i].desc);
		break;
	default:
		break;
	}
#endif

	sipa_read_reg(si_pa->regmap, SIA91XX_REG_INT_STAT, &regVal);
	usleep_range(1000, 2000);	/* G41a: stock usleep_range_state(1000, 2000) */
	sipa_write_reg(si_pa->regmap, SIA91XX_REG_INT_WC_REG, IntVal);
}

/* g3p: stock sia91xx_get_rst_value (inlined into sia91xx_detect_chip) */
static __always_inline void sia91xx_get_rst_value(sipa_dev_t *si_pa)
{
	pr_debug("[debug][%s] %s: reset pin num:%u, value:%u \r\n", "sipa_91xx", __func__,
		si_pa->rst_pin, gpiod_get_raw_value(gpio_to_desc(si_pa->rst_pin)));
}

void *sia91xx_devm_kstrdup(struct device *dev, char *buf)
{
	char *str = devm_kzalloc(dev, strlen(buf) + 1, GFP_KERNEL);
	if (!str)
		return str;

	memcpy(str, buf, strlen(buf));
	return str;
}


int sia91xx_append_i2c_address(
	struct device *dev,
	struct i2c_client *i2c,
	struct snd_soc_dapm_widget *widgets,
	int num_widgets,
	struct snd_soc_dai_driver *dai_drv,
	int num_dai)
{
	char buf[50];
	int i;
	int i2cbus = i2c->adapter->nr;
	int addr = i2c->addr;

	if (dai_drv && num_dai > 0) {
		for (i = 0; i < num_dai; i++) {
			snprintf(buf, 50, "%s-%x-%x", dai_drv[i].name, i2cbus, addr);
			dai_drv[i].name = sia91xx_devm_kstrdup(dev, buf);

			snprintf(buf, 50, "%s-%x-%x",
						dai_drv[i].playback.stream_name,
						i2cbus, addr);
			dai_drv[i].playback.stream_name = sia91xx_devm_kstrdup(dev, buf);

			snprintf(buf, 50, "%s-%x-%x",
						dai_drv[i].capture.stream_name,
						i2cbus,
						addr);
			dai_drv[i].capture.stream_name = sia91xx_devm_kstrdup(dev, buf);
		}
	}

	if (widgets && num_widgets > 0) {
		for (i = 0; i < num_widgets; i++) {
			if (!widgets[i].sname)
				continue;

			if ((widgets[i].id == snd_soc_dapm_aif_in)
				|| (widgets[i].id == snd_soc_dapm_aif_out)) {
				snprintf(buf, 50, "%s-%x-%x", widgets[i].sname, i2cbus, addr);
				widgets[i].sname = sia91xx_devm_kstrdup(dev, buf);
			}
		}
	}
	return 0;
}

/* g3p: rewritten from the stock disassembly (reset sequence depends on the chip type mask 0x1b00000) */
int sia91xx_detect_chip(
	sipa_dev_t *si_pa)
{
	int ret;

	/* Power up! */
	if (si_pa && gpio_is_valid(si_pa->rst_pin)) {
		if (si_pa->chip_type <= 0x18 && ((1u << si_pa->chip_type) & 0x1b00000)) {
			gpiod_set_raw_value_cansleep(gpio_to_desc(si_pa->rst_pin), 1);
			usleep_range(10000, 12000);
		} else {
			gpiod_set_raw_value_cansleep(gpio_to_desc(si_pa->rst_pin), 1);
			usleep_range(5000, 6000);
			gpiod_set_raw_value_cansleep(gpio_to_desc(si_pa->rst_pin), 0);
			usleep_range(5000, 6000);
		}
	}

	if (0 == si_pa->disable_pin)
		sia91xx_get_rst_value(si_pa);

	ret = sipa_regmap_check_chip_id(si_pa->regmap,
		si_pa->channel_num, si_pa->chip_type);
	if (ret < 0) {
		pr_err("[  err][%s] %s: Failed to read Revision register: %d \n",
			LOG_FLAG, __func__, ret);
		return -EIO;
	}

	if (si_pa->chip_type <= 0x18 && ((1u << si_pa->chip_type) & 0x1b00000))
		gpiod_set_raw_value(gpio_to_desc(si_pa->rst_pin), 0);

	return 0;
}


int sia91xx_hw_params(
	struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params,
	struct snd_soc_dai *dai)
{
	//struct snd_soc_codec *codec = dai->codec;
	//sipa_dev_t *si_pa = snd_soc_codec_get_drvdata(codec);
	unsigned int rate, width, channels;

	if (SNDRV_PCM_STREAM_CAPTURE == substream->stream) {
		return 0;
	}

	rate = params_rate(params);
	/* g15c: stock format (disassembly: x3 = dai->name, x4 = substream->name, w5 = rate) */
	pr_info("[ info][%s] %s: dai:%s %s i2s rate: %u !!! \n",
		LOG_FLAG, __func__, dai->name, substream->name, rate);

	channels = params_channels(params);
	pr_info("[ info][%s] %s: i2s channel: %u !!! \n", 
		LOG_FLAG, __func__, channels);
	if (channels < CHANNEL_NUM_MIN || channels > CHANNEL_NUM_MAX) {
		pr_err("[  err][%s] %s: i2s channel not match !!! \n", 
			LOG_FLAG, __func__);
		return -SIPA_ERROR_I2S_UNMATCH;
	}

	width = snd_pcm_format_physical_width(params_format(params));
	pr_info("[ info][%s] %s: i2s width: %u !!! \n", 
		LOG_FLAG, __func__, width);

	pr_debug("[debug][%s] %s: stream = %d, requested rate = %d, sample size = %d,"
						"physical size = %d, channel num = %d\n",
					LOG_FLAG, __func__, substream->stream, rate, 
					snd_pcm_format_width(params_format(params)), width, channels);

	return 0;
}

int sia91xx_startup(
	struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai)
{
	sipa_dev_t *si_pa = snd_soc_component_get_drvdata(dai->component);

	if (NULL == si_pa) {
		pr_err("[  err][%s] %s: si_pa is NULL\n", "sipa_91xx", __func__);
		return -EINVAL;
	}

	if (SNDRV_PCM_STREAM_CAPTURE == substream->stream)
		return 0;

	pr_debug("[debug][%s] %s: dai:%s, substream:%s, startup, stream = %d \n",
		"sipa_91xx", __func__, dai->name, substream->name, substream->stream);

	/* g3m: chip types 20/21/23/24 are powered by the reset pin going high (stock mask 0x1b00000) */
	if (si_pa->chip_type <= 0x18 && ((1u << si_pa->chip_type) & 0x1b00000)) {
		gpio_set_value(si_pa->rst_pin, 1);
		usleep_range(5000, 6000);
	} else if (SIA91XX_DISABLE_LEVEL == gpio_get_value(si_pa->rst_pin)) {
		gpio_set_value(si_pa->rst_pin, 0);
		usleep_range(5000, 6000);
	}

	if (si_pa->mute) {
		pr_info("[debug][%s] %s: pa is mute on, direct return!\n", "sipa_91xx", __func__);
		return 0;
	}

	if (si_pa->power_mode) {
		pr_info("[debug][%s] %s: si_pa->power_mode is true, direct return!\n", "sipa_91xx", __func__);
		return 0;
	}

	if (si_pa->chip_type == 0x14)
		gpio_set_value(si_pa->boost_pin, 1);

	sipa_reg_init(si_pa);
	sipa_regmap_write_sram(si_pa);

	if (sia91xx_smartpa_start(si_pa, substream->stream)) {
		pr_err("[  err][%s] %s: sia91xx_smartpa_start failed \n", "sipa_91xx", __func__);
		if (si_pa->chip_type == 0x14)
			gpio_set_value(si_pa->boost_pin, 0);
		return -SIPA_ERROR_SOFT_MUTE;
	}

	sipa_regmap_check_trimming(si_pa);
	si_pa->power_mode = 1;
	return 0;
}

int sia91xx_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	pr_debug("[debug][%s] %s: fmt = 0x%x\n", LOG_FLAG, __func__, fmt);

	/* Supported mode: regular I2S, slave, or PDM */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		if ((fmt & SND_SOC_DAIFMT_MASTER_MASK) != SND_SOC_DAIFMT_CBS_CFS) {
			pr_err("[  err][%s] %s: Invalid Codec master mode\n",LOG_FLAG, __func__);
			return -EINVAL;
		}
		break;
	default:
		pr_err("[  err][%s] %s: Unsupported DAI format %d \n",
				LOG_FLAG, __func__, fmt & SND_SOC_DAIFMT_FORMAT_MASK);
		return -EINVAL;
	}

	return 0;
}

int sia91xx_smartpa_soft_mute(sipa_dev_t *si_pa)
{
	if (sipa_regmap_set_chip_off(si_pa))
		return 0;

	return -1;
}

int sia91xx_smartpa_start(sipa_dev_t *si_pa, int stream)
{
	uint8_t dsp_count = 0;

	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		while (dsp_count < SIA91XX_MAX_DSP_START_TRY_COUNT) {
			if (sipa_regmap_set_chip_on(si_pa))
				break;
			dsp_count++;
		}

		if (dsp_count >= SIA91XX_MAX_DSP_START_TRY_COUNT) {
			pr_err("[  err][%s] %s: Failed starting device \n", "sipa_91xx", __func__);
			[[clang::always_inline]] mievent_report(0x36007bca, "PA data exception", &si_pa->client->dev);
			return -SIPA_ERROR_DEV_START;
		}
	}

	pr_debug("[debug][%s] %s: device start success! \n", "sipa_91xx", __func__);
	return 0;
}

int sia91xx_mute(
	struct snd_soc_dai *dai,
	int mute,
	int stream)
{
	static const int g3m_rst_off_level[5] = { 0, 0, 1, 0, 0 };	/* stock .rodata+0x5e84, chip types 20..24 */
	sipa_dev_t *si_pa = snd_soc_component_get_drvdata(dai->component);

	if (NULL == si_pa) {
		pr_err("[  err][%s] %s: si_pa is NULL\n", "sipa_91xx", __func__);
		return -EINVAL;
	}

	pr_debug("[debug][%s] %s: dai:%s,%s mute = %d stream = %d\n",
		"sipa_91xx", __func__, dai->name, dai->driver->name, mute, stream);

	if (si_pa->mute) {
		pr_info("[debug][%s] %s: pa is mute on, direct return!\n", "sipa_91xx", __func__);
		return 0;
	}

	if (stream != SNDRV_PCM_STREAM_PLAYBACK)
		return 0;

	/* g3t: stock structure: the boost pin debug line (type 0x14) is the common tail of the unmute path AND of a successful mute */
	if (mute) {
		if (!si_pa->power_mode) {
			pr_info("[debug][%s] %s: si_pa->power_mode is false, direct return!\n", "sipa_91xx", __func__);
			return 0;
		}

		si_pa->power_mode = 0;
		if (sia91xx_smartpa_soft_mute(si_pa)) {
			int idx = (int)si_pa->chip_type - 0x14;
			int level = ((unsigned int)idx <= 4u) ? g3m_rst_off_level[idx] : 1;

			gpio_set_value(si_pa->rst_pin, level);
			usleep_range(5000, 6000);
			pr_err("[  err][%s] %s: sia91xx_smartpa_soft_mute failed \n", "sipa_91xx", __func__);
			return -SIPA_ERROR_SOFT_MUTE;
		}

		pr_debug("[debug][%s] %s: device mute success \n", "sipa_91xx", __func__);
	}

	if (si_pa->chip_type == 0x14)
		pr_debug("[debug][%s] %s: channel = %d, boost_pin_val = %d \n",
			"sipa_91xx", __func__, si_pa->channel_num, gpio_get_value(si_pa->boost_pin));
	return 0;
}


#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 2, 0)
static struct snd_soc_dapm_context *snd_soc_codec_get_dapm(
	struct snd_soc_codec *codec)
{
	return &codec->dapm;
}
#endif

#if (LINUX_VERSION_CODE > KERNEL_VERSION(4, 16, 28))
int sia91xx_component_probe(struct snd_soc_component *component)
{
	struct snd_soc_dapm_widget *widgets;
	struct snd_soc_dapm_context *dapm;
	unsigned int num_dapm_widgets = ARRAY_SIZE(sia91xx_dapm_widgets_common);
	sipa_dev_t *si_pa;

	/* G40g: the two stock NULL checks (messages "component is NULL" / "si_pa is NULL") */
	if (!component) {
		pr_err("[  err][%s] %s: component is NULL\n", LOG_FLAG, __func__);
		return -EINVAL;
	}
	si_pa = snd_soc_component_get_drvdata(component);
	if (!si_pa) {
		pr_err("[  err][%s] %s: si_pa is NULL\n", LOG_FLAG, __func__);
		return -EINVAL;
	}

	dapm = snd_soc_component_get_dapm(component);

	INIT_DELAYED_WORK(&si_pa->interrupt_work, sia91xx_interrupt);
	//INIT_DELAYED_WORK(&si_pa->monitor_work, sia91xx_monitor);
	sia91xx_register_interrupt(si_pa);

	widgets = devm_kzalloc(&si_pa->client->dev,
							sizeof(struct snd_soc_dapm_widget) *
							ARRAY_SIZE(sia91xx_dapm_widgets_common),
							GFP_KERNEL);
	if (!widgets)
			return 0;

	memcpy(widgets, sia91xx_dapm_widgets_common,
					sizeof(struct snd_soc_dapm_widget) *
							ARRAY_SIZE(sia91xx_dapm_widgets_common));

	sia91xx_append_i2c_address(&si_pa->client->dev,
					si_pa->client,
					widgets,
					num_dapm_widgets,
					NULL,
					0);

	snd_soc_dapm_new_controls(dapm, widgets,
					ARRAY_SIZE(sia91xx_dapm_widgets_common));
	snd_soc_dapm_add_routes(dapm, sia91xx_dapm_routes_common,
					ARRAY_SIZE(sia91xx_dapm_routes_common));

	return 0;
}

#else
int sia91xx_codec_probe(struct snd_soc_codec *codec)
{
	struct snd_soc_dapm_widget *widgets;
	struct snd_soc_dapm_context *dapm;
	unsigned int num_dapm_widgets = ARRAY_SIZE(sia91xx_dapm_widgets_common);
	sipa_dev_t *si_pa = snd_soc_codec_get_drvdata(codec);

	si_pa->codec = codec;
	dapm = snd_soc_codec_get_dapm(codec);

	INIT_DELAYED_WORK(&si_pa->interrupt_work, sia91xx_interrupt);
	//INIT_DELAYED_WORK(&si_pa->monitor_work, sia91xx_monitor);
	sia91xx_register_interrupt(si_pa);

	widgets = devm_kzalloc(&si_pa->client->dev,
				sizeof(struct snd_soc_dapm_widget) *
				ARRAY_SIZE(sia91xx_dapm_widgets_common),
				GFP_KERNEL);
	if (!widgets)
		return 0;

	memcpy(widgets, sia91xx_dapm_widgets_common,
			sizeof(struct snd_soc_dapm_widget) *
				ARRAY_SIZE(sia91xx_dapm_widgets_common));

	sia91xx_append_i2c_address(&si_pa->client->dev,
			si_pa->client,
			widgets,
			num_dapm_widgets,
			NULL,
			0);

	snd_soc_dapm_new_controls(dapm, widgets,
			ARRAY_SIZE(sia91xx_dapm_widgets_common));
	snd_soc_dapm_add_routes(dapm, sia91xx_dapm_routes_common,
			ARRAY_SIZE(sia91xx_dapm_routes_common));

	return 0;
}
#endif
