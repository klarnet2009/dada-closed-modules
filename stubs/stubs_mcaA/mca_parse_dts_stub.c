// SPDX-License-Identifier: GPL-2.0
/* Stub provider of the mca_parse_dts.ko exports. */
#include <linux/module.h>
#include <mca/common/mca_parse_dts.h>

int mca_parse_dts_u8(const struct device_node *np, const char *prop, u8 *data, u8 default_value) { return 0; }
EXPORT_SYMBOL(mca_parse_dts_u8);
int mca_parse_dts_u32(const struct device_node *np, const char *prop, u32 *data, u32 default_value) { return 0; }
EXPORT_SYMBOL(mca_parse_dts_u32);
int mca_parse_dts_u8_array(const struct device_node *np, const char *prop, u8 *data, u16 len) { return 0; }
EXPORT_SYMBOL(mca_parse_dts_u8_array);
int mca_parse_dts_u32_array(const struct device_node *np, const char *prop, u32 *data, u32 len) { return 0; }
EXPORT_SYMBOL(mca_parse_dts_u32_array);
int mca_parse_dts_u8_count(const struct device_node *np, const char *prop, u32 row, u32 col) { return 0; }
EXPORT_SYMBOL(mca_parse_dts_u8_count);
int mca_parse_dts_u32_count(const struct device_node *np, const char *prop, u32 row, u32 col) { return 0; }
EXPORT_SYMBOL(mca_parse_dts_u32_count);
int mca_parse_dts_u32_index(const struct device_node *np, const char *prop, int index, u32 *data) { return 0; }
EXPORT_SYMBOL(mca_parse_dts_u32_index);
int mca_parse_dts_string(const struct device_node *np, const char *prop, const char **out) { return 0; }
EXPORT_SYMBOL(mca_parse_dts_string);
int mca_parse_dts_string_index(const struct device_node *np, const char *prop, int index, const char **out) { return 0; }
EXPORT_SYMBOL(mca_parse_dts_string_index);
int mca_parse_dts_count_strings(const struct device_node *np, const char *prop, u32 row, u32 col) { return 0; }
EXPORT_SYMBOL(mca_parse_dts_count_strings);
int mca_parse_dts_string_array(const struct device_node *np, const char *prop, int *data, u32 row, u32 col) { return 0; }
EXPORT_SYMBOL(mca_parse_dts_string_array);
MODULE_LICENSE("GPL");
