/* SPDX-License-Identifier: GPL-2.0 */
/* HWID external interface used by xiaomi_wifi_gpio (provided by hwid.ko). */
#ifndef __HWID_H__
#define __HWID_H__

#include <linux/types.h>

enum hw_country {
	CountryCN = 0,
};

uint32_t get_hw_country_version(void);

#endif
