/* SPDX-License-Identifier: GPL-2.0 */
/* platform_wireless_class.h - reconstructed API of mca_platform_wireless_class.ko (prototypes from the XRING sources, CRC-verified against the stock exporter) */
#ifndef __MCA_PLATFORM_PLATFORM_WIRELESS_CLASS_H__
#define __MCA_PLATFORM_PLATFORM_WIRELESS_CLASS_H__

#include <linux/types.h>

enum wireless_role {
	WIRELESS_ROLE_MASTER = 0,
};

int platform_class_wireless_get_fw_version(unsigned int role, char *buf);
int platform_class_wireless_get_hall_gpio_status(unsigned int role, bool *status);
int platform_class_wireless_get_tx_adapter(unsigned int role, int *adapter);
int platform_class_wireless_get_vout(unsigned int role, int *vout);
int platform_class_wireless_get_vrect(unsigned int role, int *vrect);
int platform_class_wireless_get_iout(unsigned int role, int *iout);
int platform_class_wireless_get_ss_voltage(unsigned int role, int *ss_voltage);
int platform_class_wireless_get_phone_case_category(unsigned int role, int *category);
int platform_class_wireless_set_phone_case_category(unsigned int role, int category);

#endif /* __MCA_PLATFORM_PLATFORM_WIRELESS_CLASS_H__ */
