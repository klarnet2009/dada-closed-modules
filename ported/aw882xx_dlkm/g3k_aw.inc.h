/*
 * g3k: functions of the Xiaomi stock aw882xx_dlkm that the OnePlus template lacks, written from the disassembly.
 * Included at the end of aw882xx.c. The mixer controls are NOT registered (g3k-guess: control names unrecovered);
 * the functions are __used so that they exist in the module.
 */
#include <linux/gpio.h>
#include <miev/mievent.h>

int mievent_report(unsigned int eventid, const char *value, struct device *dev)
{
#if IS_ENABLED(CONFIG_MIEV)
	struct misight_mievent *mievent;
	char i2c_info[20] = {0};

	/* g3r: the stock calls the plain strcpy (no fortify wrapper) */
	__builtin_strcpy(i2c_info, dev->kobj.name);
	dev_info(dev, "%s: reg = %s, KeyWord = %s DFS report\n", __func__, i2c_info, value);
	mievent = cdev_tevent_alloc(eventid);
	cdev_tevent_add_str(mievent, "I2cAddress", i2c_info);
	cdev_tevent_add_str(mievent, "Keyword", value);
	cdev_tevent_write(mievent);
	cdev_tevent_destroy(mievent);
#endif
	return 0;
}

/* speaker switch gpio (stock: gpio at aw882xx+0x24, cached level at +0x28) */
static int __used aw882xx_spksw_gpio_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct aw882xx *aw882xx = snd_soc_component_get_drvdata(component);
	int level = gpiod_get_raw_value_cansleep(gpio_to_desc(aw882xx->spksw_gpio));

	dev_info(aw882xx->dev, "spksw level cached = %d, actual = %d\n", aw882xx->spksw_level, level);
	if (level < 0) {
		ucontrol->value.enumerated.item[0] = aw882xx->spksw_level;
	} else {
		ucontrol->value.enumerated.item[0] = level;
		aw882xx->spksw_level = level;
	}
	return 0;
}

static int __used aw882xx_spksw_gpio_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct aw882xx *aw882xx = snd_soc_component_get_drvdata(component);
	int level = ucontrol->value.integer.value[0];

	dev_info(aw882xx->dev, "spksw = %d\n", level);
	gpiod_set_raw_value_cansleep(gpio_to_desc(aw882xx->spksw_gpio), level != 0);
	return 0;
}
