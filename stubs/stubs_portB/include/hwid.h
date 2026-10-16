/* SPDX-License-Identifier: GPL-2.0 */
/* hwid.h - API of the Xiaomi hwid.ko provider (only what the reconstructed modules use) */
#ifndef __HWID_H__
#define __HWID_H__

#include <linux/types.h>

uint32_t get_hw_country_version(void);

#endif
