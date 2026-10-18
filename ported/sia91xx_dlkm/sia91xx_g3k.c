// SPDX-License-Identifier: GPL-2.0
/*
 * g3k: functions of the Xiaomi stock sia91xx_dlkm that the Android S template lacks, written from the
 * disassembly (mievent_report, crc32, sia91xx_spksw_gpio_*). Mixer registration of the spksw controls is not
 * wired (g3k-guess: control names unrecovered), the functions are non-static so that they exist in the module.
 */
#include <linux/module.h>
#include <linux/gpio.h>
#include <sound/soc.h>
#include <miev/mievent.h>
#include "sipa_common.h"

int mievent_report(unsigned int eventid, const char *value, struct device *dev)
{
#if IS_ENABLED(CONFIG_MIEV)
	struct misight_mievent *mievent;
	char i2c_info[20] = {0};

	strcpy(i2c_info, dev->kobj.name);
	dev_info(dev, "%s: reg = %s, KeyWord = %s DFS report\n", __func__, i2c_info, value);
	mievent = cdev_tevent_alloc(eventid);
	cdev_tevent_add_str(mievent, "I2cAddress", i2c_info);
	cdev_tevent_add_str(mievent, "Keyword", value);
	cdev_tevent_write(mievent);
	cdev_tevent_destroy(mievent);
#endif
	return 0;
}

/* bitwise CRC-32 (poly 0xedb88320); returns 0 for an empty buffer (as the stock does) */
unsigned int crc32(const unsigned char *data, unsigned int len)
{
	unsigned int crc = 0xffffffff;
	int i;

	if (!len)
		return 0;
	while (len--) {
		crc ^= *data++;
		for (i = 0; i < 8; i++)
			crc = (crc & 1) ? (crc >> 1) ^ 0xedb88320 : crc >> 1;
	}

	return ~crc;
}

int sia91xx_spksw_gpio_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	sipa_dev_t *si_pa = snd_soc_component_get_drvdata(component);
	int level = gpiod_get_raw_value_cansleep(gpio_to_desc(si_pa->spksw_gpio));

	dev_info(&si_pa->client->dev, "spksw level cached = %d, actual = %d\n", si_pa->spksw_level, level);
	if (level < 0) {
		ucontrol->value.integer.value[0] = si_pa->spksw_level;
	} else {
		ucontrol->value.integer.value[0] = level;
		si_pa->spksw_level = level;
	}
	return 0;
}

int sia91xx_spksw_gpio_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	sipa_dev_t *si_pa = snd_soc_component_get_drvdata(component);
	int level = ucontrol->value.integer.value[0];

	dev_info(&si_pa->client->dev, "spksw = %d\n", level);
	gpiod_set_raw_value_cansleep(gpio_to_desc(si_pa->spksw_gpio), level != 0);
	return 0;
}
