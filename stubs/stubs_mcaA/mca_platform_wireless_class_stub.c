// SPDX-License-Identifier: GPL-2.0
/* Stub provider of (a subset of) the mca_platform_wireless_class.ko exports. */
#include <linux/module.h>
#include <mca/platform/platform_wireless_class.h>

int platform_class_wireless_get_fw_version(unsigned int role, char *buf) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_fw_version);
int platform_class_wireless_get_hall_gpio_status(unsigned int role, bool *status) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_hall_gpio_status);
int platform_class_wireless_get_tx_adapter(unsigned int role, int *adapter) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_tx_adapter);
int platform_class_wireless_get_vout(unsigned int role, int *vout) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_vout);
int platform_class_wireless_get_vrect(unsigned int role, int *vrect) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_vrect);
int platform_class_wireless_get_iout(unsigned int role, int *iout) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_iout);
int platform_class_wireless_get_ss_voltage(unsigned int role, int *ss_voltage) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_ss_voltage);
int platform_class_wireless_get_phone_case_category(unsigned int role, int *category) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_get_phone_case_category);
int platform_class_wireless_set_phone_case_category(unsigned int role, int category) { return 0; }
EXPORT_SYMBOL(platform_class_wireless_set_phone_case_category);
MODULE_LICENSE("GPL");
