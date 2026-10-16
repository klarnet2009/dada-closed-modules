/* SPDX-License-Identifier: GPL-2.0 */
/* hwid.h - exports of the Xiaomi hwid.ko (reconstructed; prototypes verified against the stock export CRCs) */
#ifndef __XIAOMI_HWID_H__
#define __XIAOMI_HWID_H__

#include <linux/types.h>

uint32_t get_hw_version_platform(void);
uint32_t get_hw_country_version(void);
uint32_t get_hw_version_major(void);
uint32_t get_hw_version_minor(void);
uint32_t get_hw_version_build(void);
uint32_t get_hw_project_adc(void);
uint32_t get_hw_build_adc(void);
uint32_t get_hw_id_value(void);
const char *product_name_get(void);

#endif
