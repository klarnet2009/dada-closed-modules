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

	__builtin_strcpy(i2c_info, dev->kobj.name);	/* g3r: plain strcpy call as in the stock (no fortify wrapper) */
	dev_info(dev, "%s: reg = %s, KeyWord = %s DFS report\n", __func__, i2c_info, value);
	mievent = cdev_tevent_alloc(eventid);
	cdev_tevent_add_str(mievent, "I2cAddress", i2c_info);
	cdev_tevent_add_str(mievent, "Keyword", value);
	cdev_tevent_write(mievent);
	cdev_tevent_destroy(mievent);
#endif
	return 0;
}
