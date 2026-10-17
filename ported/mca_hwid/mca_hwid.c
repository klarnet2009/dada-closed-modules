// SPDX-License-Identifier: GPL-2.0
/*
 * mca_hwid.c
 *
 * get hwid infomation interface for power module
 *
 * Copyright (c) 2024-2024 Xiaomi Technologies Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */
#include <linux/module.h>
#include <mca/common/mca_hwid.h>
#include <mca/common/mca_log.h>
#include <hwid.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_hwid"
#endif

/* provider: mca_qcom_smem (OS4 stock) */
int get_smem_battery_is_new_batt_sn(bool *is_new);
/* OS4 addition, exported */
bool mca_is_warsaw_p2_before(const struct mca_hwid *hwid);

static struct mca_hwid *phwid;

const struct mca_hwid *mca_get_hwid_info(void)
{
	if (!phwid) {
		phwid = kzalloc(sizeof(struct mca_hwid), GFP_KERNEL);
		if (!phwid) {
#line 36
			mca_log_err("kzalloc phwid failed\n");
			return NULL;
		}
		phwid->platform_version = get_hw_version_platform();
		phwid->country_version = get_hw_country_version();
		phwid->major_version = get_hw_version_major();
		phwid->minor_version = get_hw_version_minor();
		phwid->build_version = get_hw_version_build();
		phwid->product_adc = get_hw_project_adc();
		phwid->build_adc = get_hw_build_adc();
		phwid->hwid_value = get_hw_id_value();
		phwid->product_name = product_name_get();
		if (phwid->platform_version == 7 && phwid->country_version == 2)
			phwid->country_version = 0;

		if (phwid->platform_version == 10) {
			bool is_new_batt_sn = false;

			get_smem_battery_is_new_batt_sn(&is_new_batt_sn);
			if (is_new_batt_sn) {
				switch (phwid->hwid_value) {
				case 0x10007:
				case 0x10008:
				case 0x20007:
				case 0x20008:
				case 0x90008:
				case 0x110008:
				case 0x120008:
				case 0x190008:
				case 0x210008:
				case 0x220008:
				case 0x290008:
					phwid->product_name = "donghai";
					break;
				default:
					break;
				}
			}
		}
		if (phwid->platform_version == 12) {
			switch (phwid->hwid_value) {
			case 8:
			case 9:
			case 0x10008:
			case 0x20008:
			case 0x90008:
				phwid->product_name = "yanan";
				break;
			default:
				break;
			}
		}

#line 89
		mca_log_err("platform_version: %d, country_version: %d, major_version: %d, minor_version: %d, build_version: %d, product_name: %s, hwid_value: %#x", phwid->platform_version, phwid->country_version, phwid->major_version, phwid->minor_version, phwid->build_version, phwid->product_name, phwid->hwid_value);
	}
	return phwid;
}
EXPORT_SYMBOL(mca_get_hwid_info);

bool mca_is_warsaw_p2_before(const struct mca_hwid *hwid)
{
	if (!hwid)
		return false;

	if (hwid->platform_version == 12) {
#line 100
		mca_log_err("warsaw hwid_value: %#x\n", hwid->hwid_value);
		switch (hwid->hwid_value) {
		case 1:
		case 8:
		case 9:
		case 0x10000:
		case 0x10001:
			return true;
		default:
			break;
		}
	}
	return false;
}
EXPORT_SYMBOL(mca_is_warsaw_p2_before);

MODULE_DESCRIPTION("mca get hwid info");
MODULE_AUTHOR("wangyun5@xiaomi.com");
MODULE_LICENSE("GPL v2");
